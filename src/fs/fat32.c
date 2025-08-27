#include "fat32.h"
#include "kernel.h"
#include "vga.h"
#include "heap.h"

/* Global FAT32 file system state */
static struct fat32_fs g_fat32_fs = {0};
static struct fat32_file g_open_files[FAT32_MAX_OPEN_FILES] = {0};
static int g_current_dir_cluster = 0;

/* Simple disk simulation for demonstration - in real implementation, 
   this would interface with actual disk drivers */
static uint8_t *g_disk_image = NULL;
static const uint32_t DISK_SIZE = 32 * 1024 * 1024; // 32MB simulated disk

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

int fat32_init(void) {
    if (g_fat32_fs.initialized) {
        vga_writestring("FAT32: Already initialized\n");
        return FAT32_SUCCESS;
    }
    
    /* Allocate simulated disk */
    g_disk_image = (uint8_t*)kmalloc(DISK_SIZE);
    if (!g_disk_image) {
        vga_writestring("FAT32: Failed to allocate disk image\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Initialize with a basic FAT32 structure */
    memset(g_disk_image, 0, DISK_SIZE);
    
    /* Create a minimal FAT32 boot sector */
    struct fat32_boot_sector *boot = (struct fat32_boot_sector*)g_disk_image;
    boot->jump_code[0] = 0xEB;
    boot->jump_code[1] = 0x58;
    boot->jump_code[2] = 0x90;
    memcpy(boot->oem_name, "NumOS   ", 8);
    boot->bytes_per_sector = FAT32_SECTOR_SIZE;
    boot->sectors_per_cluster = 8; // 4KB clusters
    boot->reserved_sectors = 32;
    boot->num_fats = 2;
    boot->root_entries = 0; // FAT32 uses clusters for root
    boot->total_sectors_16 = 0;
    boot->media_descriptor = 0xF8;
    boot->sectors_per_fat_16 = 0;
    boot->sectors_per_track = 63;
    boot->num_heads = 255;
    boot->hidden_sectors = 0;
    boot->total_sectors_32 = DISK_SIZE / FAT32_SECTOR_SIZE;
    boot->sectors_per_fat_32 = 256;
    boot->ext_flags = 0;
    boot->filesystem_version = 0;
    boot->root_cluster = 2; // Root directory starts at cluster 2
    boot->fsinfo_sector = 1;
    boot->backup_boot_sector = 6;
    boot->drive_number = 0x80;
    boot->boot_signature = 0x29;
    boot->volume_id = 0x12345678;
    memcpy(boot->volume_label, "NumOS FAT32", 11);
    memcpy(boot->filesystem_type, "FAT32   ", 8);
    boot->boot_sector_signature = 0xAA55;
    
    /* Initialize FAT table - mark first few clusters */
    uint32_t *fat = (uint32_t*)(g_disk_image + (boot->reserved_sectors * FAT32_SECTOR_SIZE));
    fat[0] = 0x0FFFFF00 | boot->media_descriptor;
    fat[1] = 0x0FFFFFFF;
    fat[2] = 0x0FFFFFFF; // Root directory end marker
    
    /* Clear open files array */
    memset(g_open_files, 0, sizeof(g_open_files));
    
    vga_writestring("FAT32: File system initialized\n");
    return FAT32_SUCCESS;
}

int fat32_mount(void) {
    if (g_fat32_fs.initialized) {
        vga_writestring("FAT32: Already mounted\n");
        return FAT32_SUCCESS;
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
    
    /* Free allocated memory */
    if (g_fat32_fs.fat_cache) {
        kfree(g_fat32_fs.fat_cache);
        g_fat32_fs.fat_cache = NULL;
    }
    
    if (g_fat32_fs.cluster_buffer) {
        kfree(g_fat32_fs.cluster_buffer);
        g_fat32_fs.cluster_buffer = NULL;
    }
    
    if (g_disk_image) {
        kfree(g_disk_image);
        g_disk_image = NULL;
    }
    
    g_fat32_fs.initialized = 0;
    vga_writestring("FAT32: File system unmounted\n");
}
// Add this helper function before fat32_fopen
static int fat32_create_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                                  uint32_t first_cluster, uint32_t file_size, uint8_t attributes) {
    // Read the directory cluster
    if (fat32_read_cluster(dir_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
        return FAT32_ERROR_IO;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32_fs.cluster_buffer;
    int entry_count = g_fat32_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    // Find an empty directory entry
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            // Found empty slot, create entry
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
            
            // Mark end of directory if this was the last entry
            if (i + 1 < entry_count && entries[i + 1].name[0] != 0xE5) {
                entries[i + 1].name[0] = 0x00;
            }
            
            // Write the directory back
            return fat32_write_cluster(dir_cluster, g_fat32_fs.cluster_buffer);
        }
    }
    
    return FAT32_ERROR_NO_SPACE; // Directory full
}

// Add this helper function to update existing directory entries
static int fat32_update_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                                  uint32_t file_size) {
    // Read the directory cluster
    if (fat32_read_cluster(dir_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
        return FAT32_ERROR_IO;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32_fs.cluster_buffer;
    int entry_count = g_fat32_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    // Find the directory entry
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00) {
            break; // End of directory
        }
        
        if (entries[i].name[0] == 0xE5) {
            continue; // Deleted entry
        }
        
        if (entries[i].attributes & (FAT32_ATTR_LONG_NAME | FAT32_ATTR_VOLUME_ID)) {
            continue; // Skip long name and volume ID entries
        }
        
        // Compare filenames
        if (memcmp(entries[i].name, formatted_name, 11) == 0) {
            // Update file size
            entries[i].file_size = file_size;
            // Write the directory back
            return fat32_write_cluster(dir_cluster, g_fat32_fs.cluster_buffer);
        }
    }
    
    return FAT32_ERROR_NOT_FOUND;
}

// Replace the fat32_fopen function with this fixed version:
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

// Also update the fat32_fclose function to update directory entry on close:
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
    
    /* Mark file as closed */
    file->is_open = 0;
    memset(file, 0, sizeof(struct fat32_file));
    
    return FAT32_SUCCESS;
}

