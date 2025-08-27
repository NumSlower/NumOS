#include "drivers/disk.h"
#include "drivers/vga.h"
#include "kernel.h"
#include "cpu/heap.h"
#include "drivers/timer.h"

/* Global disk state */
static struct disk_handle g_disks[DISK_MAX_DISKS];
static int g_disk_initialized = 0;
static uint8_t *g_disk_images[DISK_MAX_DISKS];
static uint32_t g_disk_image_sizes[DISK_MAX_DISKS];

/* File system simulation for disk images */
#define MAX_DISK_FILES 8
struct disk_file {
    char filename[64];
    uint8_t *data;
    uint32_t size;
    int in_use;
};

static struct disk_file g_disk_files[MAX_DISK_FILES];

/* Forward declarations */
static int disk_image_read_sectors(struct disk_handle *disk, uint32_t start_sector,
                                  uint32_t count, void *buffer);
static int disk_image_write_sectors(struct disk_handle *disk, uint32_t start_sector,
                                   uint32_t count, const void *buffer);
static int disk_image_flush(struct disk_handle *disk);
static int disk_image_identify(struct disk_handle *disk);
static struct disk_cache_entry* disk_find_cache_entry(struct disk_handle *disk, uint32_t sector);
static struct disk_cache_entry* disk_allocate_cache_entry(struct disk_handle *disk, uint32_t sector);

int disk_init(void) {
    if (g_disk_initialized) {
        vga_writestring("Disk: Already initialized\n");
        return DISK_SUCCESS;
    }
    
    /* Initialize disk handles */
    memset(g_disks, 0, sizeof(g_disks));
    memset(g_disk_images, 0, sizeof(g_disk_images));
    memset(g_disk_image_sizes, 0, sizeof(g_disk_image_sizes));
    memset(g_disk_files, 0, sizeof(g_disk_files));
    
    /* Initialize each disk handle */
    for (int i = 0; i < DISK_MAX_DISKS; i++) {
        g_disks[i].disk_id = i;
        g_disks[i].info.disk_id = i;
        g_disks[i].info.disk_type = DISK_TYPE_UNKNOWN;
        g_disks[i].info.status = 0;
        g_disks[i].info.sector_size = DISK_SECTOR_SIZE;
        strcpy(g_disks[i].info.label, "NUMOS_DISK");
        strcpy(g_disks[i].info.serial, "NUM000000000");
        
        /* Initialize cache */
        for (int j = 0; j < DISK_CACHE_SECTORS; j++) {
            g_disks[i].cache[j].data = kmalloc(DISK_SECTOR_SIZE);
            g_disks[i].cache[j].valid = 0;
            g_disks[i].cache[j].dirty = 0;
            g_disks[i].cache[j].sector = 0;
            g_disks[i].cache[j].last_access = 0;
        }
    }
    
    /* Try to detect hardware disks */
    disk_detect_hardware();
    
    /* Create default disk image if it doesn't exist */
    if (disk_create_image(DISK_IMAGE_PATH, DISK_DEFAULT_SIZE) == DISK_SUCCESS) {
        vga_writestring("Disk: Created default disk image (");
        print_dec(DISK_DEFAULT_SIZE / 1024 / 1024);
        vga_writestring("MB)\n");
    }
    
    /* Mount the default disk image */
    if (disk_mount_image(DISK_IMAGE_PATH, 0) == DISK_SUCCESS) {
        vga_writestring("Disk: Mounted default disk image as disk 0\n");
    } else {
        vga_writestring("Disk: Warning - Could not mount default disk image\n");
    }
    
    g_disk_initialized = 1;
    vga_writestring("Disk: Subsystem initialized\n");
    return DISK_SUCCESS;
}

void disk_shutdown(void) {
    if (!g_disk_initialized) {
        return;
    }
    
    /* Flush all caches and close disks */
    for (int i = 0; i < DISK_MAX_DISKS; i++) {
        if (g_disks[i].info.status & DISK_STATUS_READY) {
            disk_flush_cache(&g_disks[i]);
            disk_close(&g_disks[i]);
        }
        
        /* Free cache memory */
        for (int j = 0; j < DISK_CACHE_SECTORS; j++) {
            if (g_disks[i].cache[j].data) {
                kfree(g_disks[i].cache[j].data);
            }
        }
        
        /* Free disk image memory */
        if (g_disk_images[i]) {
            kfree(g_disk_images[i]);
            g_disk_images[i] = NULL;
        }
    }
    
    /* Free file system simulation */
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (g_disk_files[i].in_use && g_disk_files[i].data) {
            kfree(g_disk_files[i].data);
        }
    }
    
    g_disk_initialized = 0;
    vga_writestring("Disk: Subsystem shutdown\n");
}

