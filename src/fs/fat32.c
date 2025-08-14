#include "fat32.h"
#include "kernel.h"
#include "vga.h"
#include "heap.h"

/* Global file system state */
static struct fat32_fs fs = {0};
static int fs_mounted = 0;
static uint32_t current_drive = 0;

/* Helper functions */
static int fat32_read_boot_sector(void);
static int fat32_load_fat_table(void);
static uint32_t fat32_get_fat_entry(uint32_t cluster);
static int fat32_set_fat_entry(uint32_t cluster, uint32_t value);
static struct fat32_dir_entry* fat32_find_file(uint32_t dir_cluster, const char *name);
static uint32_t fat32_follow_path(const char *path);
static void fat32_format_8_3_name(const char *filename, char *fat_name);
static int fat32_compare_names(const char *fat_name, const char *filename);
static struct fat32_file* fat32_allocate_file_handle(void);
static void fat32_free_file_handle(struct fat32_file *file);
static int fat32_read_cluster(uint32_t cluster, void *buffer);
static int fat32_write_cluster(uint32_t cluster, const void *buffer);

int fat32_init(uint32_t drive_number) {
    current_drive = drive_number;
    
    /* Clear file system state */
    memset(&fs, 0, sizeof(fs));
    fs_mounted = 0;
    
    /* Initialize open files array */
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        fs.open_files[i].is_open = 0;
    }
    
    return FAT32_SUCCESS;
}

int fat32_mount(void) {
    if (fs_mounted) {
        return FAT32_SUCCESS;
    }
    
    /* Read boot sector */
    if (fat32_read_boot_sector() != FAT32_SUCCESS) {
        vga_writestring("Failed to read FAT32 boot sector\n");
        return FAT32_ERROR_IO;
    }
    
    /* Validate FAT32 signature */
    if (strncmp(fs.boot_sector.fs_type, "FAT32", 5) != 0) {
        vga_writestring("Not a FAT32 file system\n");
        return FAT32_ERROR_INVALID;
    }
    
    /* Calculate file system parameters */
    fs.fat_start_sector = fs.boot_sector.reserved_sectors;
    fs.data_start_sector = fs.fat_start_sector + 
                          (fs.boot_sector.num_fats * fs.boot_sector.sectors_per_fat_32);
    fs.cluster_size = fs.boot_sector.sectors_per_cluster * fs.boot_sector.bytes_per_sector;
    fs.sectors_per_cluster = fs.boot_sector.sectors_per_cluster;
    fs.root_cluster = fs.boot_sector.root_cluster;
    
    /* Calculate total clusters */
    uint32_t data_sectors = fs.boot_sector.total_sectors_32 - fs.data_start_sector;
    fs.total_clusters = data_sectors / fs.boot_sector.sectors_per_cluster;
    
    /* Load FAT table into memory */
    if (fat32_load_fat_table() != FAT32_SUCCESS) {
        vga_writestring("Failed to load FAT table\n");
        return FAT32_ERROR_IO;
    }
    
    fs_mounted = 1;
    
    vga_writestring("FAT32 file system mounted successfully\n");
    return FAT32_SUCCESS;
}

void fat32_unmount(void) {
    if (!fs_mounted) {
        return;
    }
    
    /* Close all open files */
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (fs.open_files[i].is_open) {
            fat32_close(&fs.open_files[i]);
        }
    }
    
    /* Flush FAT cache */
    fat32_flush_fat_cache();
    
    /* Free FAT cache */
    if (fs.fat_cache) {
        kfree(fs.fat_cache);
        fs.fat_cache = NULL;
    }
    
    fs_mounted = 0;
    vga_writestring("FAT32 file system unmounted\n");
}

static int fat32_read_boot_sector(void) {
    return disk_read_sectors(current_drive, 0, 1, &fs.boot_sector);
}

static int fat32_load_fat_table(void) {
    /* Allocate memory for FAT cache */
    size_t fat_size = fs.boot_sector.sectors_per_fat_32 * fs.boot_sector.bytes_per_sector;
    fs.fat_cache = (uint8_t*)kmalloc(fat_size);
    
    if (!fs.fat_cache) {
        return FAT32_ERROR_NO_SPACE;
    }
    
    /* Read first FAT table */
    int result = disk_read_sectors(current_drive, fs.fat_start_sector, 
                                  fs.boot_sector.sectors_per_fat_32, fs.fat_cache);
    
    if (result != 0) {
        kfree(fs.fat_cache);
        fs.fat_cache = NULL;
        return FAT32_ERROR_IO;
    }
    
    fs.fat_cache_dirty = 0;
    return FAT32_SUCCESS;
}