size_t fat32_fread(void *buffer, size_t size, size_t count, struct fat32_file *file) {
    if (!file || !file->is_open || !buffer || size == 0 || count == 0) {
        return 0;
    }
    
    if (!(file->mode & FAT32_MODE_READ)) {
        vga_writestring("FAT32: File not open for reading\n");
        return 0;
    }
    
    size_t total_bytes = size * count;
    size_t bytes_read = 0;
    uint8_t *buf = (uint8_t*)buffer;
    
    while (bytes_read < total_bytes && file->position < file->file_size) {
        /* Read current cluster if needed */
        if (fat32_read_cluster(file->current_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
            break;
        }
        
        /* Calculate position within cluster */
        uint32_t cluster_offset = file->position % g_fat32_fs.bytes_per_cluster;
        uint32_t bytes_in_cluster = g_fat32_fs.bytes_per_cluster - cluster_offset;
        uint32_t bytes_remaining_in_file = file->file_size - file->position;
        uint32_t bytes_to_read = total_bytes - bytes_read;
        
        if (bytes_to_read > bytes_in_cluster) {
            bytes_to_read = bytes_in_cluster;
        }
        if (bytes_to_read > bytes_remaining_in_file) {
            bytes_to_read = bytes_remaining_in_file;
        }
        
        /* Copy data from cluster buffer */
        memcpy(buf + bytes_read, g_fat32_fs.cluster_buffer + cluster_offset, bytes_to_read);
        
        bytes_read += bytes_to_read;
        file->position += bytes_to_read;
        
        /* Move to next cluster if we've reached the end of current cluster */
        if ((file->position % g_fat32_fs.bytes_per_cluster) == 0 && 
            file->position < file->file_size) {
            uint32_t next_cluster = fat32_get_next_cluster(file->current_cluster);
            if (next_cluster >= FAT32_END_OF_CHAIN) {
                break; /* End of chain */
            }
            file->current_cluster = next_cluster;
        }
    }
    
    return bytes_read / size; /* Return number of complete elements read */
}

size_t fat32_fwrite(const void *buffer, size_t size, size_t count, struct fat32_file *file) {
    if (!file || !file->is_open || !buffer || size == 0 || count == 0) {
        return 0;
    }
    
    if (!(file->mode & FAT32_MODE_WRITE)) {
        vga_writestring("FAT32: File not open for writing\n");
        return 0;
    }
    
    size_t total_bytes = size * count;
    size_t bytes_written = 0;
    const uint8_t *buf = (const uint8_t*)buffer;
    
    while (bytes_written < total_bytes) {
        /* Ensure we have a valid cluster */
        if (file->current_cluster == 0 || file->current_cluster >= FAT32_END_OF_CHAIN) {
            file->current_cluster = fat32_allocate_cluster();
            if (file->current_cluster == 0) {
                break; /* No more space */
            }
        }
        
        /* Read current cluster */
        if (fat32_read_cluster(file->current_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
            break;
        }
        
        /* Calculate position within cluster */
        uint32_t cluster_offset = file->position % g_fat32_fs.bytes_per_cluster;
        uint32_t bytes_in_cluster = g_fat32_fs.bytes_per_cluster - cluster_offset;
        uint32_t bytes_to_write = total_bytes - bytes_written;
        
        if (bytes_to_write > bytes_in_cluster) {
            bytes_to_write = bytes_in_cluster;
        }
        
        /* Copy data to cluster buffer */
        memcpy(g_fat32_fs.cluster_buffer + cluster_offset, buf + bytes_written, bytes_to_write);
        
        /* Write cluster back to disk */
        if (fat32_write_cluster(file->current_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
            break;
        }
        
        bytes_written += bytes_to_write;
        file->position += bytes_to_write;
        
        /* Update file size if we've extended the file */
        if (file->position > file->file_size) {
            file->file_size = file->position;
        }
        
        /* Move to next cluster if we've reached the end of current cluster */
        if ((file->position % g_fat32_fs.bytes_per_cluster) == 0 && 
            bytes_written < total_bytes) {
            uint32_t next_cluster = fat32_get_next_cluster(file->current_cluster);
            if (next_cluster >= FAT32_END_OF_CHAIN) {
                /* Need to allocate new cluster */
                next_cluster = fat32_allocate_cluster();
                if (next_cluster == 0) {
                    break; /* No more space */
                }
                /* Link current cluster to new cluster */
                uint32_t *fat = (uint32_t*)g_fat32_fs.fat_cache;
                fat[file->current_cluster] = next_cluster;
            }
            file->current_cluster = next_cluster;
        }
    }
    
    return bytes_written / size; /* Return number of complete elements written */
}

int fat32_fseek(struct fat32_file *file, long offset, int whence) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t new_position;
    
    switch (whence) {
        case FAT32_SEEK_SET:
            new_position = offset;
            break;
        case FAT32_SEEK_CUR:
            new_position = file->position + offset;
            break;
        case FAT32_SEEK_END:
            new_position = file->file_size + offset;
            break;
        default:
            return FAT32_ERROR_INVALID;
    }
    
    if (new_position > file->file_size) {
        new_position = file->file_size;
    }
    
    file->position = new_position;
    
    /* Update current cluster */
    if (file->position == 0) {
        file->current_cluster = file->first_cluster;
    } else {
        /* Walk cluster chain to find the cluster containing the new position */
        uint32_t cluster_num = file->position / g_fat32_fs.bytes_per_cluster;
        file->current_cluster = file->first_cluster;
        
        for (uint32_t i = 0; i < cluster_num; i++) {
            uint32_t next_cluster = fat32_get_next_cluster(file->current_cluster);
            if (next_cluster >= FAT32_END_OF_CHAIN) {
                break;
            }
            file->current_cluster = next_cluster;
        }
    }
    
    return FAT32_SUCCESS;
}

long fat32_ftell(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return -1;
    }
    
    return (long)file->position;
}

int fat32_feof(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return 1;
    }
    
    return file->position >= file->file_size;
}

