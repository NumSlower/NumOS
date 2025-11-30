#include "fs/fat32.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/heap.h"
#include "drivers/disk.h"

/* Single, consistent global state */
static struct {
    uint8_t initialized;
    uint8_t mounted;
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
    uint32_t fat_cache_sectors;
    
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
    vga_writestring("FAT32: === Starting initialization ===\n");
    
    if (g_fat32.initialized) {
        vga_writestring("FAT32: Already initialized\n");
        return FAT32_SUCCESS;
    }
    
    /* Initialize all values to known state */
    memset(&g_fat32, 0, sizeof(g_fat32));
    
    /* Check if heap is available */
    vga_writestring("FAT32: Testing memory allocation...\n");
    void *test_ptr = kmalloc(1024);
    if (!test_ptr) {
        vga_writestring("FAT32: ERROR - Memory allocation failed!\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    kfree(test_ptr);
    vga_writestring("FAT32: Memory allocation test passed\n");
    
    /* Initialize disk subsystem */
    vga_writestring("FAT32: Initializing disk subsystem...\n");
    int disk_result = disk_init();
    if (disk_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Disk initialization failed with code: ");
        print_dec(disk_result);
        vga_putchar('\n');
        return FAT32_ERROR_IO;
    }
    vga_writestring("FAT32: Disk subsystem initialized\n");
    
    /* Wait for disk to be ready */
    vga_writestring("FAT32: Waiting for disk 0...\n");
    int retry_count = 0;
    while (!disk_is_ready(0) && retry_count < 10) {
        timer_sleep(100);  /* Wait 100ms */
        retry_count++;
        vga_writestring(".");
    }
    vga_putchar('\n');
    
    if (!disk_is_ready(0)) {
        vga_writestring("FAT32: ERROR - Disk 0 not ready after ");
        print_dec(retry_count);
        vga_writestring(" retries\n");
        return FAT32_ERROR_NOT_FOUND;
    }
    vga_writestring("FAT32: Disk 0 is ready\n");
    
    /* Get disk info */
    struct disk_info *info = disk_get_info(0);
    if (!info) {
        vga_writestring("FAT32: ERROR - Cannot get disk info\n");
        return FAT32_ERROR_IO;
    }
    
    vga_writestring("FAT32: Disk info - Size: ");
    print_dec(info->total_size / 1024);
    vga_writestring(" KB, Sectors: ");
    print_dec(info->sector_count);
    vga_putchar('\n');
    
    /* Open disk handle */
    vga_writestring("FAT32: Opening disk handle...\n");
    g_fat32.disk = disk_open(0);
    if (!g_fat32.disk) {
        vga_writestring("FAT32: ERROR - Failed to open disk handle\n");
        return FAT32_ERROR_IO;
    }
    vga_writestring("FAT32: Disk handle opened\n");
    
    /* Clear file handles */
    memset(g_fat32.files, 0, sizeof(g_fat32.files));
    
    g_fat32.initialized = 1;
    vga_writestring("FAT32: === Initialization completed ===\n");
    return FAT32_SUCCESS;
}

int fat32_mount(void) {
    vga_writestring("FAT32: === Starting mount ===\n");
    
    /* Ensure initialization */
    if (!g_fat32.initialized) {
        vga_writestring("FAT32: Not initialized, calling fat32_init()...\n");
        int result = fat32_init();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: ERROR - Init failed: ");
            print_dec(result);
            vga_putchar('\n');
            return result;
        }
    }
    
    if (g_fat32.mounted) {
        vga_writestring("FAT32: Already mounted\n");
        return FAT32_SUCCESS;
    }
    
    /* Verify disk handle */
    if (!g_fat32.disk) {
        vga_writestring("FAT32: ERROR - No disk handle\n");
        return FAT32_ERROR_IO;
    }
    
    /* Try to read boot sector */
    vga_writestring("FAT32: Reading boot sector...\n");
    int result = fat32_read_boot_sector();
    
    if (result != FAT32_SUCCESS) {
        vga_writestring("FAT32: Boot sector read failed (");
        print_dec(result);
        vga_writestring("), creating filesystem...\n");
        
        result = fat32_create_filesystem();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: ERROR - Filesystem creation failed: ");
            print_dec(result);
            vga_putchar('\n');
            return result;
        }
        
        /* Try reading boot sector again */
        result = fat32_read_boot_sector();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: ERROR - Still can't read boot sector: ");
            print_dec(result);
            vga_putchar('\n');
            return result;
        }
    }
    
    vga_writestring("FAT32: Boot sector OK, setting up parameters...\n");
    
    /* Calculate filesystem layout */
    g_fat32.fat_start_sector = g_fat32.reserved_sectors;
    g_fat32.data_start_sector = g_fat32.fat_start_sector + 
        (g_fat32.num_fats * g_fat32.sectors_per_fat);
    g_fat32.bytes_per_cluster = g_fat32.bytes_per_sector * g_fat32.sectors_per_cluster;
    g_fat32.current_dir_cluster = g_fat32.root_cluster;
    
    /* Allocate cluster buffer */
    vga_writestring("FAT32: Allocating cluster buffer (");
    print_dec(g_fat32.bytes_per_cluster);
    vga_writestring(" bytes)...\n");
    
    g_fat32.cluster_buffer = kzalloc(g_fat32.bytes_per_cluster);
    if (!g_fat32.cluster_buffer) {
        vga_writestring("FAT32: ERROR - Cluster buffer allocation failed\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Load FAT cache */
    vga_writestring("FAT32: Loading FAT cache...\n");
    result = fat32_load_fat_cache();
    if (result != FAT32_SUCCESS) {
        vga_writestring("FAT32: ERROR - FAT cache load failed: ");
        print_dec(result);
        vga_putchar('\n');
        kfree(g_fat32.cluster_buffer);
        g_fat32.cluster_buffer = NULL;
        return result;
    }
    
    g_fat32.mounted = 1;
    
    vga_writestring("FAT32: === Mount completed successfully ===\n");
    vga_writestring("Parameters:\n");
    vga_writestring("  Sectors per cluster: ");
    print_dec(g_fat32.sectors_per_cluster);
    vga_writestring("\n  Root cluster: ");
    print_dec(g_fat32.root_cluster);
    vga_writestring("\n  FAT sectors: ");
    print_dec(g_fat32.sectors_per_fat);
    vga_putchar('\n');
    
    return FAT32_SUCCESS;
}

void fat32_unmount(void) {
    if (!g_fat32.initialized) {
        return;
    }
    
    vga_writestring("FAT32: Unmounting...\n");
    
    if (g_fat32.mounted) {
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
        
        g_fat32.mounted = 0;
    }
    
    if (g_fat32.disk) {
        disk_close(g_fat32.disk);
        g_fat32.disk = NULL;
    }
    
    g_fat32.initialized = 0;
    vga_writestring("FAT32: Unmount complete\n");
}

static int fat32_create_filesystem(void) {
    vga_writestring("FAT32: === Creating filesystem ===\n");
    
    if (!g_fat32.disk) {
        vga_writestring("FAT32: ERROR - No disk for creation\n");
        return FAT32_ERROR_IO;
    }
    
    struct disk_info *info = disk_get_info(0);
    if (!info || info->sector_count < 128) {  /* Minimum viable FAT32 size */
        vga_writestring("FAT32: ERROR - Disk too small for FAT32\n");
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t total_sectors = info->sector_count;
    uint32_t reserved_sectors = 32;
    uint8_t sectors_per_cluster = 8;  /* 4KB clusters for small disks */
    
    /* For larger disks, use bigger clusters */
    if (total_sectors > 65536) sectors_per_cluster = 16;  /* 8KB */
    if (total_sectors > 262144) sectors_per_cluster = 32; /* 16KB */
    
    /* Calculate FAT size */
    uint32_t data_sectors = total_sectors - reserved_sectors;
    uint32_t sectors_per_fat = (data_sectors / sectors_per_cluster) / 128 + 1;
    if (sectors_per_fat < 32) sectors_per_fat = 32;
    
    /* Adjust for two FATs */
    data_sectors = total_sectors - reserved_sectors - (2 * sectors_per_fat);
    
    vga_writestring("FAT32: Creating layout:\n");
    vga_writestring("  Total sectors: ");
    print_dec(total_sectors);
    vga_writestring("\n  Reserved: ");
    print_dec(reserved_sectors);
    vga_writestring("\n  FAT size: ");
    print_dec(sectors_per_fat);
    vga_writestring(" x 2\n");
    
    /* Create and write boot sector */
    struct fat32_boot_sector *boot = kzalloc(FAT32_SECTOR_SIZE);
    if (!boot) {
        vga_writestring("FAT32: ERROR - Boot sector allocation failed\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Boot sector setup */
    boot->jump_code[0] = 0xEB;
    boot->jump_code[1] = 0x58;
    boot->jump_code[2] = 0x90;
    memcpy(boot->oem_name, "NUMOS1.0", 8);
    
    /* BPB */
    boot->bytes_per_sector = FAT32_SECTOR_SIZE;
    boot->sectors_per_cluster = sectors_per_cluster;
    boot->reserved_sectors = reserved_sectors;
    boot->num_fats = 2;
    boot->root_entries = 0;
    boot->total_sectors_16 = 0;
    boot->media_descriptor = 0xF8;
    boot->sectors_per_fat_16 = 0;
    boot->sectors_per_track = 63;
    boot->num_heads = 255;
    boot->hidden_sectors = 0;
    boot->total_sectors_32 = total_sectors;
    
    /* FAT32 specific */
    boot->sectors_per_fat_32 = sectors_per_fat;
    boot->ext_flags = 0;
    boot->filesystem_version = 0;
    boot->root_cluster = 2;
    boot->fsinfo_sector = 1;
    boot->backup_boot_sector = 6;
    boot->drive_number = 0x80;
    boot->boot_signature = 0x29;
    boot->volume_id = 0x12345678;
    memcpy(boot->volume_label, "NUMOS FAT32", 11);
    memcpy(boot->filesystem_type, "FAT32   ", 8);
    boot->boot_sector_signature = 0xAA55;
    
    /* Write boot sector */
    vga_writestring("FAT32: Writing boot sector...\n");
    int write_result = disk_write_sector(g_fat32.disk, 0, boot);
    if (write_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Boot sector write failed: ");
        print_dec(write_result);
        vga_putchar('\n');
        kfree(boot);
        return FAT32_ERROR_IO;
    }
    
    /* Write backup boot sector */
    disk_write_sector(g_fat32.disk, 6, boot);
    kfree(boot);
    
    /* Initialize FAT tables */
    vga_writestring("FAT32: Initializing FAT tables...\n");
    uint8_t *fat_sector = kzalloc(FAT32_SECTOR_SIZE);
    if (!fat_sector) {
        vga_writestring("FAT32: ERROR - FAT sector allocation failed\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Set up initial FAT entries */
    uint32_t *fat_entries = (uint32_t*)fat_sector;
    fat_entries[0] = 0x0FFFFF00 | 0xF8;  /* Media descriptor in FAT[0] */
    fat_entries[1] = 0x0FFFFFFF;         /* End of chain for FAT[1] */
    fat_entries[2] = 0x0FFFFFFF;         /* Root directory (cluster 2) */
    
    /* Write first sector of both FATs */
    uint32_t fat1_start = reserved_sectors;
    uint32_t fat2_start = reserved_sectors + sectors_per_fat;
    
    if (disk_write_sector(g_fat32.disk, fat1_start, fat_sector) != DISK_SUCCESS ||
        disk_write_sector(g_fat32.disk, fat2_start, fat_sector) != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - FAT initialization failed\n");
        kfree(fat_sector);
        return FAT32_ERROR_IO;
    }
    
    /* Clear remaining FAT sectors */
    memset(fat_sector, 0, FAT32_SECTOR_SIZE);
    for (uint32_t i = 1; i < sectors_per_fat; i++) {
        disk_write_sector(g_fat32.disk, fat1_start + i, fat_sector);
        disk_write_sector(g_fat32.disk, fat2_start + i, fat_sector);
    }
    kfree(fat_sector);
    
    /* Initialize root directory (cluster 2) */
    vga_writestring("FAT32: Creating root directory...\n");
    uint32_t root_start_sector = reserved_sectors + (2 * sectors_per_fat);
    uint8_t *empty_cluster = kzalloc(sectors_per_cluster * FAT32_SECTOR_SIZE);
    if (!empty_cluster) {
        vga_writestring("FAT32: ERROR - Root directory allocation failed\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    if (disk_write_sectors(g_fat32.disk, root_start_sector, sectors_per_cluster, empty_cluster) != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Root directory write failed\n");
        kfree(empty_cluster);
        return FAT32_ERROR_IO;
    }
    kfree(empty_cluster);
    
    /* Flush changes */
    vga_writestring("FAT32: Flushing to disk...\n");
    disk_flush_cache(g_fat32.disk);
    
    vga_writestring("FAT32: === Filesystem created successfully ===\n");
    return FAT32_SUCCESS;
}

static int fat32_read_boot_sector(void) {
    vga_writestring("FAT32: Reading boot sector...\n");
    
    struct fat32_boot_sector boot;
    int read_result = disk_read_sector(g_fat32.disk, 0, &boot);
    
    if (read_result != DISK_SUCCESS) {
        vga_writestring("FAT32: Boot sector read failed: ");
        print_dec(read_result);
        vga_putchar('\n');
        return FAT32_ERROR_IO;
    }
    
    /* Validate boot sector */
    if (boot.boot_sector_signature != 0xAA55) {
        vga_writestring("FAT32: Invalid boot signature\n");
        return FAT32_ERROR_INVALID;
    }
    
    if (boot.bytes_per_sector != FAT32_SECTOR_SIZE) {
        vga_writestring("FAT32: Invalid sector size\n");
        return FAT32_ERROR_INVALID;
    }
    
    if (boot.sectors_per_cluster == 0 || boot.sectors_per_fat_32 == 0 || boot.root_cluster < 2) {
        vga_writestring("FAT32: Invalid filesystem parameters\n");
        return FAT32_ERROR_INVALID;
    }
    
    /* Store validated values */
    g_fat32.bytes_per_sector = boot.bytes_per_sector;
    g_fat32.sectors_per_cluster = boot.sectors_per_cluster;
    g_fat32.reserved_sectors = boot.reserved_sectors;
    g_fat32.num_fats = boot.num_fats;
    g_fat32.sectors_per_fat = boot.sectors_per_fat_32;
    g_fat32.root_cluster = boot.root_cluster;
    
    vga_writestring("FAT32: Boot sector validated\n");
    return FAT32_SUCCESS;
}

static int fat32_load_fat_cache(void) {
    vga_writestring("FAT32: Loading FAT cache...\n");
    
    /* Cache first few sectors of FAT */
    g_fat32.fat_cache_sectors = 4;
    if (g_fat32.sectors_per_fat < 4) {
        g_fat32.fat_cache_sectors = g_fat32.sectors_per_fat;
    }
    
    uint32_t cache_size = g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE;
    g_fat32.fat_cache = kzalloc(cache_size);
    if (!g_fat32.fat_cache) {
        vga_writestring("FAT32: ERROR - FAT cache allocation failed\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    int read_result = disk_read_sectors(g_fat32.disk, g_fat32.fat_start_sector, 
                                       g_fat32.fat_cache_sectors, g_fat32.fat_cache);
    if (read_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - FAT cache read failed: ");
        print_dec(read_result);
        vga_putchar('\n');
        kfree(g_fat32.fat_cache);
        g_fat32.fat_cache = NULL;
        return FAT32_ERROR_IO;
    }
    
    vga_writestring("FAT32: FAT cache loaded successfully\n");
    return FAT32_SUCCESS;
}

/* Utility functions */
static uint32_t fat32_cluster_to_sector(uint32_t cluster) {
    if (cluster < 2) return 0;
    return g_fat32.data_start_sector + ((cluster - 2) * g_fat32.sectors_per_cluster);
}

static int fat32_find_dir_entry(uint32_t dir_cluster, const char *formatted_name, 
                               struct fat32_dir_entry *entry, int *entry_index) {
    uint32_t sector = fat32_cluster_to_sector(dir_cluster);
    if (sector == 0) return 0;
    
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
            if (entry) memcpy(entry, &entries[i], sizeof(struct fat32_dir_entry));
            if (entry_index) *entry_index = i;
            return 1;
        }
    }
    return 0;
}

static struct fat32_file* fat32_find_free_handle(void) {
    for (int i = 0; i < FAT32_MAX_OPEN_FILES; i++) {
        if (!g_fat32.files[i].is_open) return &g_fat32.files[i];
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

/* Public API functions */
uint32_t fat32_get_next_cluster(uint32_t cluster) {
    if (!g_fat32.fat_cache || cluster < 2) return FAT32_END_OF_CHAIN;
    
    uint32_t max_cached = (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4;
    if (cluster < max_cached) {
        return g_fat32.fat_cache[cluster] & 0x0FFFFFFF;
    }
    return FAT32_END_OF_CHAIN;
}

uint32_t fat32_allocate_cluster(void) {
    if (!g_fat32.fat_cache) return 0;
    
    uint32_t max_entries = (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4;
    for (uint32_t i = 3; i < max_entries; i++) {
        if ((g_fat32.fat_cache[i] & 0x0FFFFFFF) == FAT32_FREE_CLUSTER) {
            g_fat32.fat_cache[i] = FAT32_END_OF_CHAIN;
            
            /* Write back to both FATs */
            disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector, 
                             g_fat32.fat_cache_sectors, g_fat32.fat_cache);
            disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector + g_fat32.sectors_per_fat, 
                             g_fat32.fat_cache_sectors, g_fat32.fat_cache);
            return i;
        }
    }
    return 0;
}

int fat32_free_cluster_chain(uint32_t start_cluster) {
    if (!g_fat32.fat_cache || start_cluster < 2) return FAT32_ERROR_INVALID;
    
    uint32_t max_entries = (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4;
    if (start_cluster < max_entries) {
        g_fat32.fat_cache[start_cluster] = FAT32_FREE_CLUSTER;
        
        /* Write back to both FATs */
        disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector, 
                         g_fat32.fat_cache_sectors, g_fat32.fat_cache);
        disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector + g_fat32.sectors_per_fat, 
                         g_fat32.fat_cache_sectors, g_fat32.fat_cache);
    }
    return FAT32_SUCCESS;
}

void fat32_list_files(void) {
    if (!g_fat32.mounted) {
        vga_writestring("FAT32: Not mounted\n");
        return;
    }
    
    vga_writestring("Directory listing (root):\n");
    
    uint32_t root_sector = fat32_cluster_to_sector(g_fat32.root_cluster);
    if (root_sector == 0) {
        vga_writestring("FAT32: ERROR - Invalid root cluster (");
        print_dec(g_fat32.root_cluster);
        vga_writestring(")\n");
        vga_writestring("  fat_start_sector: ");
        print_dec(g_fat32.fat_start_sector);
        vga_writestring("\n  data_start_sector: ");
        print_dec(g_fat32.data_start_sector);
        vga_writestring("\n  sectors_per_cluster: ");
        print_dec(g_fat32.sectors_per_cluster);
        vga_writestring("\n");
        return;
    }
    
    vga_writestring("  Root sector: ");
    print_dec(root_sector);
    vga_writestring("\n  Reading ");
    print_dec(g_fat32.sectors_per_cluster);
    vga_writestring(" sectors...\n");
    
    int read_result = disk_read_sectors(g_fat32.disk, root_sector, 
                                       g_fat32.sectors_per_cluster, 
                                       g_fat32.cluster_buffer);
    
    if (read_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to read root directory (code: ");
        print_dec(read_result);
        vga_writestring(")\n");
        return;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    int entry_count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    int file_count = 0;
    
    vga_writestring("  Scanning ");
    print_dec(entry_count);
    vga_writestring(" possible entries...\n\n");
    
    /* Show first entry for debugging */
    vga_writestring("  First entry name[0] = 0x");
    print_hex32(entries[0].name[0]);
    vga_writestring("\n\n");
    
    for (int i = 0; i < entry_count; i++) {
        /* End of directory marker */
        if (entries[i].name[0] == 0x00) {
            vga_writestring("  [End of directory at entry ");
            print_dec(i);
            vga_writestring("]\n");
            break;
        }
        
        /* Deleted entry */
        if (entries[i].name[0] == 0xE5) {
            continue;
        }
        
        /* Skip long filename entries */
        if (entries[i].attributes & FAT32_ATTR_LONG_NAME) {
            continue;
        }
        
        /* Skip volume label */
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) {
            vga_writestring("  [Volume: ");
            for (int j = 0; j < 11 && entries[i].name[j] != ' '; j++) {
                vga_putchar(entries[i].name[j]);
            }
            vga_writestring("]\n");
            continue;
        }
        
        /* Format filename */
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
        
        /* Print entry */
        vga_writestring("  ");
        vga_writestring(filename);
        
        /* Pad to 13 characters */
        for (int j = pos; j < 13; j++) {
            vga_putchar(' ');
        }
        
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) {
            vga_writestring("<DIR>     ");
        } else {
            /* Right-align file size in 10 characters */
            uint32_t size = entries[i].file_size;
            char size_str[16];
            int size_pos = 15;
            size_str[size_pos--] = '\0';
            
            if (size == 0) {
                size_str[size_pos--] = '0';
            } else {
                while (size > 0 && size_pos >= 0) {
                    size_str[size_pos--] = '0' + (size % 10);
                    size /= 10;
                }
            }
            
            /* Pad to 10 chars */
            while (15 - size_pos < 10) {
                size_str[size_pos--] = ' ';
            }
            
            vga_writestring(&size_str[size_pos + 1]);
            vga_writestring(" bytes");
        }
        
        /* Show cluster number for debugging */
        uint32_t cluster = ((uint32_t)entries[i].first_cluster_high << 16) | 
                          entries[i].first_cluster_low;
        vga_writestring(" [cluster ");
        print_dec(cluster);
        vga_writestring("]");
        
        vga_putchar('\n');
        file_count++;
    }
    
    vga_putchar('\n');
    if (file_count == 0) {
        vga_writestring("  (empty directory)\n");
    } else {
        vga_writestring("  Total: ");
        print_dec(file_count);
        vga_writestring(" file(s)\n");
    }
}

int fat32_format_name(const char *filename, char *formatted_name) {
    if (!filename || !formatted_name) return FAT32_ERROR_INVALID;
    
    /* Initialize with spaces */
    memset(formatted_name, ' ', 11);
    formatted_name[11] = '\0';
    
    const char *dot = strstr(filename, ".");
    int name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    
    /* Prevent buffer overflow */
    if (name_len > 8) name_len = 8;
    
    /* Copy name part */
    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;  /* Convert to uppercase */
        formatted_name[i] = c;
    }
    
    /* Copy extension part */
    if (dot && dot[1] != '\0') {
        int ext_len = strlen(dot + 1);
        if (ext_len > 3) ext_len = 3;
        
        for (int i = 0; i < ext_len; i++) {
            char c = dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 32;  /* Convert to uppercase */
            formatted_name[8 + i] = c;
        }
    }
    
    return FAT32_SUCCESS;
}

int fat32_exists(const char *filename) {
    if (!g_fat32.mounted || !filename) return 0;
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        return 0;
    }
    
    return fat32_find_dir_entry(g_fat32.current_dir_cluster, formatted_name, NULL, NULL);
}

uint32_t fat32_get_file_size(const char *filename) {
    if (!g_fat32.mounted || !filename) return 0;
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        return 0;
    }
    
    struct fat32_dir_entry entry;
    if (fat32_find_dir_entry(g_fat32.current_dir_cluster, formatted_name, &entry, NULL)) {
        return entry.file_size;
    }
    return 0;
}

void fat32_print_file_info(const char *filename) {
    if (!g_fat32.mounted || !filename) {
        vga_writestring("FAT32: Not mounted or invalid filename\n");
        return;
    }
    
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        vga_writestring("FAT32: Invalid filename format\n");
        return;
    }
    
    struct fat32_dir_entry entry;
    if (!fat32_find_dir_entry(g_fat32.current_dir_cluster, formatted_name, &entry, NULL)) {
        vga_writestring("FAT32: File not found: ");
        vga_writestring(filename);
        vga_putchar('\n');
        return;
    }
    
    vga_writestring("File info for: ");
    vga_writestring(filename);
    vga_writestring("\n  Size: ");
    print_dec(entry.file_size);
    vga_writestring(" bytes\n  First cluster: ");
    uint32_t first_cluster = ((uint32_t)entry.first_cluster_high << 16) | entry.first_cluster_low;
    print_dec(first_cluster);
    vga_writestring("\n  Attributes: 0x");
    print_hex(entry.attributes);
    vga_putchar('\n');
}

void fat32_print_boot_sector(void) {
    if (!g_fat32.mounted) {
        vga_writestring("FAT32: Not mounted\n");
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
    vga_writestring("\n  FAT start sector: ");
    print_dec(g_fat32.fat_start_sector);
    vga_writestring("\n  Data start sector: ");
    print_dec(g_fat32.data_start_sector);
    vga_putchar('\n');
}

void fat32_print_fs_info(void) {
    vga_writestring("FAT32 Status:\n");
    vga_writestring("  Initialized: ");
    vga_writestring(g_fat32.initialized ? "Yes" : "No");
    vga_writestring("\n  Mounted: ");
    vga_writestring(g_fat32.mounted ? "Yes" : "No");
    
    if (g_fat32.mounted) {
        vga_writestring("\n  Current dir cluster: ");
        print_dec(g_fat32.current_dir_cluster);
        vga_writestring("\n  Bytes per cluster: ");
        print_dec(g_fat32.bytes_per_cluster);
        vga_writestring("\n  FAT cache sectors: ");
        print_dec(g_fat32.fat_cache_sectors);
    }
    
    if (g_fat32.disk) {
        vga_writestring("\n  Disk handle: Available");
        struct disk_info *info = disk_get_info(0);
        if (info) {
            vga_writestring("\n  Disk status: 0x");
            print_hex(info->status);
            vga_writestring("\n  Disk type: ");
            print_dec(info->disk_type);
            vga_writestring("\n  Sector count: ");
            print_dec(info->sector_count);
        }
    } else {
        vga_writestring("\n  Disk handle: NULL");
    }
    vga_putchar('\n');
}

static int fat32_create_dir_entry(const char *formatted_name, uint32_t first_cluster, 
                                 uint32_t file_size, uint8_t attributes) {
    if (!g_fat32.mounted) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t root_sector = fat32_cluster_to_sector(g_fat32.root_cluster);
    if (root_sector == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Read root directory */
    if (disk_read_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                         g_fat32.cluster_buffer) != DISK_SUCCESS) {
        return FAT32_ERROR_IO;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    int entry_count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    /* Find free entry */
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            /* Found free entry */
            memset(&entries[i], 0, sizeof(struct fat32_dir_entry));
            memcpy(entries[i].name, formatted_name, 11);
            entries[i].attributes = attributes;
            entries[i].first_cluster_low = first_cluster & 0xFFFF;
            entries[i].first_cluster_high = (first_cluster >> 16) & 0xFFFF;
            entries[i].file_size = file_size;
            
            /* Set current date/time (simplified) */
            entries[i].creation_date = 0x4A21; /* Jan 1, 2017 */
            entries[i].creation_time = 0x0000;
            entries[i].last_write_date = 0x4A21;
            entries[i].last_write_time = 0x0000;
            entries[i].last_access_date = 0x4A21;
            
            /* Write back to disk */
            return (disk_write_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                                     g_fat32.cluster_buffer) == DISK_SUCCESS) ? 
                   FAT32_SUCCESS : FAT32_ERROR_IO;
        }
    }
    
    return FAT32_ERROR_NO_SPACE; /* Directory full */
}

/* Helper function to update an existing directory entry */
static int fat32_update_dir_entry(const char *formatted_name, uint32_t first_cluster, 
                                 uint32_t file_size) {
    if (!g_fat32.mounted) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t root_sector = fat32_cluster_to_sector(g_fat32.root_cluster);
    if (root_sector == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    /* Read root directory */
    if (disk_read_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                         g_fat32.cluster_buffer) != DISK_SUCCESS) {
        return FAT32_ERROR_IO;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    int entry_count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    /* Find the entry */
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & (FAT32_ATTR_LONG_NAME | FAT32_ATTR_VOLUME_ID)) continue;
        
        if (memcmp(entries[i].name, formatted_name, 11) == 0) {
            /* Update entry */
            entries[i].first_cluster_low = first_cluster & 0xFFFF;
            entries[i].first_cluster_high = (first_cluster >> 16) & 0xFFFF;
            entries[i].file_size = file_size;
            entries[i].last_write_date = 0x4A21; /* Update timestamp */
            entries[i].last_write_time = 0x0000;
            
            /* Write back to disk */
            return (disk_write_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                                     g_fat32.cluster_buffer) == DISK_SUCCESS) ? 
                   FAT32_SUCCESS : FAT32_ERROR_IO;
        }
    }
    
    return FAT32_ERROR_NOT_FOUND;
}