struct disk_handle* disk_open(uint8_t disk_id) {
    if (!g_disk_initialized || disk_id >= DISK_MAX_DISKS) {
        return NULL;
    }
    
    struct disk_handle *disk = &g_disks[disk_id];
    
    if (!(disk->info.status & DISK_STATUS_READY)) {
        vga_writestring("Disk: Disk not ready\n");
        return NULL;
    }
    
    return disk;
}

int disk_close(struct disk_handle *disk) {
    if (!disk) {
        return DISK_ERROR_INVALID;
    }
    
    /* Flush any cached data */
    disk_flush_cache(disk);
    
    return DISK_SUCCESS;
}

int disk_create_image(const char *filename, uint64_t size_bytes) {
    if (!filename || size_bytes == 0) {
        return DISK_ERROR_INVALID;
    }
    
    /* Check if file already exists in our simulation */
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (g_disk_files[i].in_use && strcmp(g_disk_files[i].filename, filename) == 0) {
            vga_writestring("Disk: Image file already exists\n");
            return DISK_SUCCESS; /* Already exists */
        }
    }
    
    /* Find free file slot */
    int slot = -1;
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (!g_disk_files[i].in_use) {
            slot = i;
            break;
        }
    }
    
    if (slot == -1) {
        vga_writestring("Disk: No free file slots\n");
        return DISK_ERROR_NO_MEMORY;
    }
    
    /* Allocate memory for disk image */
    uint8_t *image_data = kmalloc((size_t)size_bytes);
    if (!image_data) {
        vga_writestring("Disk: Failed to allocate memory for disk image\n");
        return DISK_ERROR_NO_MEMORY;
    }
    
    /* Initialize with zeros */
    memset(image_data, 0, (size_t)size_bytes);
    
    /* Store in file simulation */
    strcpy(g_disk_files[slot].filename, filename);
    g_disk_files[slot].data = image_data;
    g_disk_files[slot].size = (uint32_t)size_bytes;
    g_disk_files[slot].in_use = 1;
    
    return DISK_SUCCESS;
}

int disk_mount_image(const char *filename, uint8_t disk_id) {
    if (!filename || disk_id >= DISK_MAX_DISKS) {
        return DISK_ERROR_INVALID;
    }
    
    /* Find the file in our simulation */
    int file_slot = -1;
    for (int i = 0; i < MAX_DISK_FILES; i++) {
        if (g_disk_files[i].in_use && strcmp(g_disk_files[i].filename, filename) == 0) {
            file_slot = i;
            break;
        }
    }
    
    if (file_slot == -1) {
        vga_writestring("Disk: Image file not found\n");
        return DISK_ERROR_NOT_FOUND;
    }
    
    struct disk_handle *disk = &g_disks[disk_id];
    
    /* Set up disk information */
    disk->info.disk_type = DISK_TYPE_IMAGE;
    disk->info.sector_size = DISK_SECTOR_SIZE;
    disk->info.sector_count = g_disk_files[file_slot].size / DISK_SECTOR_SIZE;
    disk->info.total_size = g_disk_files[file_slot].size;
    disk->info.status = DISK_STATUS_READY | DISK_STATUS_WRITABLE | DISK_STATUS_CACHED;
    
    /* Store reference to image data */
    g_disk_images[disk_id] = g_disk_files[file_slot].data;
    g_disk_image_sizes[disk_id] = g_disk_files[file_slot].size;
    
    /* Set up function pointers */
    disk->read_sectors = disk_image_read_sectors;
    disk->write_sectors = disk_image_write_sectors;
    disk->flush = disk_image_flush;
    disk->identify = disk_image_identify;
    
    return DISK_SUCCESS;
}

int disk_unmount(uint8_t disk_id) {
    if (disk_id >= DISK_MAX_DISKS) {
        return DISK_ERROR_INVALID;
    }
    
    struct disk_handle *disk = &g_disks[disk_id];
    
    /* Flush cache */
    disk_flush_cache(disk);
    
    /* Clear disk information */
    disk->info.status = 0;
    disk->info.disk_type = DISK_TYPE_UNKNOWN;
    
    /* Clear function pointers */
    disk->read_sectors = NULL;
    disk->write_sectors = NULL;
    disk->flush = NULL;
    disk->identify = NULL;
    
    return DISK_SUCCESS;
}