int fat32_fflush(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    /* TODO: Implement flushing of cached data and directory entry updates */
    return FAT32_SUCCESS;
}

void fat32_list_files(void) {
    if (!g_fat32_fs.initialized) {
        vga_writestring("FAT32: File system not mounted\n");
        return;
    }
    
    vga_writestring("Directory listing:\n");
    vga_writestring("Name        Size      Attr Date       Time\n");
    vga_writestring("----------  --------  ---- ---------- --------\n");
    
    /* Read root directory */
    if (fat32_read_cluster(g_current_dir_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
        vga_writestring("Failed to read directory\n");
        return;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32_fs.cluster_buffer;
    int entry_count = g_fat32_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00) {
            break; /* End of directory */
        }
        
        if (entries[i].name[0] == 0xE5) {
            continue; /* Deleted entry */
        }
        
        if (entries[i].attributes & FAT32_ATTR_LONG_NAME) {
            continue; /* Long filename entry */
        }
        
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) {
            continue; /* Volume ID */
        }
        
        /* Format and display filename */
        char filename[13];
        int pos = 0;
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
            filename[pos++] = entries[i].name[j];
        }
        if (entries[i].name[8] != ' ') {
            filename[pos++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) {
                filename[pos++] = entries[i].name[j];
            }
        }
        filename[pos] = '\0';
        
        /* Print filename (padded to 10 chars) */
        vga_writestring(filename);
        for (int j = pos; j < 11; j++) {
            vga_putchar(' ');
        }
        
        /* Print file size */
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) {
            vga_writestring("    <DIR>  ");
        } else {
            print_dec(entries[i].file_size);
            vga_writestring("  ");
        }
        
        /* Print attributes */
        vga_putchar((entries[i].attributes & FAT32_ATTR_READ_ONLY) ? 'R' : '-');
        vga_putchar((entries[i].attributes & FAT32_ATTR_HIDDEN) ? 'H' : '-');
        vga_putchar((entries[i].attributes & FAT32_ATTR_SYSTEM) ? 'S' : '-');
        vga_putchar((entries[i].attributes & FAT32_ATTR_ARCHIVE) ? 'A' : '-');
        
        vga_putchar('\n');
    }
}