/* Helper function to read data from a cluster chain */
static int fat32_read_cluster_chain(uint32_t start_cluster, void *buffer, 
                                   uint32_t offset, uint32_t size) {
    if (start_cluster < 2 || !buffer || size == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t cluster = start_cluster;
    uint32_t bytes_read = 0;
    uint32_t cluster_offset = offset % g_fat32.bytes_per_cluster;
    uint32_t clusters_to_skip = offset / g_fat32.bytes_per_cluster;
    
    /* Skip clusters to reach the starting position */
    for (uint32_t i = 0; i < clusters_to_skip && cluster < FAT32_END_OF_CHAIN; i++) {
        cluster = fat32_get_next_cluster(cluster);
    }
    
    if (cluster >= FAT32_END_OF_CHAIN) {
        return FAT32_ERROR_INVALID; /* Offset beyond file end */
    }
    
    /* Read data */
    uint8_t *buf = (uint8_t*)buffer;
    while (bytes_read < size && cluster < FAT32_END_OF_CHAIN) {
        /* Read current cluster */
        uint32_t sector = fat32_cluster_to_sector(cluster);
        if (sector == 0 || disk_read_sectors(g_fat32.disk, sector, g_fat32.sectors_per_cluster, 
                                           g_fat32.cluster_buffer) != DISK_SUCCESS) {
            return FAT32_ERROR_IO;
        }
        
        /* Calculate how much to copy from this cluster */
        uint32_t bytes_in_cluster = g_fat32.bytes_per_cluster - cluster_offset;
        uint32_t bytes_to_copy = (size - bytes_read < bytes_in_cluster) ? 
                                (size - bytes_read) : bytes_in_cluster;
        
        /* Copy data */
        memcpy(buf + bytes_read, g_fat32.cluster_buffer + cluster_offset, bytes_to_copy);
        bytes_read += bytes_to_copy;
        cluster_offset = 0; /* Only first cluster may have offset */
        
        /* Move to next cluster */
        if (bytes_read < size) {
            cluster = fat32_get_next_cluster(cluster);
        }
    }
    
    return bytes_read;
}

/* Helper function to write data to a cluster chain */
static int fat32_write_cluster_chain(uint32_t start_cluster, const void *buffer, 
                                    uint32_t offset, uint32_t size) {
    if (start_cluster < 2 || !buffer || size == 0) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t cluster = start_cluster;
    uint32_t bytes_written = 0;
    uint32_t cluster_offset = offset % g_fat32.bytes_per_cluster;
    uint32_t clusters_to_skip = offset / g_fat32.bytes_per_cluster;
    
    /* Skip clusters to reach the starting position */
    for (uint32_t i = 0; i < clusters_to_skip && cluster < FAT32_END_OF_CHAIN; i++) {
        cluster = fat32_get_next_cluster(cluster);
    }
    
    if (cluster >= FAT32_END_OF_CHAIN) {
        return FAT32_ERROR_INVALID; /* Offset beyond allocated space */
    }
    
    /* Write data */
    const uint8_t *buf = (const uint8_t*)buffer;
    while (bytes_written < size && cluster < FAT32_END_OF_CHAIN) {
        /* Read current cluster first (for partial writes) */
        uint32_t sector = fat32_cluster_to_sector(cluster);
        if (sector == 0) {
            return FAT32_ERROR_IO;
        }
        
        if (cluster_offset != 0 || (size - bytes_written) < g_fat32.bytes_per_cluster) {
            /* Partial cluster write - need to read first */
            if (disk_read_sectors(g_fat32.disk, sector, g_fat32.sectors_per_cluster, 
                                g_fat32.cluster_buffer) != DISK_SUCCESS) {
                return FAT32_ERROR_IO;
            }
        }
        
        /* Calculate how much to write to this cluster */
        uint32_t bytes_in_cluster = g_fat32.bytes_per_cluster - cluster_offset;
        uint32_t bytes_to_write = (size - bytes_written < bytes_in_cluster) ? 
                                 (size - bytes_written) : bytes_in_cluster;
        
        /* Update cluster buffer */
        memcpy(g_fat32.cluster_buffer + cluster_offset, buf + bytes_written, bytes_to_write);
        
        /* Write cluster back to disk */
        if (disk_write_sectors(g_fat32.disk, sector, g_fat32.sectors_per_cluster, 
                             g_fat32.cluster_buffer) != DISK_SUCCESS) {
            return FAT32_ERROR_IO;
        }
        
        bytes_written += bytes_to_write;
        cluster_offset = 0; /* Only first cluster may have offset */
        
        /* Move to next cluster or allocate new one */
        if (bytes_written < size) {
            uint32_t next_cluster = fat32_get_next_cluster(cluster);
            if (next_cluster >= FAT32_END_OF_CHAIN) {
                /* Need to allocate new cluster */
                next_cluster = fat32_allocate_cluster();
                if (next_cluster == 0) {
                    return FAT32_ERROR_NO_SPACE; /* Disk full */
                }
                
                /* Link clusters */
                if (cluster < (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4) {
                    g_fat32.fat_cache[cluster] = next_cluster;
                    disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector, 
                                     g_fat32.fat_cache_sectors, g_fat32.fat_cache);
                    disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector + g_fat32.sectors_per_fat, 
                                     g_fat32.fat_cache_sectors, g_fat32.fat_cache);
                }
            }
            cluster = next_cluster;
        }
    }
    
    return bytes_written;
}