static uint32_t fat32_get_fat_entry(uint32_t cluster) {
    if (!fs.fat_cache || cluster >= fs.total_clusters) {
        return FAT32_BAD_CLUSTER;
    }
    
    uint32_t *fat_table = (uint32_t*)fs.fat_cache;
    return fat_table[cluster] & 0x0FFFFFFF; // Mask upper 4 bits
}

static int fat32_set_fat_entry(uint32_t cluster, uint32_t value) {
    if (!fs.fat_cache || cluster >= fs.total_clusters) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t *fat_table = (uint32_t*)fs.fat_cache;
    fat_table[cluster] = (fat_table[cluster] & 0xF0000000) | (value & 0x0FFFFFFF);
    fs.fat_cache_dirty = 1;
    
    return FAT32_SUCCESS;
}

uint32_t fat32_cluster_to_sector(uint32_t cluster) {
    if (cluster < 2) {
        return 0; // Invalid cluster
    }
    
    return fs.data_start_sector + ((cluster - 2) * fs.sectors_per_cluster);
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    uint32_t next = fat32_get_fat_entry(cluster);
    
    if (next >= FAT32_EOC) {
        return 0; // End of chain
    }
    
    return next;
}

static int fat32_read_cluster(uint32_t cluster, void *buffer) {
    uint32_t sector = fat32_cluster_to_sector(cluster);
    if (sector == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    return disk_read_sectors(current_drive, sector, fs.sectors_per_cluster, buffer);
}

static int fat32_write_cluster(uint32_t cluster, const void *buffer) {
    uint32_t sector = fat32_cluster_to_sector(cluster);
    if (sector == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    return disk_write_sectors(current_drive, sector, fs.sectors_per_cluster, buffer);
}

static struct fat32_dir_entry* fat32_find_file(uint32_t dir_cluster, const char *name) {
    static struct fat32_dir_entry entry;
    uint8_t *cluster_buffer = (uint8_t*)kmalloc(fs.cluster_size);
    
    if (!cluster_buffer) {
        return NULL;
    }
    
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster != 0 && current_cluster < FAT32_EOC) {
        if (fat32_read_cluster(current_cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return NULL;
        }
        
        struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buffer;
        uint32_t entries_per_cluster = fs.cluster_size / sizeof(struct fat32_dir_entry);
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0) {
                // End of directory
                kfree(cluster_buffer);
                return NULL;
            }
            
            if ((unsigned char)entries[i].name[0] == 0xE5) {
                // Deleted entry, skip
                continue;
            }
            
            if (entries[i].attributes == FAT32_ATTR_LFN) {
                // Long filename entry, skip for now
                continue;
            }
            
            if (fat32_compare_names(entries[i].name, name)) {
                memcpy(&entry, &entries[i], sizeof(struct fat32_dir_entry));
                kfree(cluster_buffer);
                return &entry;
            }
        }
        
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    
    kfree(cluster_buffer);
    return NULL;
}

static uint32_t fat32_follow_path(const char *path) {
    if (!path || path[0] != '/') {
        return 0; // Invalid path
    }
    
    if (strcmp(path, "/") == 0) {
        return fs.root_cluster;
    }
    
    uint32_t current_cluster = fs.root_cluster;
    char *path_copy = kstrdup(path + 1); // Skip leading slash
    char *token = path_copy;
    char *next_token;
    
    while (token && *token) {
        // Find next path separator
        next_token = token;
        while (*next_token && *next_token != '/') {
            next_token++;
        }
        
        if (*next_token == '/') {
            *next_token = '\0';
            next_token++;
        } else {
            next_token = NULL;
        }
        
        // Look for this component in current directory
        struct fat32_dir_entry *entry = fat32_find_file(current_cluster, token);
        if (!entry) {
            kfree(path_copy);
            return 0; // Not found
        }
        
        // Get cluster from directory entry
        current_cluster = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
        
        // If we have more path components, this must be a directory
        if (next_token && !(entry->attributes & FAT32_ATTR_DIRECTORY)) {
            kfree(path_copy);
            return 0; // Not a directory
        }
        
        token = next_token;
    }
    
    kfree(path_copy);
    return current_cluster;
}

static void fat32_format_8_3_name(const char *filename, char *fat_name) {
    int i, j;
    
    // Clear name buffer
    memset(fat_name, ' ', 11);
    
    // Find extension
    const char *ext = NULL;
    for (i = strlen(filename) - 1; i >= 0; i--) {
        if (filename[i] == '.') {
            ext = &filename[i + 1];
            break;
        }
    }
    
    // Copy name part (up to 8 characters)
    j = 0;
    for (i = 0; i < 8 && filename[i] && (ext == NULL || &filename[i] < ext - 1); i++) {
        if (filename[i] >= 'a' && filename[i] <= 'z') {
            fat_name[j++] = filename[i] - 32; // Convert to uppercase
        } else if (filename[i] != '.') {
            fat_name[j++] = filename[i];
        }
    }
    
    // Copy extension (up to 3 characters)
    if (ext) {
        j = 8;
        for (i = 0; i < 3 && ext[i]; i++) {
            if (ext[i] >= 'a' && ext[i] <= 'z') {
                fat_name[j++] = ext[i] - 32; // Convert to uppercase
            } else {
                fat_name[j++] = ext[i];
            }
        }
    }
}

static int fat32_compare_names(const char *fat_name, const char *filename) {
    char formatted_name[12];
    fat32_format_8_3_name(filename, formatted_name);
    return (strncmp(fat_name, formatted_name, 11) == 0);
}

static struct fat32_file* fat32_allocate_file_handle(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!fs.open_files[i].is_open) {
            memset(&fs.open_files[i], 0, sizeof(struct fat32_file));
            fs.open_files[i].is_open = 1;
            return &fs.open_files[i];
        }
    }
    return NULL; // No free handles
}

