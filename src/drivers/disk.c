/* 
 * disk.c - ATA/IDE disk driver with real hardware I/O
 * Provides persistent storage through QEMU disk images
 */
#include "drivers/disk.h"
#include "drivers/vga.h"
#include "kernel/kernel.h"
#include "cpu/heap.h"
#include "drivers/timer.h"

/* ATA I/O ports - Primary IDE controller */
#define ATA_PRIMARY_IO       0x1F0
#define ATA_PRIMARY_CONTROL  0x3F6
#define ATA_SECONDARY_IO     0x170
#define ATA_SECONDARY_CONTROL 0x376

/* ATA register offsets from base I/O port */
#define ATA_REG_DATA         0
#define ATA_REG_ERROR        1
#define ATA_REG_FEATURES     1
#define ATA_REG_SECCOUNT     2
#define ATA_REG_LBA_LO       3
#define ATA_REG_LBA_MID      4
#define ATA_REG_LBA_HI       5
#define ATA_REG_DRIVE        6
#define ATA_REG_STATUS       7
#define ATA_REG_COMMAND      7

/* ATA commands */
#define ATA_CMD_READ_PIO     0x20
#define ATA_CMD_WRITE_PIO    0x30
#define ATA_CMD_IDENTIFY     0xEC
#define ATA_CMD_FLUSH        0xE7

/* ATA status register bits */
#define ATA_SR_BSY   0x80    /* Busy */
#define ATA_SR_DRDY  0x40    /* Drive ready */
#define ATA_SR_DRQ   0x08    /* Data request ready */
#define ATA_SR_ERR   0x01    /* Error */

/* Global disk state */
static struct disk_handle g_disks[DISK_MAX_DISKS];
static int g_disk_initialized = 0;

/* Forward declarations for ATA functions */
static void ata_wait_bsy(uint16_t io_base);
static void ata_wait_drq(uint16_t io_base);
static int ata_detect_drive(uint16_t io_base, int is_slave);
static int ata_read_sectors_pio(struct disk_handle *disk, uint32_t start_sector,
                                uint32_t count, void *buffer);
static int ata_write_sectors_pio(struct disk_handle *disk, uint32_t start_sector,
                                 uint32_t count, const void *buffer);

/* Cache helper functions */
static struct disk_cache_entry* disk_find_cache_entry(struct disk_handle *disk, uint32_t sector);
static struct disk_cache_entry* disk_allocate_cache_entry(struct disk_handle *disk, uint32_t sector);

int disk_init(void) {
    vga_writestring("Disk: === Starting disk subsystem initialization ===\n");
    
    if (g_disk_initialized) {
        vga_writestring("Disk: Already initialized\n");
        return DISK_SUCCESS;
    }
    
    /* Initialize disk handles */
    vga_writestring("Disk: Initializing disk handles...\n");
    memset(g_disks, 0, sizeof(g_disks));
    
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
            if (!g_disks[i].cache[j].data) {
                vga_writestring("Disk: ERROR - Failed to allocate cache for disk ");
                print_dec(i);
                vga_writestring(", sector ");
                print_dec(j);
                vga_putchar('\n');
                return DISK_ERROR_NO_MEMORY;
            }
            g_disks[i].cache[j].valid = 0;
            g_disks[i].cache[j].dirty = 0;
            g_disks[i].cache[j].sector = 0;
            g_disks[i].cache[j].last_access = 0;
        }
    }
    vga_writestring("Disk: All disk handles initialized\n");
    
    /* Detect hardware disks */
    vga_writestring("Disk: Detecting hardware...\n");
    disk_detect_hardware();
    
    g_disk_initialized = 1;
    vga_writestring("Disk: === Subsystem initialized successfully ===\n");
    return DISK_SUCCESS;
}