/* File I/O stub implementations - these need to be implemented for full functionality */
struct fat32_file* fat32_fopen(const char *filename, const char *mode) {
    if (!g_fat32.mounted || !filename || !mode) {
        vga_writestring("FAT32: fopen - invalid parameters\n");
        return NULL;
    }
    
    vga_writestring("FAT32: Opening file '");
    vga_writestring(filename);
    vga_writestring("' mode '");
    vga_writestring(mode);
    vga_writestring("'\n");
    
    /* Parse mode string */
    int file_mode = fat32_parse_mode(mode);
    if (file_mode < 0) {
        vga_writestring("FAT32: Invalid file mode\n");
        return NULL;
    }
    
    /* Find free file handle */
    struct fat32_file *file = fat32_find_free_handle();
    if (!file) {
        vga_writestring("FAT32: No free file handles\n");
        return NULL;
    }
    
    /* Format filename to FAT32 8.3 format */
    char formatted_name[12];
    if (fat32_format_name(filename, formatted_name) != FAT32_SUCCESS) {
        vga_writestring("FAT32: Invalid filename format\n");
        return NULL;
    }
    
    /* Search for existing file */
    struct fat32_dir_entry dir_entry;
    int entry_found = fat32_find_dir_entry(g_fat32.current_dir_cluster, formatted_name, 
                                          &dir_entry, NULL);
    
