#include "fs/fat32.h"
#include "kernel.h"
#include "drivers/vga.h"
#include "cpu/heap.h"

/* Optimized global state - single structure with everything */
static struct {
    uint8_t initialized;
    uint8_t *disk_image;
    uint8_t *cluster_buffer;
    uint32_t *fat_cache;
    
    /* Boot sector data - only store what we need */
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    
    /* Calculated values */
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t bytes_per_cluster;
    
    /* File handles */
    struct fat32_file files[FAT32_MAX_OPEN_FILES];
    uint32_t current_dir_cluster;
} g_fs = {0};

static const uint32_t DISK_SIZE = 32 * 1024 * 1024; // 32MB
/* Global FAT32 file system state */
static struct fat32_fs g_fat32_fs = {0};
static struct fat32_file g_open_files[FAT32_MAX_OPEN_FILES] = {0};
static int g_current_dir_cluster = 0;
static struct disk_handle *g_disk_handle = NULL;

/* Helper function prototypes */
static int fat32_read_boot_sector(void);
static int fat32_read_fat_cache(void);
static uint32_t fat32_cluster_to_sector(uint32_t cluster);
static int fat32_read_cluster(uint32_t cluster, void *buffer);
static int fat32_write_cluster(uint32_t cluster, const void *buffer);
static struct fat32_file* fat32_find_free_handle(void);
static int fat32_find_file_in_dir(uint32_t dir_cluster, const char *filename, struct fat32_dir_entry *entry);
static int fat32_parse_mode(const char *mode);
static void fat32_close_all_files(void);
static int fat32_create_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                                  uint32_t first_cluster, uint32_t file_size, uint8_t attributes);
static int fat32_update_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                                  uint32_t file_size);

/* Optimized helper functions - inline where possible */
static inline uint32_t cluster_to_sector(uint32_t cluster) {
    return cluster < 2 ? 0 : g_fs.data_start_sector + ((cluster - 2) * g_fs.sectors_per_cluster);
}

static inline int read_sectors(uint32_t start, uint32_t count, void *buffer) {
    if (!g_fs.disk_image || !buffer) return FAT32_ERROR_IO;
    uint32_t offset = start * g_fs.bytes_per_sector;
    uint32_t size = count * g_fs.bytes_per_sector;
    if (offset + size > DISK_SIZE) return FAT32_ERROR_IO;
    memcpy(buffer, g_fs.disk_image + offset, size);
    return FAT32_SUCCESS;
}

static inline int write_sectors(uint32_t start, uint32_t count, const void *buffer) {
    if (!g_fs.disk_image || !buffer) return FAT32_ERROR_IO;
    uint32_t offset = start * g_fs.bytes_per_sector;
    uint32_t size = count * g_fs.bytes_per_sector;
    if (offset + size > DISK_SIZE) return FAT32_ERROR_IO;
    memcpy(g_fs.disk_image + offset, buffer, size);
    return FAT32_SUCCESS;
}

static inline int read_cluster(uint32_t cluster, void *buffer) {
    uint32_t sector = cluster_to_sector(cluster);
    return sector ? read_sectors(sector, g_fs.sectors_per_cluster, buffer) : FAT32_ERROR_INVALID;
}

static inline int write_cluster(uint32_t cluster, const void *buffer) {
    uint32_t sector = cluster_to_sector(cluster);
    return sector ? write_sectors(sector, g_fs.sectors_per_cluster, buffer) : FAT32_ERROR_INVALID;
}

static struct fat32_file* find_free_handle(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!g_fs.files[i].is_open) return &g_fs.files[i];
    }
    return NULL;
}

static int parse_mode(const char *mode) {
    int file_mode = 0;
    for (; *mode; mode++) {
        switch (*mode) {
            case 'r': file_mode |= FAT32_MODE_READ; break;
            case 'w': file_mode |= FAT32_MODE_WRITE | FAT32_MODE_CREATE; break;
            case 'a': file_mode |= FAT32_MODE_WRITE | FAT32_MODE_APPEND | FAT32_MODE_CREATE; break;
            case '+': file_mode |= FAT32_MODE_READ | FAT32_MODE_WRITE; break;
        }
    }
    return file_mode;
}

