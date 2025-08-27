#include "fs/fat32.h"
#include "kernel.h"
#include "drivers/vga.h"
#include "cpu/heap.h"
#include "drivers/disk.h"

/* Single, consistent global state */
static struct {
    uint8_t initialized;
    struct disk_handle *disk;
    
    /* Boot sector data */
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
    uint32_t current_dir_cluster;
    
    /* Buffers */
    uint8_t *cluster_buffer;
    uint32_t *fat_cache;
    
    /* File handles */
    struct fat32_file files[FAT32_MAX_OPEN_FILES];
} g_fat32 = {0};

/* Helper function prototypes */
static int fat32_create_filesystem(void);
static int fat32_read_boot_sector(void);
static int fat32_load_fat_cache(void);
static uint32_t fat32_cluster_to_sector(uint32_t cluster);
static int fat32_find_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                                struct fat32_dir_entry *entry, int *entry_index);
static struct fat32_file* fat32_find_free_handle(void);
static int fat32_parse_mode(const char *mode);
static void fat32_close_all_files(void);

int fat32_init(void) {
    if (g_fat32.initialized) {
        vga_writestring("FAT32: Already initialized\n");
        return FAT32_SUCCESS;
    }
    
    /* Open disk handle */
    g_fat32.disk = disk_open(0);
    if (!g_fat32.disk) {
        vga_writestring("FAT32: Failed to open disk 0\n");
        return FAT32_ERROR_IO;
    }
    
    /* Clear file handles */
    memset(g_fat32.files, 0, sizeof(g_fat32.files));
    
    vga_writestring("FAT32: Initialized with disk driver\n");
    return FAT32_SUCCESS;
}