/* Helper function implementations */
static int fat32_read_boot_sector(void) {
    return fat32_read_sector(0, &g_fat32_fs.boot_sector);
}

static int fat32_read_fat_cache(void) {
    size_t fat_size = g_fat32_fs.sectors_per_fat * FAT32_SECTOR_SIZE;
    g_fat32_fs.fat_cache = (uint8_t*)kmalloc(fat_size);
    if (!g_fat32_fs.fat_cache) {
        return FAT32_ERROR_NO_MEMORY;
    }
    
    return fat32_read_sectors(g_fat32_fs.fat_start_sector, g_fat32_fs.sectors_per_fat, 
                             g_fat32_fs.fat_cache);
}

static uint32_t fat32_cluster_to_sector(uint32_t cluster) {
    if (cluster < 2) {
        return 0; /* Invalid cluster */
    }
    
    return g_fat32_fs.data_start_sector + 
           ((cluster - 2) * g_fat32_fs.boot_sector.sectors_per_cluster);
}

static int fat32_read_cluster(uint32_t cluster, void *buffer) {
    uint32_t sector = fat32_cluster_to_sector(cluster);
    if (sector == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    return fat32_read_sectors(sector, g_fat32_fs.boot_sector.sectors_per_cluster, buffer);
}

static int fat32_write_cluster(uint32_t cluster, const void *buffer) {
    uint32_t sector = fat32_cluster_to_sector(cluster);
    if (sector == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    return fat32_write_sectors(sector, g_fat32_fs.boot_sector.sectors_per_cluster, buffer);
}

static struct fat32_file* fat32_find_free_handle(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!g_open_files[i].is_open) {
            return &g_open_files[i];
        }
    }
    return NULL;
}