/* Optimized directory operations */
static int find_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                         struct fat32_dir_entry *entry, int *entry_index) {
    if (read_cluster(dir_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) return 0;
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fs.cluster_buffer;
    int count = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & (FAT32_ATTR_LONG_NAME | FAT32_ATTR_VOLUME_ID)) continue;
        
        if (memcmp(entries[i].name, formatted_name, 11) == 0) {
            if (entry) memcpy(entry, &entries[i], sizeof(struct fat32_dir_entry));
            if (entry_index) *entry_index = i;
            return 1;
        }
    }
    return 0;
}

static int update_file_size_in_dir(uint32_t dir_cluster, int entry_index, uint32_t new_size) {
    if (read_cluster(dir_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) return FAT32_ERROR_IO;
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fs.cluster_buffer;
    entries[entry_index].file_size = new_size;
    
    return write_cluster(dir_cluster, g_fs.cluster_buffer);
}

static int create_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                           uint32_t first_cluster, uint32_t file_size, uint8_t attributes) {
    if (read_cluster(dir_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) return FAT32_ERROR_IO;
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fs.cluster_buffer;
    int count = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < count; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            memcpy(entries[i].name, formatted_name, 11);
            entries[i].attributes = attributes;
            entries[i].reserved = 0;
            entries[i].creation_time_tenth = 0;
            entries[i].creation_time = 0;
            entries[i].creation_date = 0;
            entries[i].last_access_date = 0;
            entries[i].first_cluster_high = (uint16_t)(first_cluster >> 16);
            entries[i].last_write_time = 0;
            entries[i].last_write_date = 0;
            entries[i].first_cluster_low = (uint16_t)(first_cluster & 0xFFFF);
            entries[i].file_size = file_size;
            
            if (i + 1 < count && entries[i + 1].name[0] != 0xE5) {
                entries[i + 1].name[0] = 0x00;
            }
            
            return write_cluster(dir_cluster, g_fs.cluster_buffer);
        }
    }
    return FAT32_ERROR_NO_SPACE;
}

/* Create minimal boot sector */
static void create_boot_sector(void) {
    struct fat32_boot_sector *boot = (struct fat32_boot_sector*)g_fs.disk_image;
    memset(boot, 0, sizeof(struct fat32_boot_sector));
    
    boot->jump_code[0] = 0xEB; boot->jump_code[1] = 0x58; boot->jump_code[2] = 0x90;
    memcpy(boot->oem_name, "NumOS   ", 8);
    boot->bytes_per_sector = FAT32_SECTOR_SIZE;
    boot->sectors_per_cluster = 8;
    boot->reserved_sectors = 32;
    boot->num_fats = 2;
    boot->total_sectors_32 = DISK_SIZE / FAT32_SECTOR_SIZE;
    boot->sectors_per_fat_32 = 256;
    boot->root_cluster = 2;
    boot->fsinfo_sector = 1;
    boot->backup_boot_sector = 6;
    boot->drive_number = 0x80;
    boot->boot_signature = 0x29;
    boot->volume_id = 0x12345678;
    memcpy(boot->volume_label, "NumOS FAT32", 11);
    memcpy(boot->filesystem_type, "FAT32   ", 8);
    boot->boot_sector_signature = 0xAA55;
    
    /* Initialize FAT table */
    uint32_t *fat = (uint32_t*)(g_fs.disk_image + (boot->reserved_sectors * FAT32_SECTOR_SIZE));
    fat[0] = 0x0FFFFF00 | 0xF8;
    fat[1] = 0x0FFFFFFF;
    fat[2] = 0x0FFFFFFF;
}