    if (entry_found) {
        /* File exists */
        vga_writestring("FAT32: File exists (");
        print_dec(dir_entry.file_size);
        vga_writestring(" bytes)\n");
        
        if (file_mode & FAT32_MODE_WRITE && !(file_mode & FAT32_MODE_APPEND)) {
            /* Write mode - truncate file */
            vga_writestring("FAT32: Truncating existing file\n");
            
            uint32_t first_cluster = ((uint32_t)dir_entry.first_cluster_high << 16) | 
                                   dir_entry.first_cluster_low;
            
            /* Free cluster chain if it exists */
            if (first_cluster >= 2) {
                fat32_free_cluster_chain(first_cluster);
            }
            
            /* Update directory entry */
            fat32_update_dir_entry(formatted_name, 0, 0);
            
            /* Set up file handle */
            strcpy(file->name, filename);
            file->first_cluster = 0;
            file->current_cluster = 0;
            file->file_size = 0;
            file->position = 0;
            file->attributes = dir_entry.attributes;
            file->mode = file_mode;
            file->is_open = 1;
        } else {
            /* Read or append mode */
            uint32_t first_cluster = ((uint32_t)dir_entry.first_cluster_high << 16) | 
                                   dir_entry.first_cluster_low;
            
            /* Set up file handle */
            strcpy(file->name, filename);
            file->first_cluster = first_cluster;
            file->current_cluster = first_cluster;
            file->file_size = dir_entry.file_size;
            file->attributes = dir_entry.attributes;
            file->mode = file_mode;
            file->is_open = 1;
            
            if (file_mode & FAT32_MODE_APPEND) {
                /* Append mode - position at end */
                file->position = file->file_size;
            } else {
                /* Read mode - position at beginning */
                file->position = 0;
            }
        }
    } else {
        /* File doesn't exist */
        if (!(file_mode & FAT32_MODE_CREATE)) {
            vga_writestring("FAT32: File not found and not in create mode\n");
            return NULL;
        }
        
        vga_writestring("FAT32: Creating new file\n");
        
        /* Create new directory entry */
        if (fat32_create_dir_entry(formatted_name, 0, 0, FAT32_ATTR_ARCHIVE) != FAT32_SUCCESS) {
            vga_writestring("FAT32: Failed to create directory entry\n");
            return NULL;
        }
        
        /* Set up file handle */
        strcpy(file->name, filename);
        file->first_cluster = 0;
        file->current_cluster = 0;
        file->file_size = 0;
        file->position = 0;
        file->attributes = FAT32_ATTR_ARCHIVE;
        file->mode = file_mode;
        file->is_open = 1;
    }
    