int disk_read_sector(struct disk_handle *disk, uint32_t sector, void *buffer) {
    if (!disk || !buffer) {
        return DISK_ERROR_INVALID;
    }
    
    /* Check cache first */
    struct disk_cache_entry *cache_entry = disk_find_cache_entry(disk, sector);
    if (cache_entry && cache_entry->valid) {
        memcpy(buffer, cache_entry->data, DISK_SECTOR_SIZE);
        cache_entry->last_access = timer_get_ticks();
        return DISK_SUCCESS;
    }
    
    /* Read from disk */
    int result = disk->read_sectors(disk, sector, 1, buffer);
    if (result == DISK_SUCCESS) {
        /* Cache the sector */
        cache_entry = disk_allocate_cache_entry(disk, sector);
        if (cache_entry) {
            memcpy(cache_entry->data, buffer, DISK_SECTOR_SIZE);
            cache_entry->valid = 1;
            cache_entry->dirty = 0;
            cache_entry->last_access = timer_get_ticks();
        }
    }
    
    return result;
}

int disk_write_sector(struct disk_handle *disk, uint32_t sector, const void *buffer) {
    if (!disk || !buffer) {
        return DISK_ERROR_INVALID;
    }
    
    if (!(disk->info.status & DISK_STATUS_WRITABLE)) {
        return DISK_ERROR_READ_ONLY;
    }
    
    /* Update cache */
    struct disk_cache_entry *cache_entry = disk_find_cache_entry(disk, sector);
    if (!cache_entry) {
        cache_entry = disk_allocate_cache_entry(disk, sector);
    }
    
    if (cache_entry) {
        memcpy(cache_entry->data, buffer, DISK_SECTOR_SIZE);
        cache_entry->valid = 1;
        cache_entry->dirty = 1;
        cache_entry->last_access = timer_get_ticks();
    }
    
    /* Write through to disk if caching is disabled */
    if (!(disk->info.status & DISK_STATUS_CACHED)) {
        return disk->write_sectors(disk, sector, 1, buffer);
    }
    
    return DISK_SUCCESS;
}

int disk_read_sectors(struct disk_handle *disk, uint32_t start_sector, 
                     uint32_t count, void *buffer) {
    if (!disk || !buffer || count == 0) {
        return DISK_ERROR_INVALID;
    }
    
    uint8_t *buf = (uint8_t*)buffer;
    
    for (uint32_t i = 0; i < count; i++) {
        int result = disk_read_sector(disk, start_sector + i, buf + (i * DISK_SECTOR_SIZE));
        if (result != DISK_SUCCESS) {
            return result;
        }
    }
    
    return DISK_SUCCESS;
}

int disk_write_sectors(struct disk_handle *disk, uint32_t start_sector, 
                      uint32_t count, const void *buffer) {
    if (!disk || !buffer || count == 0) {
        return DISK_ERROR_INVALID;
    }
    
    const uint8_t *buf = (const uint8_t*)buffer;
    
    for (uint32_t i = 0; i < count; i++) {
        int result = disk_write_sector(disk, start_sector + i, buf + (i * DISK_SECTOR_SIZE));
        if (result != DISK_SUCCESS) {
            return result;
        }
    }
    
    return DISK_SUCCESS;
}

int disk_flush_cache(struct disk_handle *disk) {
    if (!disk) {
        return DISK_ERROR_INVALID;
    }
    
    int errors = 0;
    
    /* Write all dirty cache entries */
    for (int i = 0; i < DISK_CACHE_SECTORS; i++) {
        struct disk_cache_entry *entry = &disk->cache[i];
        if (entry->valid && entry->dirty) {
            int result = disk->write_sectors(disk, entry->sector, 1, entry->data);
            if (result == DISK_SUCCESS) {
                entry->dirty = 0;
            } else {
                errors++;
            }
        }
    }
    
    /* Call disk-specific flush */
    if (disk->flush) {
        disk->flush(disk);
    }
    
    return (errors > 0) ? DISK_ERROR_IO : DISK_SUCCESS;
}

int disk_invalidate_cache(struct disk_handle *disk) {
    if (!disk) {
        return DISK_ERROR_INVALID;
    }
    
    /* Flush dirty entries first */
    disk_flush_cache(disk);
    
    /* Invalidate all cache entries */
    for (int i = 0; i < DISK_CACHE_SECTORS; i++) {
        disk->cache[i].valid = 0;
        disk->cache[i].dirty = 0;
    }
    
    return DISK_SUCCESS;
}

void disk_enable_cache(struct disk_handle *disk, int enable) {
    if (!disk) {
        return;
    }
    
    if (enable) {
        disk->info.status |= DISK_STATUS_CACHED;
    } else {
        /* Flush cache before disabling */
        disk_flush_cache(disk);
        disk->info.status &= ~DISK_STATUS_CACHED;
    }
}