int fat32_init(void) {
    if (g_fat32_fs.initialized) {
        vga_writestring("FAT32: Already initialized\n");
        return FAT32_SUCCESS;
    }
    
    /* Open disk handle */
    g_disk_handle = disk_open(0); /* Use disk 0 */
    if (!g_disk_handle) {
        vga_writestring("FAT32: Failed to open disk 0\n");
        return FAT32_ERROR_IO;
    }
    
    /* Clear open files array */
    memset(g_open_files, 0, sizeof(g_open_files));
    
    vga_writestring("FAT32: File system initialized with disk driver\n");
    return FAT32_SUCCESS;
}

int fat32_mount(void) {
    if (g_fat32_fs.initialized) {
        vga_writestring("FAT32: Already mounted\n");
        return FAT32_SUCCESS;
    }
    
    if (!g_disk_handle) {
        vga_writestring("FAT32: No disk handle available\n");
        return FAT32_ERROR_NOT_FOUND;
    }
    
    /* Read boot sector */
    if (fat32_read_boot_sector() != FAT32_SUCCESS) {
        vga_writestring("FAT32: Failed to read boot sector\n");
        return FAT32_ERROR_IO;
    }
    
    /* Validate FAT32 signature */
    if (g_fat32_fs.boot_sector.boot_sector_signature != 0xAA55) {
        vga_writestring("FAT32: Invalid boot sector signature\n");
        return FAT32_ERROR_INVALID;
    }
    
    /* Calculate file system parameters */
    g_fat32_fs.fat_start_sector = g_fat32_fs.boot_sector.reserved_sectors;
    g_fat32_fs.data_start_sector = g_fat32_fs.fat_start_sector + 
        (g_fat32_fs.boot_sector.num_fats * g_fat32_fs.boot_sector.sectors_per_fat_32);
    g_fat32_fs.root_dir_cluster = g_fat32_fs.boot_sector.root_cluster;
    g_fat32_fs.sectors_per_fat = g_fat32_fs.boot_sector.sectors_per_fat_32;
    g_fat32_fs.bytes_per_cluster = g_fat32_fs.boot_sector.bytes_per_sector * 
        g_fat32_fs.boot_sector.sectors_per_cluster;
    
    /* Allocate cluster buffer */
    g_fat32_fs.cluster_buffer = (uint8_t*)kmalloc(g_fat32_fs.bytes_per_cluster);
    if (!g_fat32_fs.cluster_buffer) {
        vga_writestring("FAT32: Failed to allocate cluster buffer\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Read and cache FAT */
    if (fat32_read_fat_cache() != FAT32_SUCCESS) {
        vga_writestring("FAT32: Failed to read FAT cache\n");
        kfree(g_fat32_fs.cluster_buffer);
        return FAT32_ERROR_IO;
    }
    
    g_fat32_fs.initialized = 1;
    g_current_dir_cluster = g_fat32_fs.root_dir_cluster;
    
    vga_writestring("FAT32: File system mounted successfully\n");
    return FAT32_SUCCESS;
}

void fat32_unmount(void) {
    if (!g_fat32_fs.initialized) {
        return;
    }
    
    /* Close all open files */
    fat32_close_all_files();
    
    /* Flush disk cache */
    if (g_disk_handle) {
        disk_flush_cache(g_disk_handle);
    }
    
    /* Free allocated memory */
    if (g_fat32_fs.fat_cache) {
        kfree(g_fat32_fs.fat_cache);
        g_fat32_fs.fat_cache = NULL;
    }
    
    if (g_fat32_fs.cluster_buffer) {
        kfree(g_fat32_fs.cluster_buffer);
        g_fat32_fs.cluster_buffer = NULL;
    }
    
    g_fat32_fs.initialized = 0;
    vga_writestring("FAT32: File system unmounted\n");
}

struct fat32_file* fat32_fopen(const char *filename, const char *mode) {
    if (!g_fat32_fs.initialized) {
        vga_writestring("FAT32: File system not mounted\n");
        return NULL;
    }
    
    if (!filename || !mode) {
        return NULL;
    }
    
    /* Find free file handle */
    struct fat32_file *file = fat32_find_free_handle();
    if (!file) {
        vga_writestring("FAT32: No free file handles\n");
        return NULL;
    }
    
    /* Parse mode */
    int file_mode = fat32_parse_mode(mode);
    if (file_mode < 0) {
        vga_writestring("FAT32: Invalid file mode\n");
        return NULL;
    }
    
    /* Format filename */
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        vga_writestring("FAT32: Invalid filename\n");
        return NULL;
    }
    
    /* Search for file in current directory */
    struct fat32_dir_entry dir_entry;
    int found = fat32_find_file_in_dir(g_current_dir_cluster, formatted_name, &dir_entry);
    
    if (found && (file_mode & FAT32_MODE_CREATE) && !(file_mode & FAT32_MODE_APPEND)) {
        vga_writestring("FAT32: File already exists\n");
        return NULL;
    }
    
    if (!found && !(file_mode & FAT32_MODE_CREATE)) {
        vga_writestring("FAT32: File not found\n");
        return NULL;
    }
    
    /* Initialize file handle */
    strcpy(file->name, filename);
    file->mode = file_mode;
    file->position = 0;
    file->is_open = 1;
    
    if (found) {
        /* Existing file */
        file->first_cluster = ((uint32_t)dir_entry.first_cluster_high << 16) | 
                             dir_entry.first_cluster_low;
        file->current_cluster = file->first_cluster;
        file->file_size = dir_entry.file_size;
        file->attributes = dir_entry.attributes;
        
        if (file_mode & FAT32_MODE_APPEND) {
            file->position = file->file_size;
        }
        
        // If opening for write (not append), truncate the file
        if ((file_mode & FAT32_MODE_WRITE) && !(file_mode & FAT32_MODE_APPEND)) {
            file->file_size = 0;
            file->position = 0;
            // Update directory entry
            fat32_update_dir_entry(g_current_dir_cluster, formatted_name, 0);
        }
    } else {
        /* New file - create directory entry and allocate first cluster */
        file->first_cluster = fat32_allocate_cluster();
        if (file->first_cluster == 0) {
            file->is_open = 0;
            vga_writestring("FAT32: Failed to allocate cluster for new file\n");
            return NULL;
        }
        file->current_cluster = file->first_cluster;
        file->file_size = 0;
        file->attributes = FAT32_ATTR_ARCHIVE;
        
        /* Create directory entry */
        if (fat32_create_dir_entry(g_current_dir_cluster, formatted_name, 
                                  file->first_cluster, 0, FAT32_ATTR_ARCHIVE) != FAT32_SUCCESS) {
            // Failed to create directory entry, free the allocated cluster
            fat32_free_cluster_chain(file->first_cluster);
            file->is_open = 0;
            vga_writestring("FAT32: Failed to create directory entry\n");
            return NULL;
        }
    }
    
    return file;
}

int fat32_fclose(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Flush any pending writes */
    fat32_fflush(file);
    
    /* Update directory entry if file was modified */
    if (file->mode & FAT32_MODE_WRITE) {
        char formatted_name[12];
        if (fat32_format_name(file->name, formatted_name) == FAT32_SUCCESS) {
            fat32_update_dir_entry(g_current_dir_cluster, formatted_name, file->file_size);
        }
    }
    
    /* Flush disk cache to ensure data persistence */
    if (g_disk_handle) {
        disk_flush_cache(g_disk_handle);
    }
    
    /* Mark file as closed */
    file->is_open = 0;
    memset(file, 0, sizeof(struct fat32_file));
    
    return FAT32_SUCCESS;
}

size_t fat32_fread(void *buffer, size_t size, size_t count, struct fat32_file *file) {
    if (!file || !file->is_open || !buffer || !(file->mode & FAT32_MODE_READ)) return 0;
    
    size_t total_bytes = size * count;
    size_t bytes_read = 0;
    uint8_t *buf = (uint8_t*)buffer;
    
    while (bytes_read < total_bytes && file->position < file->file_size) {
        if (read_cluster(file->current_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) break;
        
        uint32_t cluster_offset = file->position % g_fs.bytes_per_cluster;
        uint32_t bytes_available = g_fs.bytes_per_cluster - cluster_offset;
        uint32_t bytes_remaining = file->file_size - file->position;
        uint32_t bytes_to_read = total_bytes - bytes_read;
        
        if (bytes_to_read > bytes_available) bytes_to_read = bytes_available;
        if (bytes_to_read > bytes_remaining) bytes_to_read = bytes_remaining;
        
        memcpy(buf + bytes_read, g_fs.cluster_buffer + cluster_offset, bytes_to_read);
        bytes_read += bytes_to_read;
        file->position += bytes_to_read;
        
        if ((file->position % g_fs.bytes_per_cluster) == 0 && file->position < file->file_size) {
            uint32_t next = fat32_get_next_cluster(file->current_cluster);
            if (next >= FAT32_END_OF_CHAIN) break;
            file->current_cluster = next;
        }
    }
    
    return bytes_read / size;
}

size_t fat32_fwrite(const void *buffer, size_t size, size_t count, struct fat32_file *file) {
    if (!file || !file->is_open || !buffer || !(file->mode & FAT32_MODE_WRITE)) return 0;
    
    size_t total_bytes = size * count;
    size_t bytes_written = 0;
    const uint8_t *buf = (const uint8_t*)buffer;
    
    while (bytes_written < total_bytes) {
        if (file->current_cluster == 0 || file->current_cluster >= FAT32_END_OF_CHAIN) {
            file->current_cluster = fat32_allocate_cluster();
            if (file->current_cluster == 0) break;
        }
        
        if (read_cluster(file->current_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) break;
        
        uint32_t cluster_offset = file->position % g_fs.bytes_per_cluster;
        uint32_t bytes_available = g_fs.bytes_per_cluster - cluster_offset;
        uint32_t bytes_to_write = total_bytes - bytes_written;
        if (bytes_to_write > bytes_available) bytes_to_write = bytes_available;
        
        memcpy(g_fs.cluster_buffer + cluster_offset, buf + bytes_written, bytes_to_write);
        
        if (write_cluster(file->current_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) break;
        
        bytes_written += bytes_to_write;
        file->position += bytes_to_write;
        if (file->position > file->file_size) file->file_size = file->position;
        
        if ((file->position % g_fs.bytes_per_cluster) == 0 && bytes_written < total_bytes) {
            uint32_t next = fat32_get_next_cluster(file->current_cluster);
            if (next >= FAT32_END_OF_CHAIN) {
                next = fat32_allocate_cluster();
                if (next == 0) break;
                g_fs.fat_cache[file->current_cluster] = next;
            }
            file->current_cluster = next;
        }
    }
    
    return bytes_written / size;
}

int fat32_fseek(struct fat32_file *file, long offset, int whence) {
    if (!file || !file->is_open) return FAT32_ERROR_INVALID;
    
    uint32_t new_pos = (whence == FAT32_SEEK_SET) ? offset :
                       (whence == FAT32_SEEK_CUR) ? file->position + offset :
                       (whence == FAT32_SEEK_END) ? file->file_size + offset : file->position;
    
    if (new_pos > file->file_size) new_pos = file->file_size;
    file->position = new_pos;
    
    /* Update current cluster */
    if (new_pos == 0) {
        file->current_cluster = file->first_cluster;
    } else {
        uint32_t target_cluster = new_pos / g_fs.bytes_per_cluster;
        file->current_cluster = file->first_cluster;
        for (uint32_t i = 0; i < target_cluster; i++) {
            uint32_t next = fat32_get_next_cluster(file->current_cluster);
            if (next >= FAT32_END_OF_CHAIN) break;
            file->current_cluster = next;
        }
    }
    
    return FAT32_SUCCESS;
}

long fat32_ftell(struct fat32_file *file) {
    return (file && file->is_open) ? (long)file->position : -1;
}

int fat32_feof(struct fat32_file *file) {
    return (!file || !file->is_open) ? 1 : (file->position >= file->file_size);
}

int fat32_fflush(struct fat32_file *file) {
    return (file && file->is_open) ? FAT32_SUCCESS : FAT32_ERROR_INVALID;
}

void fat32_list_files(void) {
    if (!g_fs.initialized) return;
    
    vga_writestring("Directory listing:\n");
    
    if (read_cluster(g_fs.current_dir_cluster, g_fs.cluster_buffer) != FAT32_SUCCESS) {
        vga_writestring("Failed to read directory\n");
        return;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fs.cluster_buffer;
    int count = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & (FAT32_ATTR_LONG_NAME | FAT32_ATTR_VOLUME_ID)) continue;
        
        /* Format filename */
        char filename[13];
        int pos = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) filename[pos++] = entries[i].name[j];
        if (entries[i].name[8] != ' ') {
            filename[pos++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) filename[pos++] = entries[i].name[j];
        }
        filename[pos] = '\0';
        
        vga_writestring("  ");
        vga_writestring(filename);
        vga_writestring(" (");
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) {
            vga_writestring("DIR");
        } else {
            print_dec(entries[i].file_size);
            vga_writestring(" bytes");
        }
        vga_writestring(")\n");
    }
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    return (g_fs.fat_cache && cluster >= 2) ? (g_fs.fat_cache[cluster] & 0x0FFFFFFF) : FAT32_END_OF_CHAIN;
}

uint32_t fat32_allocate_cluster(void) {
    if (!g_fs.fat_cache) return 0;
    
    uint32_t total_clusters = (DISK_SIZE / g_fs.bytes_per_sector - g_fs.data_start_sector) / g_fs.sectors_per_cluster;
    for (uint32_t i = 2; i < total_clusters + 2; i++) {
        if ((g_fs.fat_cache[i] & 0x0FFFFFFF) == FAT32_FREE_CLUSTER) {
            g_fs.fat_cache[i] = FAT32_END_OF_CHAIN;
            return i;
        }
    }
    return 0;
}

int fat32_free_cluster_chain(uint32_t start_cluster) {
    if (!g_fs.fat_cache || start_cluster < 2) return FAT32_ERROR_INVALID;
    
    uint32_t current = start_cluster;
    while (current < FAT32_END_OF_CHAIN) {
        uint32_t next = g_fs.fat_cache[current] & 0x0FFFFFFF;
        g_fs.fat_cache[current] = FAT32_FREE_CLUSTER;
        current = next;
    }
    return FAT32_SUCCESS;
}

int fat32_format_name(const char *filename, char *formatted_name) {
    if (!filename || !formatted_name) return FAT32_ERROR_INVALID;
    
    memset(formatted_name, ' ', 11);
    formatted_name[11] = '\0';
    
    int name_pos = 0, ext_pos = 8;
    const char *dot = strstr(filename, ".");
    
    /* Copy name part */
    for (const char *p = filename; *p && p != dot && name_pos < 8; p++) {
        char c = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
            formatted_name[name_pos++] = c;
        } else {
            return FAT32_ERROR_INVALID;
        }
    }
    
    /* Copy extension part */
    if (dot) {
        for (const char *p = dot + 1; *p && ext_pos < 11; p++) {
            char c = (*p >= 'a' && *p <= 'z') ? *p - 32 : *p;
            if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-') {
                formatted_name[ext_pos++] = c;
            } else {
                return FAT32_ERROR_INVALID;
            }
        }
    }
    
    return FAT32_SUCCESS;
}