static void fat32_free_file_handle(struct fat32_file *file) {
    if (file) {
        file->is_open = 0;
    }
}

struct fat32_file* fat32_open(const char *path, const char *mode) {

    if (!fs_mounted) {
        return NULL;
    }
    
    uint32_t cluster = fat32_follow_path(path);
    if (cluster == 0) {
        return NULL; // File not found
    }
    
    // Extract filename from path
    const char *filename = path;
    const char *slash = path;
    while (*slash) {
        if (*slash == '/') {
            filename = slash + 1;
        }
        slash++;
    }
    
    // Get parent directory cluster for the file
    char *parent_path = kstrdup(path);
    char *last_slash = parent_path;
    char *search = parent_path;
    while (*search) {
        if (*search == '/') {
            last_slash = search;
        }
        search++;
    }
    
    uint32_t parent_cluster;
    if (last_slash == parent_path) {
        // Root directory
        parent_cluster = fs.root_cluster;
    } else {
        *last_slash = '\0';
        parent_cluster = fat32_follow_path(parent_path);
    }
    kfree(parent_path);
    
    // Find the directory entry
    struct fat32_dir_entry *entry = fat32_find_file(parent_cluster, filename);
    if (!entry) {
        return NULL;
    }
    
    // Allocate file handle
    struct fat32_file *file = fat32_allocate_file_handle();
    if (!file) {
        return NULL;
    }
    
    // Initialize file handle
    strcpy(file->name, filename);
    file->cluster = cluster;
    file->first_cluster = cluster;
    file->size = entry->file_size;
    file->position = 0;
    file->attributes = entry->attributes;
    file->is_directory = (entry->attributes & FAT32_ATTR_DIRECTORY) ? 1 : 0;
    file->dir_cluster = parent_cluster;
    
    return file;
}

int fat32_close(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    fat32_free_file_handle(file);
    return FAT32_SUCCESS;
}

size_t fat32_read(struct fat32_file *file, void *buffer, size_t size) {
    if (!file || !file->is_open || file->is_directory) {
        return 0;
    }
    
    if (file->position >= file->size) {
        return 0; // EOF
    }
    
    // Limit read size to remaining file data
    if (file->position + size > file->size) {
        size = file->size - file->position;
    }
    
    size_t bytes_read = 0;
    uint8_t *dest = (uint8_t*)buffer;
    uint8_t *cluster_buffer = (uint8_t*)kmalloc(fs.cluster_size);
    
    if (!cluster_buffer) {
        return 0;
    }
    
    uint32_t current_cluster = file->cluster;
    uint32_t cluster_offset = file->position % fs.cluster_size;
    
    // Skip to the correct cluster based on position
    uint32_t clusters_to_skip = file->position / fs.cluster_size;
    current_cluster = file->first_cluster;
    
    for (uint32_t i = 0; i < clusters_to_skip && current_cluster < FAT32_EOC; i++) {
        current_cluster = fat32_get_next_cluster(current_cluster);
    }
    
    while (bytes_read < size && current_cluster != 0 && current_cluster < FAT32_EOC) {
        if (fat32_read_cluster(current_cluster, cluster_buffer) != 0) {
            break;
        }
        
        uint32_t bytes_to_copy = fs.cluster_size - cluster_offset;
        if (bytes_to_copy > size - bytes_read) {
            bytes_to_copy = size - bytes_read;
        }
        
        memcpy(dest + bytes_read, cluster_buffer + cluster_offset, bytes_to_copy);
        bytes_read += bytes_to_copy;
        cluster_offset = 0; // Only first cluster has offset
        
        if (bytes_read < size) {
            current_cluster = fat32_get_next_cluster(current_cluster);
        }
    }
    
    file->position += bytes_read;
    file->cluster = current_cluster;
    
    kfree(cluster_buffer);
    return bytes_read;
}