    vga_writestring("FAT32: File opened successfully\n");
    return file;
}

int fat32_fclose(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    vga_writestring("FAT32: Closing file '");
    vga_writestring(file->name);
    vga_writestring("'\n");
    
    /* Flush any pending writes */
    fat32_fflush(file);
    
    /* Clear file handle */
    memset(file, 0, sizeof(struct fat32_file));
    
    vga_writestring("FAT32: File closed\n");
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
    
    uint32_t total_bytes = size * count;
    uint32_t available_bytes = (file->file_size > file->position) ? 
                              (file->file_size - file->position) : 0;
    
    if (total_bytes > available_bytes) {
        total_bytes = available_bytes;
    }
    
    if (total_bytes == 0 || file->first_cluster < 2) {
        return 0; /* Nothing to read or empty file */
    }
    
    vga_writestring("FAT32: Reading ");
    print_dec(total_bytes);
    vga_writestring(" bytes at position ");
    print_dec(file->position);
    vga_putchar('\n');
    
    /* Read data from cluster chain */
    int bytes_read = fat32_read_cluster_chain(file->first_cluster, buffer, 
                                            file->position, total_bytes);
    
    if (bytes_read > 0) {
        file->position += bytes_read;
        return bytes_read / size; /* Return number of complete elements read */
    }
    
    return 0;
}