static int fat32_find_file_in_dir(uint32_t dir_cluster, const char *filename, 
                                  struct fat32_dir_entry *entry) {
    if (fat32_read_cluster(dir_cluster, g_fat32_fs.cluster_buffer) != FAT32_SUCCESS) {
        return 0;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32_fs.cluster_buffer;
    int entry_count = g_fat32_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00) {
            break; /* End of directory */
        }
        
        if (entries[i].name[0] == 0xE5) {
            continue; /* Deleted entry */
        }
        
        if (entries[i].attributes & (FAT32_ATTR_LONG_NAME | FAT32_ATTR_VOLUME_ID)) {
            continue; /* Skip long name and volume ID entries */
        }
        
        /* Compare filenames */
        if (memcmp(entries[i].name, filename, 11) == 0) {
            if (entry) {
                memcpy(entry, &entries[i], sizeof(struct fat32_dir_entry));
            }
            return 1; /* Found */
        }
    }
    
    return 0; /* Not found */
}

static int fat32_parse_mode(const char *mode) {
    int file_mode = 0;
    
    while (*mode) {
        switch (*mode) {
            case 'r':
                file_mode |= FAT32_MODE_READ;
                break;
            case 'w':
                file_mode |= FAT32_MODE_WRITE | FAT32_MODE_CREATE;
                break;
            case 'a':
                file_mode |= FAT32_MODE_WRITE | FAT32_MODE_APPEND | FAT32_MODE_CREATE;
                break;
            case '+':
                file_mode |= FAT32_MODE_READ | FAT32_MODE_WRITE;
                break;
            default:
                /* Ignore unknown mode characters */
                break;
        }
        mode++;
    }
    
    return file_mode;
}

static void fat32_close_all_files(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (g_open_files[i].is_open) {
            fat32_fclose(&g_open_files[i]);
        }
    }
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    if (!g_fat32_fs.fat_cache || cluster < 2) {
        return FAT32_END_OF_CHAIN;
    }
    
    uint32_t *fat = (uint32_t*)g_fat32_fs.fat_cache;
    uint32_t next_cluster = fat[cluster] & 0x0FFFFFFF;
    
    return next_cluster;
}

uint32_t fat32_allocate_cluster(void) {
    if (!g_fat32_fs.fat_cache) {
        return 0;
    }
    
    uint32_t *fat = (uint32_t*)g_fat32_fs.fat_cache;
    uint32_t total_clusters = (g_fat32_fs.boot_sector.total_sectors_32 - 
                              g_fat32_fs.data_start_sector) / 
                              g_fat32_fs.boot_sector.sectors_per_cluster;
    
    /* Search for free cluster starting from cluster 2 */
    for (uint32_t i = 2; i < total_clusters + 2; i++) {
        if ((fat[i] & 0x0FFFFFFF) == FAT32_FREE_CLUSTER) {
            fat[i] = FAT32_END_OF_CHAIN;
            return i;
        }
    }
    
    return 0; /* No free clusters */
}

int fat32_free_cluster_chain(uint32_t start_cluster) {
    if (!g_fat32_fs.fat_cache || start_cluster < 2) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t *fat = (uint32_t*)g_fat32_fs.fat_cache;
    uint32_t current_cluster = start_cluster;
    
    while (current_cluster < FAT32_END_OF_CHAIN) {
        uint32_t next_cluster = fat[current_cluster] & 0x0FFFFFFF;
        fat[current_cluster] = FAT32_FREE_CLUSTER;
        current_cluster = next_cluster;
    }
    
    return FAT32_SUCCESS;
}