struct disk_info* disk_get_info(uint8_t disk_id) {
    if (disk_id >= DISK_MAX_DISKS) {
        return NULL;
    }
    
    return &g_disks[disk_id].info;
}

int disk_get_sector_size(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    if (!info) {
        return -1;  // Return -1 as int, not mixed with uint32_t
    }
    return (int)info->sector_size;  // Explicit cast to int
}

uint32_t disk_get_sector_count(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    return info ? info->sector_count : 0;
}

uint64_t disk_get_size(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    return info ? info->total_size : 0;
}

int disk_is_ready(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    return info ? (info->status & DISK_STATUS_READY) != 0 : 0;
}

void disk_print_info(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    if (!info) {
        vga_writestring("Invalid disk ID\n");
        return;
    }
    
    vga_writestring("Disk ");
    print_dec(disk_id);
    vga_writestring(" Information:\n");
    vga_writestring("  Type: ");
    
    switch (info->disk_type) {
        case DISK_TYPE_UNKNOWN: vga_writestring("Unknown"); break;
        case DISK_TYPE_FLOPPY: vga_writestring("Floppy"); break;
        case DISK_TYPE_HDD: vga_writestring("Hard Disk"); break;
        case DISK_TYPE_SSD: vga_writestring("SSD"); break;
        case DISK_TYPE_CDROM: vga_writestring("CD-ROM"); break;
        case DISK_TYPE_IMAGE: vga_writestring("Disk Image"); break;
        default: vga_writestring("Invalid"); break;
    }
    
    vga_writestring("\n  Status: ");
    if (info->status & DISK_STATUS_READY) vga_writestring("Ready ");
    if (info->status & DISK_STATUS_MOUNTED) vga_writestring("Mounted ");
    if (info->status & DISK_STATUS_WRITABLE) vga_writestring("Writable ");
    if (info->status & DISK_STATUS_CACHED) vga_writestring("Cached ");
    if (info->status & DISK_STATUS_ERROR) vga_writestring("Error ");
    
    vga_writestring("\n  Size: ");
    print_dec(info->total_size / 1024 / 1024);
    vga_writestring(" MB (");
    print_dec(info->sector_count);
    vga_writestring(" sectors)\n");
    
    vga_writestring("  Sector size: ");
    print_dec(info->sector_size);
    vga_writestring(" bytes\n");
    
    vga_writestring("  Label: ");
    vga_writestring(info->label);
    vga_writestring("\n  Serial: ");
    vga_writestring(info->serial);
    vga_putchar('\n');
}

void disk_list_disks(void) {
    vga_writestring("Available Disks:\n");
    vga_writestring("ID Type     Status   Size     Label\n");
    vga_writestring("-- -------- -------- -------- --------\n");
    
    for (int i = 0; i < DISK_MAX_DISKS; i++) {
        struct disk_info *info = &g_disks[i].info;
        
        print_dec(i);
        vga_writestring("  ");
        
        switch (info->disk_type) {
            case DISK_TYPE_UNKNOWN: vga_writestring("Unknown "); break;
            case DISK_TYPE_IMAGE: vga_writestring("Image   "); break;
            case DISK_TYPE_HDD: vga_writestring("HDD     "); break;
            case DISK_TYPE_SSD: vga_writestring("SSD     "); break;
            default: vga_writestring("Other   "); break;
        }
        
        if (info->status & DISK_STATUS_READY) {
            vga_writestring("Ready    ");
            print_dec(info->total_size / 1024 / 1024);
            vga_writestring("MB     ");
            vga_writestring(info->label);
        } else {
            vga_writestring("Not Ready");
        }
        
        vga_putchar('\n');
    }
}

/* Internal helper functions */
static int disk_image_read_sectors(struct disk_handle *disk, uint32_t start_sector,
                                  uint32_t count, void *buffer) {
    if (!disk || !buffer) {
        return DISK_ERROR_INVALID;
    }
    
    uint8_t *image = g_disk_images[disk->disk_id];
    if (!image) {
        return DISK_ERROR_NOT_READY;
    }
    
    uint32_t offset = start_sector * DISK_SECTOR_SIZE;
    uint32_t size = count * DISK_SECTOR_SIZE;
    
    if (offset + size > g_disk_image_sizes[disk->disk_id]) {
        return DISK_ERROR_INVALID;
    }
    
    memcpy(buffer, image + offset, size);
    return DISK_SUCCESS;
}