int fat32_mount(void) {
    if (g_fat32.initialized) {
        vga_writestring("FAT32: Already mounted\n");
        return FAT32_SUCCESS;
    }
    
    if (!g_fat32.disk) {
        vga_writestring("FAT32: No disk available\n");
        return fat32_init(); /* Try to initialize */
    }
    
    /* Try to read existing boot sector */
    int result = fat32_read_boot_sector();
    if (result != FAT32_SUCCESS) {
        vga_writestring("FAT32: No valid filesystem found, creating new one...\n");
        
        /* Create a new FAT32 filesystem */
        result = fat32_create_filesystem();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: Failed to create filesystem\n");
            return result;
        }
        
        /* Now try to read the boot sector again */
        result = fat32_read_boot_sector();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: Failed to read newly created boot sector\n");
            return result;
        }
    }
    
    /* Calculate filesystem parameters */
    g_fat32.fat_start_sector = g_fat32.reserved_sectors;
    g_fat32.data_start_sector = g_fat32.fat_start_sector + 
        (g_fat32.num_fats * g_fat32.sectors_per_fat);
    g_fat32.bytes_per_cluster = g_fat32.bytes_per_sector * g_fat32.sectors_per_cluster;
    g_fat32.current_dir_cluster = g_fat32.root_cluster;
    
    /* Allocate buffers */
    g_fat32.cluster_buffer = kmalloc(g_fat32.bytes_per_cluster);
    if (!g_fat32.cluster_buffer) {
        vga_writestring("FAT32: Failed to allocate cluster buffer\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Load FAT cache */
    result = fat32_load_fat_cache();
    if (result != FAT32_SUCCESS) {
        vga_writestring("FAT32: Failed to load FAT cache\n");
        kfree(g_fat32.cluster_buffer);
        return result;
    }
    
    g_fat32.initialized = 1;
    
    vga_writestring("FAT32: Filesystem mounted successfully\n");
    vga_writestring("  Bytes per sector: ");
    print_dec(g_fat32.bytes_per_sector);
    vga_writestring("\n  Sectors per cluster: ");
    print_dec(g_fat32.sectors_per_cluster);
    vga_writestring("\n  Root cluster: ");
    print_dec(g_fat32.root_cluster);
    vga_putchar('\n');
    
    return FAT32_SUCCESS;
}

void fat32_unmount(void) {
    if (!g_fat32.initialized) {
        return;
    }
    
    fat32_close_all_files();
    
    if (g_fat32.disk) {
        disk_flush_cache(g_fat32.disk);
    }
    
    if (g_fat32.fat_cache) {
        kfree(g_fat32.fat_cache);
        g_fat32.fat_cache = NULL;
    }
    
    if (g_fat32.cluster_buffer) {
        kfree(g_fat32.cluster_buffer);
        g_fat32.cluster_buffer = NULL;
    }
    
    g_fat32.initialized = 0;
    vga_writestring("FAT32: Filesystem unmounted\n");
}

/* Create a minimal FAT32 filesystem */
static int fat32_create_filesystem(void) {
    if (!g_fat32.disk) {
        return FAT32_ERROR_IO;
    }
    
    /* Get disk info */
    struct disk_info *info = disk_get_info(0);
    if (!info) {
        return FAT32_ERROR_IO;
    }
    
    /* Calculate filesystem parameters for a small disk */
    uint32_t total_sectors = info->sector_count;
    uint32_t reserved_sectors = 32;
    uint32_t sectors_per_fat = 256; /* Conservative estimate */
    uint32_t data_sectors = total_sectors - reserved_sectors - (2 * sectors_per_fat);
    uint32_t cluster_count = data_sectors / 8; /* 8 sectors per cluster */
    
    /* Create boot sector */
    struct fat32_boot_sector boot = {0};
    
    /* Jump instruction */
    boot.jump_code[0] = 0xEB;
    boot.jump_code[1] = 0x58;
    boot.jump_code[2] = 0x90;
    
    /* OEM name */
    memcpy(boot.oem_name, "NumOS   ", 8);
    
    /* BIOS Parameter Block */
    boot.bytes_per_sector = FAT32_SECTOR_SIZE;
    boot.sectors_per_cluster = 8;
    boot.reserved_sectors = reserved_sectors;
    boot.num_fats = 2;
    boot.root_entries = 0; /* FAT32 doesn't use this */
    boot.total_sectors_16 = 0; /* Use 32-bit field instead */
    boot.media_descriptor = 0xF8;
    boot.sectors_per_fat_16 = 0; /* Use 32-bit field instead */
    boot.sectors_per_track = 63;
    boot.num_heads = 255;
    boot.hidden_sectors = 0;
    boot.total_sectors_32 = total_sectors;
    
    /* FAT32 Extended BIOS Parameter Block */
    boot.sectors_per_fat_32 = sectors_per_fat;
    boot.ext_flags = 0;
    boot.filesystem_version = 0;
    boot.root_cluster = 2;
    boot.fsinfo_sector = 1;
    boot.backup_boot_sector = 6;
    boot.drive_number = 0x80;
    boot.boot_signature = 0x29;
    boot.volume_id = 0x12345678;
    memcpy(boot.volume_label, "NumOS FAT32", 11);
    memcpy(boot.filesystem_type, "FAT32   ", 8);
    boot.boot_sector_signature = 0xAA55;
    
    /* Write boot sector */
    if (disk_write_sector(g_fat32.disk, 0, &boot) != DISK_SUCCESS) {
        return FAT32_ERROR_IO;
    }
    
    /* Initialize FAT tables */
    uint32_t *fat_buffer = kmalloc(FAT32_SECTOR_SIZE);
    if (!fat_buffer) {
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Clear FAT sectors */
    memset(fat_buffer, 0, FAT32_SECTOR_SIZE);
    
    /* Set up first few FAT entries */
    fat_buffer[0] = 0x0FFFFF00 | boot.media_descriptor; /* Media descriptor in low byte */
    fat_buffer[1] = 0x0FFFFFFF; /* End of chain for FAT[1] */
    fat_buffer[2] = 0x0FFFFFFF; /* End of chain for root directory */
    
    /* Write first FAT sector */
    if (disk_write_sector(g_fat32.disk, reserved_sectors, fat_buffer) != DISK_SUCCESS) {
        kfree(fat_buffer);
        return FAT32_ERROR_IO;
    }
    
    /* Write backup FAT */
    if (disk_write_sector(g_fat32.disk, reserved_sectors + sectors_per_fat, fat_buffer) != DISK_SUCCESS) {
        kfree(fat_buffer);
        return FAT32_ERROR_IO;
    }
    
    kfree(fat_buffer);
    
    /* Initialize root directory */
    uint8_t *dir_buffer = kzalloc(boot.sectors_per_cluster * FAT32_SECTOR_SIZE);
    if (!dir_buffer) {
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Write empty root directory */
    uint32_t root_sector = reserved_sectors + (2 * sectors_per_fat);
    if (disk_write_sectors(g_fat32.disk, root_sector, boot.sectors_per_cluster, dir_buffer) != DISK_SUCCESS) {
        kfree(dir_buffer);
        return FAT32_ERROR_IO;
    }
    
    kfree(dir_buffer);
    
    /* Flush to disk */
    disk_flush_cache(g_fat32.disk);
    
    vga_writestring("FAT32: Created new filesystem\n");
    return FAT32_SUCCESS;
}

static int fat32_read_boot_sector(void) {
    struct fat32_boot_sector boot;
    
    if (disk_read_sector(g_fat32.disk, 0, &boot) != DISK_SUCCESS) {
        return FAT32_ERROR_IO;
    }
    
    /* Validate boot sector */
    if (boot.boot_sector_signature != 0xAA55) {
        return FAT32_ERROR_INVALID;
    }
    
    if (boot.bytes_per_sector != FAT32_SECTOR_SIZE) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Store important values */
    g_fat32.bytes_per_sector = boot.bytes_per_sector;
    g_fat32.sectors_per_cluster = boot.sectors_per_cluster;
    g_fat32.reserved_sectors = boot.reserved_sectors;
    g_fat32.num_fats = boot.num_fats;
    g_fat32.sectors_per_fat = boot.sectors_per_fat_32;
    g_fat32.root_cluster = boot.root_cluster;
    
    return FAT32_SUCCESS;
}

static int fat32_load_fat_cache(void) {
    /* Allocate FAT cache (just first sector for now) */
    g_fat32.fat_cache = kmalloc(FAT32_SECTOR_SIZE);
    if (!g_fat32.fat_cache) {
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Read first FAT sector */
    if (disk_read_sector(g_fat32.disk, g_fat32.fat_start_sector, g_fat32.fat_cache) != DISK_SUCCESS) {
        kfree(g_fat32.fat_cache);
        g_fat32.fat_cache = NULL;
        return FAT32_ERROR_IO;
    }
    
    return FAT32_SUCCESS;
}

static uint32_t fat32_cluster_to_sector(uint32_t cluster) {
    if (cluster < 2) {
        return 0;
    }
    return g_fat32.data_start_sector + ((cluster - 2) * g_fat32.sectors_per_cluster);
}

uint32_t fat32_get_next_cluster(uint32_t cluster) {
    if (!g_fat32.fat_cache || cluster < 2) {
        return FAT32_END_OF_CHAIN;
    }
    
    /* For simplicity, we only cache first sector of FAT */
    if (cluster < (FAT32_SECTOR_SIZE / 4)) {
        return g_fat32.fat_cache[cluster] & 0x0FFFFFFF;
    }
    
    return FAT32_END_OF_CHAIN;
}

uint32_t fat32_allocate_cluster(void) {
    if (!g_fat32.fat_cache) {
        return 0;
    }
    
    /* Simple allocation from cached sector */
    uint32_t entries = FAT32_SECTOR_SIZE / 4;
    for (uint32_t i = 2; i < entries; i++) {
        if ((g_fat32.fat_cache[i] & 0x0FFFFFFF) == FAT32_FREE_CLUSTER) {
            g_fat32.fat_cache[i] = FAT32_END_OF_CHAIN;
            
            /* Write back FAT sector */
            disk_write_sector(g_fat32.disk, g_fat32.fat_start_sector, g_fat32.fat_cache);
            disk_write_sector(g_fat32.disk, g_fat32.fat_start_sector + g_fat32.sectors_per_fat, g_fat32.fat_cache);
            
            return i;
        }
    }
    
    return 0;
}

int fat32_free_cluster_chain(uint32_t start_cluster) {
    if (!g_fat32.fat_cache || start_cluster < 2) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Simple implementation for cached clusters only */
    uint32_t entries = FAT32_SECTOR_SIZE / 4;
    if (start_cluster < entries) {
        g_fat32.fat_cache[start_cluster] = FAT32_FREE_CLUSTER;
        disk_write_sector(g_fat32.disk, g_fat32.fat_start_sector, g_fat32.fat_cache);
        disk_write_sector(g_fat32.disk, g_fat32.fat_start_sector + g_fat32.sectors_per_fat, g_fat32.fat_cache);
    }
    
    return FAT32_SUCCESS;
}

void fat32_list_files(void) {
    if (!g_fat32.initialized) {
        vga_writestring("FAT32: Filesystem not mounted\n");
        return;
    }
    
    vga_writestring("Directory listing:\n");
    
    /* Read root directory */
    uint32_t root_sector = fat32_cluster_to_sector(g_fat32.root_cluster);
    if (root_sector == 0) {
        vga_writestring("Invalid root directory\n");
        return;
    }
    
    if (disk_read_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                         g_fat32.cluster_buffer) != DISK_SUCCESS) {
        vga_writestring("Failed to read root directory\n");
        return;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    int entry_count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00) break; /* End of directory */
        if (entries[i].name[0] == 0xE5) continue; /* Deleted entry */
        if (entries[i].attributes & FAT32_ATTR_LONG_NAME) continue; /* LFN entry */
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) continue; /* Volume label */
        
        /* Format filename for display */
        char filename[13];
        int pos = 0;
        
        /* Name part */
        for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
            filename[pos++] = entries[i].name[j];
        }
        
        /* Extension part */
        if (entries[i].name[8] != ' ') {
            filename[pos++] = '.';
            for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) {
                filename[pos++] = entries[i].name[j];
            }
        }
        filename[pos] = '\0';
        
        vga_writestring("  ");
        vga_writestring(filename);
        
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) {
            vga_writestring(" <DIR>");
        } else {
            vga_writestring(" ");
            print_dec(entries[i].file_size);
            vga_writestring(" bytes");
        }
        vga_putchar('\n');
    }
}