struct fat32_file* fat32_opendir(const char *path) {
    if (!fs_mounted) {
        return NULL;
    }

    uint32_t cluster = fat32_follow_path(path);
    if (cluster == 0) {
        return NULL;
    }

    struct fat32_file *dir = fat32_allocate_file_handle();
    if (!dir) {
        return NULL;
    }

    if (strcmp(path, "/") == 0) {
        strcpy(dir->name, "/");
        dir->cluster = fs.root_cluster;
        dir->first_cluster = fs.root_cluster;
        dir->size = 0;
        dir->position = 0;
        dir->attributes = FAT32_ATTR_DIRECTORY;
        dir->is_directory = 1;
        dir->dir_cluster = fs.root_cluster;
        return dir;
    }

    const char *filename = path;
    const char *slash = path;
    while (*slash) {
        if (*slash == '/') {
            filename = slash + 1;
        }
        slash++;
    }

    char *parent_path = kstrdup(path);
    char *last_slash = parent_path;
    char *search = parent_path;
    while (*search) {
        if (*search == '/') {
            last_slash = search;
        }
        search++;
    }
    *last_slash = '\0';

    uint32_t parent_cluster = fat32_follow_path(parent_path);
    kfree(parent_path);

    struct fat32_dir_entry *entry = fat32_find_file(parent_cluster, filename);
    if (!entry || !(entry->attributes & FAT32_ATTR_DIRECTORY)) {
        fat32_free_file_handle(dir);
        return NULL;
    }

    strcpy(dir->name, filename);
    dir->cluster = cluster;
    dir->first_cluster = cluster;
    dir->size = entry->file_size;
    dir->position = 0;
    dir->attributes = entry->attributes;
    dir->is_directory = 1;
    dir->dir_cluster = parent_cluster;

    return dir;
}

struct fat32_dir_entry* fat32_readdir(struct fat32_file *dir) {
    if (!dir || !dir->is_open || !dir->is_directory) {
        return NULL;
    }

    uint8_t *cluster_buffer = (uint8_t*)kmalloc(fs.cluster_size);
    if (!cluster_buffer) {
        return NULL;
    }

    uint32_t entries_per_cluster = fs.cluster_size / sizeof(struct fat32_dir_entry);

    while (dir->cluster != 0 && dir->cluster < FAT32_EOC) {
        if (fat32_read_cluster(dir->cluster, cluster_buffer) != 0) {
            kfree(cluster_buffer);
            return NULL;
        }

        struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buffer;
        
        while (dir->position < entries_per_cluster) {
            struct fat32_dir_entry *entry = &entries[dir->position];
            dir->position++;

            if (entry->name[0] == 0) { // End of directory
                kfree(cluster_buffer);
                return NULL;
            }

            if ((uint8_t)entry->name[0] == 0xE5) { // Deleted
                continue;
            }

            if (entry->attributes == FAT32_ATTR_LFN) { // Skip LFN
                continue;
            }
            
            static struct fat32_dir_entry result;
            memcpy(&result, entry, sizeof(struct fat32_dir_entry));
            kfree(cluster_buffer);
            return &result;
        }

        dir->cluster = fat32_get_next_cluster(dir->cluster);
        dir->position = 0;
    }

    kfree(cluster_buffer);
    return NULL;
}

int fat32_closedir(struct fat32_file *dir) {
    if (!dir || !dir->is_open || !dir->is_directory) {
        return FAT32_ERROR_INVALID;
    }
    
    fat32_free_file_handle(dir);
    return FAT32_SUCCESS;
}