static int disk_image_write_sectors(struct disk_handle *disk, uint32_t start_sector,
                                   uint32_t count, const void *buffer) {
    if (!disk || !buffer) {
        return DISK_ERROR_INVALID;
    }
    
    uint8_t *image = g_disk_images[disk->disk_id];
    if (!image) {
        return DISK_ERROR_NOT_READY;
    }
    
    uint32_t offset = start_sector * DISK_SECTOR_SIZE;
    uint32_t size = count * DISK_SECTOR_SIZE;
    
    if (offset + size > g_disk_image_sizes[disk->disk_id]) {
        return DISK_ERROR_INVALID;
    }
    
    memcpy(image + offset, buffer, size);
    return DISK_SUCCESS;
}

static int disk_image_flush(struct disk_handle *disk) {
    /* For memory-based images, data is already "flushed" */
    (void)disk;
    return DISK_SUCCESS;
}

static int disk_image_identify(struct disk_handle *disk) {
    if (!disk) {
        return DISK_ERROR_INVALID;
    }
    
    /* Image is already identified during mount */
    return DISK_SUCCESS;
}

static struct disk_cache_entry* disk_find_cache_entry(struct disk_handle *disk, uint32_t sector) {
    if (!disk) {
        return NULL;
    }
    
    for (int i = 0; i < DISK_CACHE_SECTORS; i++) {
        struct disk_cache_entry *entry = &disk->cache[i];
        if (entry->valid && entry->sector == sector) {
            return entry;
        }
    }
    
    return NULL;
}

static struct disk_cache_entry* disk_allocate_cache_entry(struct disk_handle *disk, uint32_t sector) {
    if (!disk) {
        return NULL;
    }
    
    /* Find empty slot first */
    for (int i = 0; i < DISK_CACHE_SECTORS; i++) {
        struct disk_cache_entry *entry = &disk->cache[i];
        if (!entry->valid) {
            entry->sector = sector;
            return entry;
        }
    }
    
    /* Find least recently used slot */
    struct disk_cache_entry *lru = &disk->cache[0];
    for (int i = 1; i < DISK_CACHE_SECTORS; i++) {
        struct disk_cache_entry *entry = &disk->cache[i];
        if (entry->last_access < lru->last_access) {
            lru = entry;
        }
    }
    
    /* Flush if dirty */
    if (lru->dirty) {
        disk->write_sectors(disk, lru->sector, 1, lru->data);
    }
    
    lru->sector = sector;
    lru->valid = 0;
    lru->dirty = 0;
    return lru;
}

int disk_detect_hardware(void) {
    /* In a real implementation, this would probe for ATA/SATA/NVMe devices */
    /* For now, we'll just indicate no hardware disks found */
    vga_writestring("Disk: Hardware detection - no physical disks detected\n");
    return DISK_SUCCESS;
}

int disk_initialize_ata(void) {
    /* ATA initialization would go here */
    return DISK_SUCCESS;
}

void disk_print_cache_stats(uint8_t disk_id) {
    if (disk_id >= DISK_MAX_DISKS) {
        vga_writestring("Invalid disk ID\n");
        return;
    }
    
    struct disk_handle *disk = &g_disks[disk_id];
    int valid_entries = 0;
    int dirty_entries = 0;
    
    for (int i = 0; i < DISK_CACHE_SECTORS; i++) {
        if (disk->cache[i].valid) {
            valid_entries++;
            if (disk->cache[i].dirty) {
                dirty_entries++;
            }
        }
    }
    
    vga_writestring("Cache Statistics for Disk ");
    print_dec(disk_id);
    vga_writestring(":\n");
    vga_writestring("  Cache entries: ");
    print_dec(valid_entries);
    vga_writestring("/");
    print_dec(DISK_CACHE_SECTORS);
    vga_writestring("\n  Dirty entries: ");
    print_dec(dirty_entries);
    vga_writestring("\n  Cache status: ");
    if (disk->info.status & DISK_STATUS_CACHED) {
        vga_writestring("Enabled");
    } else {
        vga_writestring("Disabled");
    }
    vga_putchar('\n');
}

int disk_validate_sector(struct disk_handle *disk, uint32_t sector) {
    if (!disk) {
        return DISK_ERROR_INVALID;
    }
    
    if (sector >= disk->info.sector_count) {
        return DISK_ERROR_INVALID;
    }
    
    return DISK_SUCCESS;
}

int disk_validate_range(struct disk_handle *disk, uint32_t start_sector, uint32_t count) {
    if (!disk || count == 0) {
        return DISK_ERROR_INVALID;
    }
    
    if (start_sector >= disk->info.sector_count) {
        return DISK_ERROR_INVALID;
    }
    
    if (start_sector + count > disk->info.sector_count) {
        return DISK_ERROR_INVALID;
    }
    
    return DISK_SUCCESS;
}