int fat32_format_name(const char *filename, char *formatted_name) {
    if (!filename || !formatted_name) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Initialize with spaces */
    memset(formatted_name, ' ', 11);
    formatted_name[11] = '\0';
    
    const char *dot = strstr(filename, ".");
    int name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    
    /* Copy name part (max 8 characters) */
    for (int i = 0; i < name_len && i < 8; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32; /* Convert to uppercase */
        formatted_name[i] = c;
    }
    
    /* Copy extension part (max 3 characters) */
    if (dot) {
        for (int i = 0; i < 3 && dot[i + 1] != '\0'; i++) {
            char c = dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 32; /* Convert to uppercase */
            formatted_name[8 + i] = c;
        }
    }
    
    return FAT32_SUCCESS;
}

int fat32_exists(const char *filename) {
    if (!g_fat32.initialized || !filename) {
        return 0;
    }
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        return 0;
    }
    
    return fat32_find_dir_entry(g_fat32.current_dir_cluster, formatted_name, NULL, NULL) != 0;
}

static int fat32_find_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                               struct fat32_dir_entry *entry, int *entry_index) {
    uint32_t sector = fat32_cluster_to_sector(dir_cluster);
    if (sector == 0) {
        return 0;
    }
    
    if (disk_read_sectors(g_fat32.disk, sector, g_fat32.sectors_per_cluster, 
                         g_fat32.cluster_buffer) != DISK_SUCCESS) {
        return 0;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    int count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & (FAT32_ATTR_LONG_NAME | FAT32_ATTR_VOLUME_ID)) continue;
        
        if (memcmp(entries[i].name, formatted_name, 11) == 0) {
            if (entry) {
                memcpy(entry, &entries[i], sizeof(struct fat32_dir_entry));
            }
            if (entry_index) {
                *entry_index = i;
            }
            return 1;
        }
    }
    
    return 0;
}