int fat32_format_name(const char *filename, char *formatted_name) {
    if (!filename || !formatted_name) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Initialize formatted name with spaces */
    memset(formatted_name, ' ', 11);
    formatted_name[11] = '\0';
    
    int name_pos = 0;
    int ext_pos = 8;
    int in_extension = 0;
    
    while (*filename && name_pos < 8) {
        if (*filename == '.') {
            in_extension = 1;
            filename++;
            break;
        }
        
        if (*filename >= 'a' && *filename <= 'z') {
            formatted_name[name_pos++] = *filename - 32; /* Convert to uppercase */
        } else if ((*filename >= 'A' && *filename <= 'Z') || 
                   (*filename >= '0' && *filename <= '9') ||
                   *filename == '_' || *filename == '-') {
            formatted_name[name_pos++] = *filename;
        } else {
            return FAT32_ERROR_INVALID; /* Invalid character */
        }
        
        filename++;
    }
    
    /* Skip to extension if we found a dot */
    if (in_extension) {
        while (*filename && ext_pos < 11) {
            if (*filename >= 'a' && *filename <= 'z') {
                formatted_name[ext_pos++] = *filename - 32; /* Convert to uppercase */
            } else if ((*filename >= 'A' && *filename <= 'Z') || 
                       (*filename >= '0' && *filename <= '9') ||
                       *filename == '_' || *filename == '-') {
                formatted_name[ext_pos++] = *filename;
            } else {
                return FAT32_ERROR_INVALID; /* Invalid character */
            }
            
            filename++;
        }
    }
    
    return FAT32_SUCCESS;
}

int fat32_exists(const char *filename) {
    if (!g_fat32_fs.initialized || !filename) {
        return 0;
    }
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        return 0;
    }
    
    return fat32_find_file_in_dir(g_current_dir_cluster, formatted_name, NULL);
}

uint32_t fat32_get_file_size(const char *filename) {
    if (!filename) {
        return 0;
    }
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        return 0;
    }
    
    struct fat32_dir_entry entry;
    if (fat32_find_file_in_dir(g_current_dir_cluster, formatted_name, &entry)) {
        return entry.file_size;
    }
    
    return 0;
}

void fat32_print_file_info(const char *filename) {
    if (!filename) {
        return;
    }
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        vga_writestring("Invalid filename\n");
        return;
    }
    
    struct fat32_dir_entry entry;
    if (!fat32_find_file_in_dir(g_current_dir_cluster, formatted_name, &entry)) {
        vga_writestring("File not found\n");
        return;
    }
    
    vga_writestring("File Information:\n");
    vga_writestring("  Name: ");
    vga_writestring(filename);
    vga_writestring("\n  Size: ");
    print_dec(entry.file_size);
    vga_writestring(" bytes\n  First cluster: ");
    print_dec(((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low);
    vga_writestring("\n  Attributes: ");
    
    if (entry.attributes & FAT32_ATTR_READ_ONLY) vga_writestring("Read-Only ");
    if (entry.attributes & FAT32_ATTR_HIDDEN) vga_writestring("Hidden ");
    if (entry.attributes & FAT32_ATTR_SYSTEM) vga_writestring("System ");
    if (entry.attributes & FAT32_ATTR_DIRECTORY) vga_writestring("Directory ");
    if (entry.attributes & FAT32_ATTR_ARCHIVE) vga_writestring("Archive ");
    
    vga_putchar('\n');
}

void fat32_print_boot_sector(void) {
    if (!g_fat32_fs.initialized) {
        vga_writestring("FAT32: File system not mounted\n");
        return;
    }
    
    struct fat32_boot_sector *bs = &g_fat32_fs.boot_sector;
    
    vga_writestring("FAT32 Boot Sector Information:\n");
    vga_writestring("  OEM Name: ");
    for (int i = 0; i < 8; i++) {
        vga_putchar(bs->oem_name[i]);
    }
    vga_writestring("\n  Bytes per sector: ");
    print_dec(bs->bytes_per_sector);
    vga_writestring("\n  Sectors per cluster: ");
    print_dec(bs->sectors_per_cluster);
    vga_writestring("\n  Reserved sectors: ");
    print_dec(bs->reserved_sectors);
    vga_writestring("\n  Number of FATs: ");
    print_dec(bs->num_fats);
    vga_writestring("\n  Total sectors: ");
    print_dec(bs->total_sectors_32);
    vga_writestring("\n  Sectors per FAT: ");
    print_dec(bs->sectors_per_fat_32);
    vga_writestring("\n  Root cluster: ");
    print_dec(bs->root_cluster);
    vga_writestring("\n  Volume ID: ");
    print_hex(bs->volume_id);
    vga_writestring("\n  Volume Label: ");
    for (int i = 0; i < 11; i++) {
        vga_putchar(bs->volume_label[i]);
    }
    vga_putchar('\n');
}

void fat32_print_fs_info(void) {
    if (!g_fat32_fs.initialized) {
        vga_writestring("FAT32: File system not mounted\n");
        return;
    }
    
    vga_writestring("FAT32 File System Information:\n");
    vga_writestring("  FAT start sector: ");
    print_dec(g_fat32_fs.fat_start_sector);
    vga_writestring("\n  Data start sector: ");
    print_dec(g_fat32_fs.data_start_sector);
    vga_writestring("\n  Root directory cluster: ");
    print_dec(g_fat32_fs.root_dir_cluster);
    vga_writestring("\n  Bytes per cluster: ");
    print_dec(g_fat32_fs.bytes_per_cluster);
    vga_writestring("\n  Open files: ");
    
    int open_count = 0;
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (g_open_files[i].is_open) {
            open_count++;
        }
    }
    print_dec(open_count);
    vga_writestring("/");
    print_dec(FAT32_MAX_OPEN_FILES);
    vga_putchar('\n');
}