int fat32_exists(const char *filename) {
    if (!g_fs.initialized || !filename) return 0;
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) return 0;
    
    return find_dir_entry(g_fs.current_dir_cluster, formatted_name, NULL, NULL);
}

uint32_t fat32_get_file_size(const char *filename) {
    if (!filename) return 0;
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) return 0;
    
    struct fat32_dir_entry entry;
    return find_dir_entry(g_fs.current_dir_cluster, formatted_name, &entry, NULL) ? entry.file_size : 0;
}

void fat32_print_file_info(const char *filename) {
    if (!filename) return;
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        vga_writestring("Invalid filename\n");
        return;
    }
    
    struct fat32_dir_entry entry;
    if (!find_dir_entry(g_fs.current_dir_cluster, formatted_name, &entry, NULL)) {
        vga_writestring("File not found\n");
        return;
    }
    
    vga_writestring("File: ");
    vga_writestring(filename);
    vga_writestring("\nSize: ");
    print_dec(entry.file_size);
    vga_writestring(" bytes\nCluster: ");
    print_dec(((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low);
    vga_writestring("\nAttributes: ");
    if (entry.attributes & FAT32_ATTR_READ_ONLY) vga_writestring("R");
    if (entry.attributes & FAT32_ATTR_HIDDEN) vga_writestring("H");
    if (entry.attributes & FAT32_ATTR_SYSTEM) vga_writestring("S");
    if (entry.attributes & FAT32_ATTR_DIRECTORY) vga_writestring("D");
    if (entry.attributes & FAT32_ATTR_ARCHIVE) vga_writestring("A");
    vga_putchar('\n');
}

void fat32_print_boot_sector(void) {
    if (!g_fs.initialized) return;
    
    vga_writestring("FAT32 Boot Sector:\n");
    vga_writestring("  Bytes/sector: ");
    print_dec(g_fs.bytes_per_sector);
    vga_writestring("\n  Sectors/cluster: ");
    print_dec(g_fs.sectors_per_cluster);
    vga_writestring("\n  Reserved sectors: ");
    print_dec(g_fs.reserved_sectors);
    vga_writestring("\n  Number of FATs: ");
    print_dec(g_fs.num_fats);
    vga_writestring("\n  Sectors per FAT: ");
    print_dec(g_fs.sectors_per_fat);
    vga_writestring("\n  Root cluster: ");
    print_dec(g_fs.root_cluster);
    vga_putchar('\n');
}

void fat32_print_fs_info(void) {
    if (!g_fs.initialized) return;
    
    vga_writestring("FAT32 Filesystem Info:\n");
    vga_writestring("  FAT start: sector ");
    print_dec(g_fs.fat_start_sector);
    vga_writestring("\n  Data start: sector ");
    print_dec(g_fs.data_start_sector);
    vga_writestring("\n  Bytes per cluster: ");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring("\n  Current directory: cluster ");
    print_dec(g_fs.current_dir_cluster);
    
    /* Count open files */
    int open_files = 0;
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (g_fs.files[i].is_open) open_files++;
    }
    vga_writestring("\n  Open files: ");
    print_dec(open_files);
    vga_writestring("/");
    print_dec(FAT32_MAX_OPEN_FILES);
    vga_putchar('\n');
}