int disk_detect_hardware(void) {
    vga_writestring("Disk: Probing ATA buses...\n");
    
    /* Try simple detection first */
    vga_writestring("Disk: Checking primary master...\n");
    
    if (ata_detect_drive(ATA_PRIMARY_IO, 0)) {
        vga_writestring("Disk: Found primary master (disk 0)\n");
        
        struct disk_handle *disk = &g_disks[0];
        disk->info.disk_type = DISK_TYPE_HDD;
        disk->info.status = DISK_STATUS_READY | DISK_STATUS_WRITABLE | DISK_STATUS_CACHED;
        
        /* Set function pointers */
        disk->read_sectors = ata_read_sectors_pio;
        disk->write_sectors = ata_write_sectors_pio;
        
        /* Set disk size - we'll use a reasonable default */
        disk->info.sector_count = 131072;  /* 64MB */
        disk->info.total_size = (uint64_t)disk->info.sector_count * DISK_SECTOR_SIZE;
        
        vga_writestring("Disk: Primary master configured (");
        print_dec(disk->info.total_size / 1024 / 1024);
        vga_writestring("MB)\n");
    } else {
        vga_writestring("Disk: No primary master found\n");
        vga_writestring("Disk: WARNING - No disk available!\n");
    }
    
    vga_writestring("Disk: Hardware detection complete\n");
    return DISK_SUCCESS;
}

/* ATA Helper Functions */

static void ata_wait_bsy(uint16_t io_base) {
    /* Wait for busy flag to clear */
    while (inb(io_base + ATA_REG_STATUS) & ATA_SR_BSY) {
        /* Busy-wait */
    }
}

static void ata_wait_drq(uint16_t io_base) {
    /* Wait for data request flag to be set */
    while (!(inb(io_base + ATA_REG_STATUS) & ATA_SR_DRQ)) {
        /* Busy-wait */
    }
}

