#include "fs/fat32.h"
#include "kernel.h"
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
    vga_writestring("FAT32: === Starting detailed initialization ===\n");
    
    if (g_fat32.initialized) {
        vga_writestring("FAT32: Already initialized\n");
        return FAT32_SUCCESS;
    }
    
    /* Check if heap is available */
    vga_writestring("FAT32: Testing memory allocation...\n");
    void *test_ptr = kmalloc(1024);
    if (!test_ptr) {
        vga_writestring("FAT32: ERROR - Memory allocation failed! Heap not ready?\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    kfree(test_ptr);
    vga_writestring("FAT32: Memory allocation test passed\n");
    
    /* Initialize disk subsystem first */
    vga_writestring("FAT32: Checking disk subsystem...\n");
    int disk_result = disk_init();
    vga_writestring("FAT32: disk_init() returned: ");
    print_dec(disk_result);
    vga_putchar('\n');
    
    if (disk_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to initialize disk subsystem\n");
        return FAT32_ERROR_IO;
    }
    
    /* Check if disk 0 exists and is ready */
    vga_writestring("FAT32: Checking disk 0 availability...\n");
    if (!disk_is_ready(0)) {
        vga_writestring("FAT32: Disk 0 is not ready, trying to create/mount disk image...\n");
        
        /* Wait a bit and try again */
        timer_sleep(100);
        
        if (!disk_is_ready(0)) {
            vga_writestring("FAT32: ERROR - Disk 0 still not ready after wait\n");
            return FAT32_ERROR_NOT_FOUND;
        }
    }
    vga_writestring("FAT32: Disk 0 is ready\n");
    
    /* Get disk info */
    struct disk_info *info = disk_get_info(0);
    if (!info) {
        vga_writestring("FAT32: ERROR - Cannot get disk 0 info\n");
        return FAT32_ERROR_IO;
    }
    
    vga_writestring("FAT32: Disk info - Size: ");
    print_dec(info->total_size / 1024);
    vga_writestring(" KB, Sectors: ");
    print_dec(info->sector_count);
    vga_writestring(", Type: ");
    print_dec(info->disk_type);
    vga_putchar('\n');
    
    /* Open disk handle */
    vga_writestring("FAT32: Opening disk handle...\n");
    g_fat32.disk = disk_open(0);
    if (!g_fat32.disk) {
        vga_writestring("FAT32: ERROR - Failed to open disk 0 handle\n");
        return FAT32_ERROR_IO;
    }
    vga_writestring("FAT32: Disk handle opened successfully\n");
    
    /* Clear file handles */
    memset(g_fat32.files, 0, sizeof(g_fat32.files));
    
    /* Reset state */
    g_fat32.mounted = 0;
    g_fat32.fat_cache = NULL;
    g_fat32.cluster_buffer = NULL;
    
    g_fat32.initialized = 1;
    vga_writestring("FAT32: === Initialization completed successfully ===\n");
    return FAT32_SUCCESS;
}

int fat32_mount(void) {
    vga_writestring("FAT32: === Starting detailed mount process ===\n");
    
    if (!g_fat32.initialized) {
        vga_writestring("FAT32: Not initialized, calling fat32_init()...\n");
        int result = fat32_init();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: ERROR - Initialization failed with code: ");
            print_dec(result);
            vga_putchar('\n');
            return result;
        }
    }
    
    if (g_fat32.mounted) {
        vga_writestring("FAT32: Already mounted\n");
        return FAT32_SUCCESS;
    }
    
    if (!g_fat32.disk) {
        vga_writestring("FAT32: ERROR - No disk handle available\n");
        return FAT32_ERROR_IO;
    }
    
    /* Try to read existing boot sector first */
    vga_writestring("FAT32: Attempting to read existing boot sector...\n");
    int result = fat32_read_boot_sector();
    
    if (result != FAT32_SUCCESS) {
        vga_writestring("FAT32: No valid filesystem found (error code: ");
        print_dec(result);
        vga_writestring("), creating new filesystem...\n");
        
        /* Create a new FAT32 filesystem */
        result = fat32_create_filesystem();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: ERROR - Failed to create filesystem, code: ");
            print_dec(result);
            vga_putchar('\n');
            return result;
        }
        
        /* Try to read the boot sector again */
        vga_writestring("FAT32: Reading newly created boot sector...\n");
        result = fat32_read_boot_sector();
        if (result != FAT32_SUCCESS) {
            vga_writestring("FAT32: ERROR - Failed to read newly created boot sector, code: ");
            print_dec(result);
            vga_putchar('\n');
            return result;
        }
    }
    
    vga_writestring("FAT32: Boot sector read successfully, validating parameters...\n");
    
    /* Validate filesystem parameters */
    if (g_fat32.bytes_per_sector != FAT32_SECTOR_SIZE) {
        vga_writestring("FAT32: ERROR - Invalid sector size: ");
        print_dec(g_fat32.bytes_per_sector);
        vga_writestring(" (expected ");
        print_dec(FAT32_SECTOR_SIZE);
        vga_writestring(")\n");
        return FAT32_ERROR_INVALID;
    }
    
    if (g_fat32.sectors_per_cluster == 0 || g_fat32.sectors_per_cluster > 128) {
        vga_writestring("FAT32: ERROR - Invalid sectors per cluster: ");
        print_dec(g_fat32.sectors_per_cluster);
        vga_putchar('\n');
        return FAT32_ERROR_INVALID;
    }
    
    if (g_fat32.root_cluster < 2) {
        vga_writestring("FAT32: ERROR - Invalid root cluster: ");
        print_dec(g_fat32.root_cluster);
        vga_putchar('\n');
        return FAT32_ERROR_INVALID;
    }
    
    /* Calculate filesystem parameters */
    g_fat32.fat_start_sector = g_fat32.reserved_sectors;
    g_fat32.data_start_sector = g_fat32.fat_start_sector + 
        (g_fat32.num_fats * g_fat32.sectors_per_fat);
    g_fat32.bytes_per_cluster = g_fat32.bytes_per_sector * g_fat32.sectors_per_cluster;
    g_fat32.current_dir_cluster = g_fat32.root_cluster;
    
    vga_writestring("FAT32: Filesystem parameters validated:\n");
    vga_writestring("  FAT start sector: ");
    print_dec(g_fat32.fat_start_sector);
    vga_writestring("\n  Data start sector: ");
    print_dec(g_fat32.data_start_sector);
    vga_writestring("\n  Bytes per cluster: ");
    print_dec(g_fat32.bytes_per_cluster);
    vga_putchar('\n');
    
    /* Allocate cluster buffer */
    vga_writestring("FAT32: Allocating cluster buffer (");
    print_dec(g_fat32.bytes_per_cluster);
    vga_writestring(" bytes)...\n");
    g_fat32.cluster_buffer = kmalloc(g_fat32.bytes_per_cluster);
    if (!g_fat32.cluster_buffer) {
        vga_writestring("FAT32: ERROR - Failed to allocate cluster buffer\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    vga_writestring("FAT32: Cluster buffer allocated successfully\n");
    
    /* Load FAT cache */
    vga_writestring("FAT32: Loading FAT cache...\n");
    result = fat32_load_fat_cache();
    if (result != FAT32_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to load FAT cache, code: ");
        print_dec(result);
        vga_putchar('\n');
        if (g_fat32.cluster_buffer) {
            kfree(g_fat32.cluster_buffer);
            g_fat32.cluster_buffer = NULL;
        }
        return result;
    }
    vga_writestring("FAT32: FAT cache loaded successfully\n");
    
    g_fat32.mounted = 1;
    
    vga_writestring("FAT32: === Mount completed successfully! ===\n");
    vga_writestring("Summary:\n");
    vga_writestring("  Bytes per sector: ");
    print_dec(g_fat32.bytes_per_sector);
    vga_writestring("\n  Sectors per cluster: ");
    print_dec(g_fat32.sectors_per_cluster);
    vga_writestring("\n  Root cluster: ");
    print_dec(g_fat32.root_cluster);
    vga_writestring("\n  Sectors per FAT: ");
    print_dec(g_fat32.sectors_per_fat);
    vga_putchar('\n');
    
    return FAT32_SUCCESS;
}

void fat32_unmount(void) {
    if (!g_fat32.initialized) {
        return;
    }
    
    vga_writestring("FAT32: Unmounting filesystem...\n");
    
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
    vga_writestring("FAT32: Filesystem unmounted\n");
}

/* Create a minimal but valid FAT32 filesystem */
static int fat32_create_filesystem(void) {
    vga_writestring("FAT32: === Creating new filesystem ===\n");
    
    if (!g_fat32.disk) {
        vga_writestring("FAT32: ERROR - No disk handle for filesystem creation\n");
        return FAT32_ERROR_IO;
    }
    
    /* Get disk info */
    struct disk_info *info = disk_get_info(0);
    if (!info || info->sector_count == 0) {
        vga_writestring("FAT32: ERROR - Cannot get disk information for filesystem creation\n");
        return FAT32_ERROR_IO;
    }
    
    vga_writestring("FAT32: Creating filesystem on disk with ");
    print_dec(info->sector_count);
    vga_writestring(" sectors (");
    print_dec(info->total_size / 1024 / 1024);
    vga_writestring(" MB)\n");
    
    /* Calculate filesystem parameters */
    uint32_t total_sectors = info->sector_count;
    uint32_t reserved_sectors = 32;
    uint8_t sectors_per_cluster = 8; /* 4KB clusters */
    
    /* Calculate sectors per FAT (conservative estimate) */
    uint32_t sectors_per_fat = total_sectors / 128; /* About 0.8% of disk */
    if (sectors_per_fat < 64) sectors_per_fat = 64;
    if (sectors_per_fat > 1024) sectors_per_fat = 1024;
    
    uint32_t data_sectors = total_sectors - reserved_sectors - (2 * sectors_per_fat);
    uint32_t cluster_count = data_sectors / sectors_per_cluster;
    
    vga_writestring("FAT32: Filesystem layout:\n");
    vga_writestring("  Total sectors: ");
    print_dec(total_sectors);
    vga_writestring("\n  Reserved: ");
    print_dec(reserved_sectors);
    vga_writestring("\n  FAT sectors: ");
    print_dec(sectors_per_fat);
    vga_writestring(" x 2\n  Data sectors: ");
    print_dec(data_sectors);
    vga_writestring("\n  Clusters: ");
    print_dec(cluster_count);
    vga_putchar('\n');
    
    /* Create boot sector */
    vga_writestring("FAT32: Creating boot sector...\n");
    struct fat32_boot_sector *boot = kzalloc(sizeof(struct fat32_boot_sector));
    if (!boot) {
        vga_writestring("FAT32: ERROR - Cannot allocate boot sector\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Jump instruction */
    boot->jump_code[0] = 0xEB;
    boot->jump_code[1] = 0x58;
    boot->jump_code[2] = 0x90;
    
    /* OEM name */
    memcpy(boot->oem_name, "NUMOS1.0", 8);
    
    /* BIOS Parameter Block */
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
    
    /* FAT32 Extended BPB */
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
    vga_writestring("FAT32: Writing boot sector to disk...\n");
    int write_result = disk_write_sector(g_fat32.disk, 0, boot);
    if (write_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to write boot sector, disk error: ");
        print_dec(write_result);
        vga_putchar('\n');
        kfree(boot);
        return FAT32_ERROR_IO;
    }
    
    /* Write backup boot sector */
    if (disk_write_sector(g_fat32.disk, 6, boot) != DISK_SUCCESS) {
        vga_writestring("FAT32: WARNING - Failed to write backup boot sector\n");
    }
    
    kfree(boot);
    vga_writestring("FAT32: Boot sector written successfully\n");
    
    /* Initialize FAT tables */
    vga_writestring("FAT32: Initializing FAT tables...\n");
    uint8_t *fat_sector = kzalloc(FAT32_SECTOR_SIZE);
    if (!fat_sector) {
        vga_writestring("FAT32: ERROR - Cannot allocate FAT sector buffer\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* First FAT sector with initial entries */
    uint32_t *fat_entries = (uint32_t*)fat_sector;
    fat_entries[0] = 0x0FFFFF00 | 0xF8; /* Media descriptor */
    fat_entries[1] = 0x0FFFFFFF;        /* End of chain */
    fat_entries[2] = 0x0FFFFFFF;        /* Root directory end of chain */
    
    /* Write first sector of both FATs */
    if (disk_write_sector(g_fat32.disk, reserved_sectors, fat_sector) != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to write first FAT sector\n");
        kfree(fat_sector);
        return FAT32_ERROR_IO;
    }
    
    if (disk_write_sector(g_fat32.disk, reserved_sectors + sectors_per_fat, fat_sector) != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to write second FAT sector\n");
        kfree(fat_sector);
        return FAT32_ERROR_IO;
    }
    
    /* Clear remaining FAT sectors */
    memset(fat_sector, 0, FAT32_SECTOR_SIZE);
    for (uint32_t i = 1; i < sectors_per_fat; i++) {
        disk_write_sector(g_fat32.disk, reserved_sectors + i, fat_sector);
        disk_write_sector(g_fat32.disk, reserved_sectors + sectors_per_fat + i, fat_sector);
    }
    
    kfree(fat_sector);
    vga_writestring("FAT32: FAT tables initialized\n");
    
    /* Initialize root directory */
    vga_writestring("FAT32: Initializing root directory...\n");
    uint8_t *root_cluster = kzalloc(sectors_per_cluster * FAT32_SECTOR_SIZE);
    if (!root_cluster) {
        vga_writestring("FAT32: ERROR - Cannot allocate root directory buffer\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    uint32_t root_sector = reserved_sectors + (2 * sectors_per_fat);
    if (disk_write_sectors(g_fat32.disk, root_sector, sectors_per_cluster, root_cluster) != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to write root directory\n");
        kfree(root_cluster);
        return FAT32_ERROR_IO;
    }
    
    kfree(root_cluster);
    
    /* Flush all changes */
    vga_writestring("FAT32: Flushing changes to disk...\n");
    disk_flush_cache(g_fat32.disk);
    
    vga_writestring("FAT32: === Filesystem creation completed ===\n");
    return FAT32_SUCCESS;
}

static int fat32_read_boot_sector(void) {
    vga_writestring("FAT32: Reading boot sector...\n");
    
    struct fat32_boot_sector boot;
    int read_result = disk_read_sector(g_fat32.disk, 0, &boot);
    
    if (read_result != DISK_SUCCESS) {
        vga_writestring("FAT32: Failed to read boot sector, disk error: ");
        print_dec(read_result);
        vga_putchar('\n');
        return FAT32_ERROR_IO;
    }
    
    vga_writestring("FAT32: Boot sector read, validating...\n");
    
    /* Check boot signature */
    if (boot.boot_sector_signature != 0xAA55) {
        vga_writestring("FAT32: Invalid boot signature: 0x");
        print_hex(boot.boot_sector_signature);
        vga_writestring(" (expected 0xAA55)\n");
        return FAT32_ERROR_INVALID;
    }
    
    /* Check sector size */
    if (boot.bytes_per_sector != FAT32_SECTOR_SIZE) {
        vga_writestring("FAT32: Invalid sector size: ");
        print_dec(boot.bytes_per_sector);
        vga_putchar('\n');
        return FAT32_ERROR_INVALID;
    }
    
    /* Check other parameters */
    if (boot.sectors_per_cluster == 0) {
        vga_writestring("FAT32: Invalid sectors per cluster\n");
        return FAT32_ERROR_INVALID;
    }
    
    if (boot.num_fats == 0) {
        vga_writestring("FAT32: Invalid number of FATs\n");
        return FAT32_ERROR_INVALID;
    }
    
    if (boot.sectors_per_fat_32 == 0) {
        vga_writestring("FAT32: Invalid sectors per FAT\n");
        return FAT32_ERROR_INVALID;
    }
    
    if (boot.root_cluster < 2) {
        vga_writestring("FAT32: Invalid root cluster\n");
        return FAT32_ERROR_INVALID;
    }
    
    /* Store values */
    g_fat32.bytes_per_sector = boot.bytes_per_sector;
    g_fat32.sectors_per_cluster = boot.sectors_per_cluster;
    g_fat32.reserved_sectors = boot.reserved_sectors;
    g_fat32.num_fats = boot.num_fats;
    g_fat32.sectors_per_fat = boot.sectors_per_fat_32;
    g_fat32.root_cluster = boot.root_cluster;
    
    vga_writestring("FAT32: Boot sector validated successfully\n");
    return FAT32_SUCCESS;
}

static int fat32_load_fat_cache(void) {
    vga_writestring("FAT32: Loading FAT cache...\n");
    
    /* Cache first 4 sectors of FAT */
    g_fat32.fat_cache_sectors = 4;
    if (g_fat32.sectors_per_fat < 4) {
        g_fat32.fat_cache_sectors = g_fat32.sectors_per_fat;
    }
    
    uint32_t cache_size = g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE;
    g_fat32.fat_cache = kmalloc(cache_size);
    if (!g_fat32.fat_cache) {
        vga_writestring("FAT32: ERROR - Cannot allocate FAT cache (");
        print_dec(cache_size);
        vga_writestring(" bytes)\n");
        return FAT32_ERROR_NO_MEMORY;
    }
    
    /* Read FAT sectors */
    int read_result = disk_read_sectors(g_fat32.disk, g_fat32.fat_start_sector, 
                                       g_fat32.fat_cache_sectors, g_fat32.fat_cache);
    if (read_result != DISK_SUCCESS) {
        vga_writestring("FAT32: ERROR - Failed to read FAT sectors, disk error: ");
        print_dec(read_result);
        vga_putchar('\n');
        kfree(g_fat32.fat_cache);
        g_fat32.fat_cache = NULL;
        return FAT32_ERROR_IO;
    }
    
    vga_writestring("FAT32: FAT cache loaded (");
    print_dec(g_fat32.fat_cache_sectors);
    vga_writestring(" sectors)\n");
    
    return FAT32_SUCCESS;
}

/* Rest of the functions remain the same as the previous implementation */
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
    
    uint32_t max_cached_cluster = (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4;
    if (cluster < max_cached_cluster) {
        return g_fat32.fat_cache[cluster] & 0x0FFFFFFF;
    }
    
    return FAT32_END_OF_CHAIN;
}

uint32_t fat32_allocate_cluster(void) {
    if (!g_fat32.fat_cache) {
        return 0;
    }
    
    uint32_t max_entries = (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4;
    for (uint32_t i = 3; i < max_entries; i++) {
        if ((g_fat32.fat_cache[i] & 0x0FFFFFFF) == FAT32_FREE_CLUSTER) {
            g_fat32.fat_cache[i] = FAT32_END_OF_CHAIN;
            
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
    if (!g_fat32.fat_cache || start_cluster < 2) {
        return FAT32_ERROR_INVALID;
    }
    
    uint32_t max_entries = (g_fat32.fat_cache_sectors * FAT32_SECTOR_SIZE) / 4;
    if (start_cluster < max_entries) {
        g_fat32.fat_cache[start_cluster] = FAT32_FREE_CLUSTER;
        
        disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector, 
                         g_fat32.fat_cache_sectors, g_fat32.fat_cache);
        disk_write_sectors(g_fat32.disk, g_fat32.fat_start_sector + g_fat32.sectors_per_fat, 
                         g_fat32.fat_cache_sectors, g_fat32.fat_cache);
    }
    
    return FAT32_SUCCESS;
}

void fat32_list_files(void) {
    if (!g_fat32.mounted) {
        vga_writestring("FAT32: Filesystem not mounted\n");
        return;
    }
    
    vga_writestring("Directory listing (root):\n");
    
    uint32_t root_sector = fat32_cluster_to_sector(g_fat32.root_cluster);
    if (root_sector == 0) {
        vga_writestring("FAT32: Invalid root directory cluster\n");
        return;
    }
    
    if (disk_read_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                         g_fat32.cluster_buffer) != DISK_SUCCESS) {
        vga_writestring("FAT32: Failed to read root directory\n");
        return;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    int entry_count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    int file_count = 0;
    
    for (int i = 0; i < entry_count; i++) {
        if (entries[i].name[0] == 0x00) break;
        if (entries[i].name[0] == 0xE5) continue;
        if (entries[i].attributes & FAT32_ATTR_LONG_NAME) continue;
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) continue;
        
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
        file_count++;
    }
    
    if (file_count == 0) {
        vga_writestring("  (empty directory)\n");
    } else {
        vga_writestring("Total files: ");
        print_dec(file_count);
        vga_putchar('\n');
    }
}

int fat32_format_name(const char *filename, char *formatted_name) {
    if (!filename || !formatted_name) {
        return FAT32_ERROR_INVALID;
    }
    
    memset(formatted_name, ' ', 11);
    formatted_name[11] = '\0';
    
    const char *dot = strstr(filename, ".");
    int name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    
    for (int i = 0; i < name_len && i < 8; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        formatted_name[i] = c;
    }
    
    if (dot) {
        for (int i = 0; i < 3 && dot[i + 1] != '\0'; i++) {
            char c = dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 32;
            formatted_name[8 + i] = c;
        }
    }
    
    return FAT32_SUCCESS;
}

int fat32_exists(const char *filename) {
    if (!g_fat32.mounted || !filename) {
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

/* Stub implementations for now */
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
    if (!g_fat32.mounted) {
        vga_writestring("FAT32: Filesystem not mounted\n");
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
    vga_writestring("FAT32 Filesystem Status:\n");
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
        }
    } else {
        vga_writestring("\n  Disk handle: NULL");
    }
    
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