/* Low-level disk I/O implementations using disk driver */
int fat32_read_sector(uint32_t sector, void *buffer) {
    if (!g_disk_handle || !buffer) {
        return FAT32_ERROR_IO;
    }
    
    int result = disk_read_sector(g_disk_handle, sector, buffer);
    return (result == DISK_SUCCESS) ? FAT32_SUCCESS : FAT32_ERROR_IO;
}

int fat32_write_sector(uint32_t sector, const void *buffer) {
    if (!g_disk_handle || !buffer) {
        return FAT32_ERROR_IO;
    }
    
    int result = disk_write_sector(g_disk_handle, sector, buffer);
    return (result == DISK_SUCCESS) ? FAT32_SUCCESS : FAT32_ERROR_IO;
}

int fat32_read_sectors(uint32_t start_sector, uint32_t count, void *buffer) {
    if (!g_disk_handle || !buffer || count == 0) {
        return FAT32_ERROR_IO;
    }
    
    int result = disk_read_sectors(g_disk_handle, start_sector, count, buffer);
    return (result == DISK_SUCCESS) ? FAT32_SUCCESS : FAT32_ERROR_IO;
}

int fat32_write_sectors(uint32_t start_sector, uint32_t count, const void *buffer) {
    if (!g_disk_handle || !buffer || count == 0) {
        return FAT32_ERROR_IO;
    }
    
    int result = disk_write_sectors(g_disk_handle, start_sector, count, buffer);
    return (result == DISK_SUCCESS) ? FAT32_SUCCESS : FAT32_ERROR_IO;
}