static int ata_detect_drive(uint16_t io_base, int is_slave) {
    /* Select drive */
    outb(io_base + ATA_REG_DRIVE, 0xA0 | (is_slave ? 0x10 : 0));
    
    /* Wait 400ns for drive selection (read status 4 times) */
    for (int i = 0; i < 4; i++) {
        inb(io_base + ATA_REG_STATUS);
    }
    
    /* Additional delay */
    for (volatile int i = 0; i < 10000; i++);
    
    /* Check if a drive exists by reading status */
    uint8_t status = inb(io_base + ATA_REG_STATUS);
    
    /* 0xFF or 0x00 means no drive */
    if (status == 0xFF || status == 0x00) {
        return 0;
    }
    
    /* Clear error register */
    inb(io_base + ATA_REG_ERROR);
    
    /* Set sector count and LBA registers to known values */
    outb(io_base + ATA_REG_SECCOUNT, 0);
    outb(io_base + ATA_REG_LBA_LO, 0);
    outb(io_base + ATA_REG_LBA_MID, 0);
    outb(io_base + ATA_REG_LBA_HI, 0);
    
    /* Send IDENTIFY command */
    outb(io_base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    /* Read status immediately */
    status = inb(io_base + ATA_REG_STATUS);
    
    /* If status is 0, no drive */
    if (status == 0) {
        return 0;
    }
    
    /* Wait for BSY to clear with timeout */
    int timeout = 10000;
    while (timeout-- > 0) {
        status = inb(io_base + ATA_REG_STATUS);
        
        /* Check if BSY cleared */
        if (!(status & ATA_SR_BSY)) {
            break;
        }
        
        /* Small delay */
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout <= 0) {
        vga_writestring("Disk: Timeout waiting for BSY to clear\n");
        return 0;
    }
    
    /* Check for errors */
    uint8_t lba_mid = inb(io_base + ATA_REG_LBA_MID);
    uint8_t lba_hi = inb(io_base + ATA_REG_LBA_HI);
    
    /* Check if it's ATAPI or SATA (we only support PATA for now) */
    if (lba_mid != 0 || lba_hi != 0) {
        vga_writestring("Disk: Non-PATA device detected (ATAPI/SATA), skipping\n");
        return 0;
    }
    
    /* Wait for DRQ or ERR */
    timeout = 10000;
    while (timeout-- > 0) {
        status = inb(io_base + ATA_REG_STATUS);
        
        if (status & ATA_SR_ERR) {
            vga_writestring("Disk: Error during IDENTIFY\n");
            return 0;
        }
        
        if (status & ATA_SR_DRQ) {
            break;
        }
        
        for (volatile int i = 0; i < 100; i++);
    }
    
    if (timeout <= 0) {
        vga_writestring("Disk: Timeout waiting for DRQ\n");
        return 0;
    }
    
    /* Drive detected successfully */
    return 1;
}

static int ata_read_sectors_pio(struct disk_handle *disk, uint32_t start_sector, uint32_t count, void *buffer) {
    if (!disk || !buffer || count == 0) {
        return DISK_ERROR_INVALID;
    }
    
    /* Determine I/O base and slave bit */
    uint16_t io_base = ATA_PRIMARY_IO;
    int is_slave = 0;
    
    if (disk->disk_id == 1) is_slave = 1;
    else if (disk->disk_id >= 2) io_base = ATA_SECONDARY_IO;
    
    uint16_t *buf = (uint16_t*)buffer;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t lba = start_sector + i;
        
        /* Wait for drive to be ready */
        ata_wait_bsy(io_base);
        
        /* Select drive and set LBA mode (bits 7-5 = 111b for LBA, bit 4 = slave) */
        outb(io_base + ATA_REG_DRIVE, 0xE0 | (is_slave ? 0x10 : 0) | ((lba >> 24) & 0x0F));
        
        /* Set sector count (reading 1 sector at a time) */
        outb(io_base + ATA_REG_SECCOUNT, 1);
        
        /* Set LBA address (28-bit) */
        outb(io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        
        /* Send read command */
        outb(io_base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);
        
        /* Wait for data to be ready */
        ata_wait_drq(io_base);
        
        /* Read 256 words (512 bytes) */
        for (int j = 0; j < 256; j++) {
            buf[i * 256 + j] = inw(io_base + ATA_REG_DATA);
        }
        
        /* 400ns delay after each sector */
        inb(io_base + ATA_REG_STATUS);
        inb(io_base + ATA_REG_STATUS);
        inb(io_base + ATA_REG_STATUS);
        inb(io_base + ATA_REG_STATUS);
    }
    
    return DISK_SUCCESS;
}

static int ata_write_sectors_pio(struct disk_handle *disk, uint32_t start_sector,
                                 uint32_t count, const void *buffer) {
    if (!disk || !buffer || count == 0) {
        return DISK_ERROR_INVALID;
    }
    
    uint16_t io_base = ATA_PRIMARY_IO;
    int is_slave = 0;
    
    if (disk->disk_id == 1) is_slave = 1;
    else if (disk->disk_id >= 2) io_base = ATA_SECONDARY_IO;
    
    const uint16_t *buf = (const uint16_t*)buffer;
    
    for (uint32_t i = 0; i < count; i++) {
        uint32_t lba = start_sector + i;
        
        /* Wait for drive ready */
        ata_wait_bsy(io_base);
        
        /* Select drive */
        outb(io_base + ATA_REG_DRIVE, 0xE0 | (is_slave ? 0x10 : 0) | ((lba >> 24) & 0x0F));
        
        /* 400ns delay */
        for (int j = 0; j < 4; j++) {
            inb(io_base + ATA_REG_STATUS);
        }
        
        /* Set sector count */
        outb(io_base + ATA_REG_SECCOUNT, 1);
        
        /* Set LBA */
        outb(io_base + ATA_REG_LBA_LO, lba & 0xFF);
        outb(io_base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
        outb(io_base + ATA_REG_LBA_HI, (lba >> 16) & 0xFF);
        
        /* Send write command */
        outb(io_base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);
        
        /* Wait for DRQ */
        ata_wait_drq(io_base);
        
        /* Write 256 words */
        for (int j = 0; j < 256; j++) {
            outw(io_base + ATA_REG_DATA, buf[i * 256 + j]);
        }
        
        /* CRITICAL: Flush cache after EACH sector write */
        ata_wait_bsy(io_base);
        outb(io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
        ata_wait_bsy(io_base);
        
        /* Check for errors */
        uint8_t status = inb(io_base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) {
            vga_writestring("Disk: Write error at sector ");
            print_dec(lba);
            vga_putchar('\n');
            return DISK_ERROR_IO;
        }
        
        /* Delay */
        for (int j = 0; j < 4; j++) {
            inb(io_base + ATA_REG_STATUS);
        }
    }
    
    return DISK_SUCCESS;
}

/* Disk API Functions */

struct disk_handle* disk_open(uint8_t disk_id) {
    if (!g_disk_initialized || disk_id >= DISK_MAX_DISKS) {
        vga_writestring("Disk: Open failed - invalid parameters (init: ");
        vga_writestring(g_disk_initialized ? "yes" : "no");
        vga_writestring(", id: ");
        print_dec(disk_id);
        vga_writestring(")\n");
        return NULL;
    }
    
    struct disk_handle *disk = &g_disks[disk_id];
    
    if (!(disk->info.status & DISK_STATUS_READY)) {
        vga_writestring("Disk: Open failed - disk ");
        print_dec(disk_id);
        vga_writestring(" not ready (status: 0x");
        print_hex32(disk->info.status);
        vga_writestring(")\n");
        return NULL;
    }
    
    vga_writestring("Disk: Successfully opened disk ");
    print_dec(disk_id);
    vga_putchar('\n');
    return disk;
}

int disk_close(struct disk_handle *disk) {
    if (!disk) {
        return DISK_ERROR_INVALID;
    }
    
    vga_writestring("Disk: Closing disk ");
    print_dec(disk->disk_id);
    vga_writestring("...\n");
    
    /* Flush any cached data */
    int flush_result = disk_flush_cache(disk);
    if (flush_result != DISK_SUCCESS) {
        vga_writestring("Disk: Warning - flush failed during close\n");
    }
    
    vga_writestring("Disk: Disk ");
    print_dec(disk->disk_id);
    vga_writestring(" closed\n");
    return DISK_SUCCESS;
}

int disk_read_sector(struct disk_handle *disk, uint32_t sector, void *buffer) {
    if (!disk || !buffer) {
        return DISK_ERROR_INVALID;
    }
    
    if (!(disk->info.status & DISK_STATUS_READY)) {
        return DISK_ERROR_NOT_READY;
    }
    
    if (sector >= disk->info.sector_count) {
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
    int result = DISK_ERROR_IO;
    if (disk->read_sectors) {
        result = disk->read_sectors(disk, sector, 1, buffer);
    }
    
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
    
    if (!(disk->info.status & DISK_STATUS_READY)) {
        return DISK_ERROR_NOT_READY;
    }
    
    if (!(disk->info.status & DISK_STATUS_WRITABLE)) {
        return DISK_ERROR_READ_ONLY;
    }
    
    if (sector >= disk->info.sector_count) {
        return DISK_ERROR_INVALID;
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
    
    /* Write through to disk immediately for reliability */
    if (disk->write_sectors) {
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
    int flushed = 0;
    
    /* Write all dirty cache entries */
    for (int i = 0; i < DISK_CACHE_SECTORS; i++) {
        struct disk_cache_entry *entry = &disk->cache[i];
        if (entry->valid && entry->dirty) {
            if (disk->write_sectors) {
                int result = disk->write_sectors(disk, entry->sector, 1, entry->data);
                if (result == DISK_SUCCESS) {
                    entry->dirty = 0;
                    flushed++;
                } else {
                    errors++;
                }
            }
        }
    }
    
    if (flushed > 0) {
        vga_writestring("Disk: Flushed ");
        print_dec(flushed);
        vga_writestring(" dirty sectors");
        if (errors > 0) {
            vga_writestring(" (");
            print_dec(errors);
            vga_writestring(" errors)");
        }
        vga_putchar('\n');
    }
    
    return (errors > 0) ? DISK_ERROR_IO : DISK_SUCCESS;
}

void disk_shutdown(void) {
    if (!g_disk_initialized) {
        return;
    }
    
    vga_writestring("Disk: === Starting safe shutdown ===\n");
    
    /* Flush all disk caches */
    for (int i = 0; i < DISK_MAX_DISKS; i++) {
        if (g_disks[i].info.status & DISK_STATUS_READY) {
            vga_writestring("Disk: Flushing disk ");
            print_dec(i);
            vga_writestring("...\n");
            
            /* Flush software cache */
            disk_flush_cache(&g_disks[i]);
            
            /* Flush hardware cache */
            uint16_t io_base = (i < 2) ? ATA_PRIMARY_IO : ATA_SECONDARY_IO;
            int is_slave = (i % 2);
            
            /* Select drive */
            outb(io_base + ATA_REG_DRIVE, 0xA0 | (is_slave ? 0x10 : 0));
            timer_sleep(10);
            
            /* Send flush command */
            outb(io_base + ATA_REG_COMMAND, ATA_CMD_FLUSH);
            
            /* Wait for completion */
            int timeout = 1000;
            while ((inb(io_base + ATA_REG_STATUS) & ATA_SR_BSY) && timeout--) {
                timer_sleep(1);
            }
            
            if (timeout > 0) {
                vga_writestring("Disk: Hardware cache flushed\n");
            } else {
                vga_writestring("Disk: WARNING - Hardware flush timeout\n");
            }
        }
        
        /* Free cache memory */
        for (int j = 0; j < DISK_CACHE_SECTORS; j++) {
            if (g_disks[i].cache[j].data) {
                kfree(g_disks[i].cache[j].data);
                g_disks[i].cache[j].data = NULL;
            }
        }
    }
    
    /* Extra safety delay */
    vga_writestring("Disk: Waiting for all writes to complete...\n");
    timer_sleep(500);
    
    g_disk_initialized = 0;
    vga_writestring("Disk: === Shutdown completed safely ===\n");
}

/* Cache Helper Functions */

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
    if (lru->dirty && disk->write_sectors) {
        disk->write_sectors(disk, lru->sector, 1, lru->data);
    }
    
    lru->sector = sector;
    lru->valid = 0;
    lru->dirty = 0;
    return lru;
}

/* Utility Functions */

int disk_is_ready(uint8_t disk_id) {
    if (disk_id >= DISK_MAX_DISKS) return 0;
    return (g_disks[disk_id].info.status & DISK_STATUS_READY) != 0;
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
        return -1;
    }
    return (int)info->sector_size;
}

uint32_t disk_get_sector_count(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    return info ? info->sector_count : 0;
}

uint64_t disk_get_size(uint8_t disk_id) {
    struct disk_info *info = disk_get_info(disk_id);
    return info ? info->total_size : 0;
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
            case DISK_TYPE_HDD: vga_writestring("HDD     "); break;
            case DISK_TYPE_SSD: vga_writestring("SSD     "); break;
            case DISK_TYPE_IMAGE: vga_writestring("Image   "); break;
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
        vga_writestring("Disk: Cache enabled for disk ");
    } else {
        /* Flush cache before disabling */
        disk_flush_cache(disk);
        disk->info.status &= ~DISK_STATUS_CACHED;
        vga_writestring("Disk: Cache disabled for disk ");
    }
    print_dec(disk->disk_id);
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

/* Stub implementations for compatibility */
int disk_initialize_ata(void) {
    /* Already handled by disk_detect_hardware */
    return DISK_SUCCESS;
}

int disk_create_image(const char *filename, uint64_t size_bytes) {
    /* Not applicable for hardware disks */
    (void)filename;
    (void)size_bytes;
    return DISK_ERROR_INVALID;
}

int disk_mount_image(const char *filename, uint8_t disk_id) {
    /* Not applicable for hardware disks */
    (void)filename;
    (void)disk_id;
    return DISK_ERROR_INVALID;
}

int disk_unmount(uint8_t disk_id) {
    if (disk_id >= DISK_MAX_DISKS) {
        return DISK_ERROR_INVALID;
    }
    
    struct disk_handle *disk = &g_disks[disk_id];
    
    /* Flush cache */
    disk_flush_cache(disk);
    
    vga_writestring("Disk: Disk ");
    print_dec(disk_id);
    vga_writestring(" unmounted\n");
    
    return DISK_SUCCESS;
}