static struct fat32_file* fat32_find_free_handle(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!g_fat32.files[i].is_open) {
            return &g_fat32.files[i];
        }
    }
    return NULL;
}

static int fat32_parse_mode(const char *mode) {
    int file_mode = 0;
    
    while (*mode) {
        switch (*mode) {
            case 'r': file_mode |= FAT32_MODE_READ; break;
            case 'w': file_mode |= FAT32_MODE_WRITE | FAT32_MODE_CREATE; break;
            case 'a': file_mode |= FAT32_MODE_APPEND | FAT32_MODE_CREATE; break;
            case '+': file_mode |= FAT32_MODE_READ | FAT32_MODE_WRITE; break;
        }
        mode++;
    }
    
    return file_mode ? file_mode : -1;
}

static void fat32_close_all_files(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (g_fat32.files[i].is_open) {
            fat32_fclose(&g_fat32.files[i]);
        }
    }
}

/* Stub implementations for file operations */
struct fat32_file* fat32_fopen(const char *filename, const char *mode) {
    vga_writestring("FAT32: fopen not fully implemented yet\n");
    return NULL;
}

int fat32_fclose(struct fat32_file *file) {
    if (file && file->is_open) {
        file->is_open = 0;
        return FAT32_SUCCESS;
    }
    return FAT32_ERROR_INVALID;
}