/* Low-level disk I/O implementations using simulated disk */
int fat32_read_sector(uint32_t sector, void *buffer) {
    if (!g_disk_image || !buffer) {
        return FAT32_ERROR_IO;
    }
    
    uint32_t offset = sector * FAT32_SECTOR_SIZE;
    if (offset + FAT32_SECTOR_SIZE > DISK_SIZE) {
        return FAT32_ERROR_IO;
    }
    
    memcpy(buffer, g_disk_image + offset, FAT32_SECTOR_SIZE);
    return FAT32_SUCCESS;
}

int fat32_write_sector(uint32_t sector, const void *buffer) {
    if (!g_disk_image || !buffer) {
        return FAT32_ERROR_IO;
    }
    
    uint32_t offset = sector * FAT32_SECTOR_SIZE;
    if (offset + FAT32_SECTOR_SIZE > DISK_SIZE) {
        return FAT32_ERROR_IO;
    }
    
    memcpy(g_disk_image + offset, buffer, FAT32_SECTOR_SIZE);
    return FAT32_SUCCESS;
}

int fat32_read_sectors(uint32_t start_sector, uint32_t count, void *buffer) {
    if (!g_disk_image || !buffer || count == 0) {
        return FAT32_ERROR_IO;
    }
    
    uint32_t offset = start_sector * FAT32_SECTOR_SIZE;
    uint32_t size = count * FAT32_SECTOR_SIZE;
    
    if (offset + size > DISK_SIZE) {
        return FAT32_ERROR_IO;
    }
    
    memcpy(buffer, g_disk_image + offset, size);
    return FAT32_SUCCESS;
}

int fat32_write_sectors(uint32_t start_sector, uint32_t count, const void *buffer) {
    if (!g_disk_image || !buffer || count == 0) {
        return FAT32_ERROR_IO;
    }
    
    uint32_t offset = start_sector * FAT32_SECTOR_SIZE;
    uint32_t size = count * FAT32_SECTOR_SIZE;
    
    if (offset + size > DISK_SIZE) {
        return FAT32_ERROR_IO;
    }
    
    memcpy(g_disk_image + offset, buffer, size);
    return FAT32_SUCCESS;
}