size_t fat32_fwrite(const void *buffer, size_t size, size_t count, struct fat32_file *file) {
    if (!file || !file->is_open || !buffer || size == 0 || count == 0) {
        return 0;
    }
    
    if (!(file->mode & (FAT32_MODE_WRITE | FAT32_MODE_APPEND))) {
        vga_writestring("FAT32: File not open for writing\n");
        return 0;
    }
    
    uint32_t total_bytes = size * count;
    
    vga_writestring("FAT32: Writing ");
    print_dec(total_bytes);
    vga_writestring(" bytes at position ");
    print_dec(file->position);
    vga_putchar('\n');
    
    /* Allocate first cluster if file is empty */
    if (file->first_cluster < 2) {
        file->first_cluster = fat32_allocate_cluster();
        if (file->first_cluster == 0) {
            vga_writestring("FAT32: Failed to allocate cluster\n");
            return 0;
        }
        file->current_cluster = file->first_cluster;
        
        /* Update directory entry with first cluster */
        char formatted_name[12];
        fat32_format_name(file->name, formatted_name);
        fat32_update_dir_entry(formatted_name, file->first_cluster, 0);
    }
    
    /* Write data to cluster chain */
    int bytes_written = fat32_write_cluster_chain(file->first_cluster, buffer, 
                                                file->position, total_bytes);
    
    if (bytes_written > 0) {
        file->position += bytes_written;
        
        /* Update file size if we wrote beyond current end */
        if (file->position > file->file_size) {
            file->file_size = file->position;
            
            /* Update directory entry with new file size */
            char formatted_name[12];
            fat32_format_name(file->name, formatted_name);
            fat32_update_dir_entry(formatted_name, file->first_cluster, file->file_size);
        }
        
        return bytes_written / size; /* Return number of complete elements written */
    }
    
    return 0;
}