size_t fat32_fread(void *buffer, size_t size, size_t count, struct fat32_file *file) {
    (void)buffer; (void)size; (void)count; (void)file;
    return 0;
}

size_t fat32_fwrite(const void *buffer, size_t size, size_t count, struct fat32_file *file) {
    (void)buffer; (void)size; (void)count; (void)file;
    return 0;
}

int fat32_fseek(struct fat32_file *file, long offset, int whence) {
    (void)file; (void)offset; (void)whence;
    return FAT32_ERROR_INVALID;
}

long fat32_ftell(struct fat32_file *file) {
    (void)file;
    return -1;
}

int fat32_feof(struct fat32_file *file) {
    (void)file;
    return 1;
}

int fat32_fflush(struct fat32_file *file) {
    (void)file;
    return FAT32_SUCCESS;
}

uint32_t fat32_get_file_size(const char *filename) {
    (void)filename;
    return 0;
}

void fat32_print_file_info(const char *filename) {
    vga_writestring("File info not implemented for: ");
    vga_writestring(filename);
    vga_putchar('\n');
}

void fat32_print_boot_sector(void) {
    if (!g_fat32.initialized) {
        vga_writestring("FAT32: Not initialized\n");
        return;
    }
    
    vga_writestring("FAT32 Boot Sector Info:\n");
    vga_writestring("  Bytes per sector: ");
    print_dec(g_fat32.bytes_per_sector);
    vga_writestring("\n  Sectors per cluster: ");
    print_dec(g_fat32.sectors_per_cluster);
    vga_writestring("\n  Reserved sectors: ");
    print_dec(g_fat32.reserved_sectors);
    vga_writestring("\n  Number of FATs: ");
    print_dec(g_fat32.num_fats);
    vga_writestring("\n  Sectors per FAT: ");
    print_dec(g_fat32.sectors_per_fat);
    vga_writestring("\n  Root cluster: ");
    print_dec(g_fat32.root_cluster);
    vga_putchar('\n');
}

void fat32_print_fs_info(void) {
    if (!g_fat32.initialized) {
        vga_writestring("FAT32: Not initialized\n");
        return;
    }
    
    vga_writestring("FAT32 Filesystem Info:\n");
    vga_writestring("  Initialized: Yes\n");
    vga_writestring("  Current dir cluster: ");
    print_dec(g_fat32.current_dir_cluster);
    vga_putchar('\n');
}

/* Disk I/O wrapper functions */
int fat32_read_sector(uint32_t sector, void *buffer) {
    if (!g_fat32.disk) return FAT32_ERROR_IO;
    return (disk_read_sector(g_fat32.disk, sector, buffer) == DISK_SUCCESS) ? 
           FAT32_SUCCESS : FAT32_ERROR_IO;
}

int fat32_write_sector(uint32_t sector, const void *buffer) {
    if (!g_fat32.disk) return FAT32_ERROR_IO;
    return (disk_write_sector(g_fat32.disk, sector, buffer) == DISK_SUCCESS) ? 
           FAT32_SUCCESS : FAT32_ERROR_IO;
}

int fat32_read_sectors(uint32_t start_sector, uint32_t count, void *buffer) {
    if (!g_fat32.disk) return FAT32_ERROR_IO;
    return (disk_read_sectors(g_fat32.disk, start_sector, count, buffer) == DISK_SUCCESS) ? 
           FAT32_SUCCESS : FAT32_ERROR_IO;
}

int fat32_write_sectors(uint32_t start_sector, uint32_t count, const void *buffer) {
    if (!g_fat32.disk) return FAT32_ERROR_IO;
    return (disk_write_sectors(g_fat32.disk, start_sector, count, buffer) == DISK_SUCCESS) ? 
           FAT32_SUCCESS : FAT32_ERROR_IO;
}