int fat32_fseek(struct fat32_file *file, long offset, int whence) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t new_position;
    
    switch (whence) {
        case FAT32_SEEK_SET:
            new_position = (offset >= 0) ? (uint32_t)offset : 0;
            break;
            
        case FAT32_SEEK_CUR:
            if (offset >= 0) {
                new_position = file->position + (uint32_t)offset;
            } else {
                new_position = (file->position >= (uint32_t)(-offset)) ? 
                              (file->position - (uint32_t)(-offset)) : 0;
            }
            break;
            
        case FAT32_SEEK_END:
            if (offset >= 0) {
                new_position = file->file_size + (uint32_t)offset;
            } else {
                new_position = (file->file_size >= (uint32_t)(-offset)) ? 
                              (file->file_size - (uint32_t)(-offset)) : 0;
            }
            break;
            
        default:
            return FAT32_ERROR_INVALID;
    }
    
    file->position = new_position;
    
    vga_writestring("FAT32: Seek to position ");
    print_dec(file->position);
    vga_putchar('\n');
    
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
    
    return (file->position >= file->file_size) ? 1 : 0;
}

int fat32_fflush(struct fat32_file *file) {
    if (!file || !file->is_open) {
        return FAT32_ERROR_INVALID;
    }
    
    /* For our implementation, data is written immediately */
    /* Just flush the disk cache */
    if (g_fat32.disk) {
        disk_flush_cache(g_fat32.disk);
    }
    
    return FAT32_SUCCESS;
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
