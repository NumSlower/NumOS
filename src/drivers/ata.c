#include "ata.h"
#include "kernel.h"
#include "vga.h"

/* ATA drive information */
static struct ata_drive drives[4] = {0}; // Primary master, slave, secondary master, slave
static int ata_initialized = 0;

/* ATA register offsets */
#define ATA_REG_DATA        0x00
#define ATA_REG_ERROR       0x01
#define ATA_REG_FEATURES    0x01
#define ATA_REG_SECTOR_COUNT 0x02
#define ATA_REG_LBA_LOW     0x03
#define ATA_REG_LBA_MID     0x04
#define ATA_REG_LBA_HIGH    0x05
#define ATA_REG_DRIVE       0x06
#define ATA_REG_STATUS      0x07
#define ATA_REG_COMMAND     0x07

/* ATA commands */
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_IDENTIFY        0xEC

/* ATA status bits */
#define ATA_STATUS_BSY      0x80    // Busy
#define ATA_STATUS_RDY      0x40    // Ready
#define ATA_STATUS_DRQ      0x08    // Data request
#define ATA_STATUS_ERR      0x01    // Error

/* Drive selection bits */
#define ATA_DRIVE_MASTER    0xA0
#define ATA_DRIVE_SLAVE     0xB0

/* Helper functions */
static void ata_wait_busy(uint16_t base);
static void ata_wait_ready(uint16_t base);
static int ata_identify_drive(int drive_num);
static void ata_select_drive(int drive_num);
static int ata_read_sectors_pio(int drive_num, uint32_t lba, uint8_t count, void* buffer);
static int ata_write_sectors_pio(int drive_num, uint32_t lba, uint8_t count, const void* buffer);

int ata_init(void) {
    if (ata_initialized) {
        return 0;
    }
    
    // Initialize drive structures
    for (int i = 0; i < 4; i++) {
        drives[i].exists = 0;
        drives[i].base_port = (i < 2) ? ATA_PRIMARY_BASE : ATA_SECONDARY_BASE;
        drives[i].control_port = (i < 2) ? ATA_PRIMARY_CONTROL : ATA_SECONDARY_CONTROL;
        drives[i].drive_num = i;
        drives[i].is_slave = (i % 2) == 1;
    }
    
    vga_writestring("Initializing ATA/IDE drives...\n");
    
    // Identify all drives
    int drives_found = 0;
    for (int i = 0; i < 4; i++) {
        if (ata_identify_drive(i) == 0) {
            drives_found++;
            vga_writestring("Drive ");
            print_dec(i);
            vga_writestring(": ");
            
            // Print drive model (first 40 characters of model string)
            for (int j = 0; j < 40 && drives[i].model[j]; j++) {
                vga_putchar(drives[i].model[j]);
            }
            vga_writestring(" (");
            print_dec(drives[i].sectors);
            vga_writestring(" sectors, ");
            print_dec((drives[i].sectors * 512) / (1024 * 1024));
            vga_writestring(" MB)\n");
        }
    }
    
    if (drives_found == 0) {
        vga_writestring("No ATA drives found\n");
        return -1;
    }
    
    ata_initialized = 1;
    vga_writestring("ATA driver initialized with ");
    print_dec(drives_found);
    vga_writestring(" drive(s)\n");
    
    return 0;
}

static int ata_identify_drive(int drive_num) {
    if (drive_num < 0 || drive_num >= 4) {
        return -1;
    }
    
    struct ata_drive* drive = &drives[drive_num];
    uint16_t base = drive->base_port;
    
    // Select drive
    ata_select_drive(drive_num);
    
    // Send IDENTIFY command
    outb(base + ATA_REG_SECTOR_COUNT, 0);
    outb(base + ATA_REG_LBA_LOW, 0);
    outb(base + ATA_REG_LBA_MID, 0);
    outb(base + ATA_REG_LBA_HIGH, 0);
    outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    
    // Check if drive exists
    uint8_t status = inb(base + ATA_REG_STATUS);
    if (status == 0) {
        return -1; // Drive doesn't exist
    }
    
    // Wait for BSY to clear
    ata_wait_busy(base);
    
    // Check status again
    status = inb(base + ATA_REG_STATUS);
    if (status & ATA_STATUS_ERR) {
        return -1; // Error occurred
    }
    
    // Wait for DRQ (data ready)
    while (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_DRQ)) {
        // Check for error
        if (inb(base + ATA_REG_STATUS) & ATA_STATUS_ERR) {
            return -1;
        }
    }
    
    // Read identify data
    uint16_t identify_data[256];
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inb(base + ATA_REG_DATA) | (inb(base + ATA_REG_DATA) << 8);
    }
    
    // Extract drive information
    drive->exists = 1;
    drive->sectors = ((uint32_t)identify_data[61] << 16) | identify_data[60];
    
    // Extract model string (words 27-46, byte-swapped)
    for (int i = 0; i < 40; i += 2) {
        uint16_t word = identify_data[27 + i/2];
        drive->model[i] = (word >> 8) & 0xFF;
        drive->model[i+1] = word & 0xFF;
    }
    drive->model[40] = '\0';
    
    // Trim trailing spaces from model string
    for (int i = 39; i >= 0; i--) {
        if (drive->model[i] == ' ' || drive->model[i] == '\0') {
            drive->model[i] = '\0';
        } else {
            break;
        }
    }
    
    return 0;
}

static void ata_select_drive(int drive_num) {
    struct ata_drive* drive = &drives[drive_num];
    uint16_t base = drive->base_port;
    
    uint8_t drive_select = drive->is_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER;
    outb(base + ATA_REG_DRIVE, drive_select);
    
    // Small delay after drive selection
    for (int i = 0; i < 4; i++) {
        inb(base + ATA_REG_STATUS);
    }
}

static void ata_wait_busy(uint16_t base) {
    while (inb(base + ATA_REG_STATUS) & ATA_STATUS_BSY) {
        // Wait for busy bit to clear
    }
}

static void ata_wait_ready(uint16_t base) {
    while (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_RDY)) {
        // Wait for ready bit to set
    }
}

static int ata_read_sectors_pio(int drive_num, uint32_t lba, uint8_t count, void* buffer) {
    if (drive_num < 0 || drive_num >= 4 || !drives[drive_num].exists) {
        return -1;
    }
    
    struct ata_drive* drive = &drives[drive_num];
    uint16_t base = drive->base_port;
    uint8_t* buf = (uint8_t*)buffer;
    
    // Select drive
    ata_select_drive(drive_num);
    
    // Wait for drive to be ready
    ata_wait_ready(base);
    
    // Set up LBA and sector count
    outb(base + ATA_REG_SECTOR_COUNT, count);
    outb(base + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(base + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    
    uint8_t drive_select = (drive->is_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER) | 
                          ((lba >> 24) & 0x0F); // LBA bits 27-24
    outb(base + ATA_REG_DRIVE, drive_select);
    
    // Send read command
    outb(base + ATA_REG_COMMAND, ATA_CMD_READ_SECTORS);
    
    // Read sectors
    for (int sector = 0; sector < count; sector++) {
        // Wait for DRQ (data ready)
        while (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_DRQ)) {
            if (inb(base + ATA_REG_STATUS) & ATA_STATUS_ERR) {
                return -1; // Error occurred
            }
        }
        
        // Read 256 words (512 bytes)
        for (int i = 0; i < 256; i++) {
            uint16_t data = inb(base + ATA_REG_DATA) | (inb(base + ATA_REG_DATA) << 8);
            buf[sector * 512 + i * 2] = data & 0xFF;
            buf[sector * 512 + i * 2 + 1] = (data >> 8) & 0xFF;
        }
    }
    
    return 0;
}

static int ata_write_sectors_pio(int drive_num, uint32_t lba, uint8_t count, const void* buffer) {
    if (drive_num < 0 || drive_num >= 4 || !drives[drive_num].exists) {
        return -1;
    }
    
    struct ata_drive* drive = &drives[drive_num];
    uint16_t base = drive->base_port;
    const uint8_t* buf = (const uint8_t*)buffer;
    
    // Select drive
    ata_select_drive(drive_num);
    
    // Wait for drive to be ready
    ata_wait_ready(base);
    
    // Set up LBA and sector count
    outb(base + ATA_REG_SECTOR_COUNT, count);
    outb(base + ATA_REG_LBA_LOW, lba & 0xFF);
    outb(base + ATA_REG_LBA_MID, (lba >> 8) & 0xFF);
    outb(base + ATA_REG_LBA_HIGH, (lba >> 16) & 0xFF);
    
    uint8_t drive_select = (drive->is_slave ? ATA_DRIVE_SLAVE : ATA_DRIVE_MASTER) | 
                          ((lba >> 24) & 0x0F); // LBA bits 27-24
    outb(base + ATA_REG_DRIVE, drive_select);
    
    // Send write command
    outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_SECTORS);
    
    // Write sectors
    for (int sector = 0; sector < count; sector++) {
        // Wait for DRQ (data ready)
        while (!(inb(base + ATA_REG_STATUS) & ATA_STATUS_DRQ)) {
            if (inb(base + ATA_REG_STATUS) & ATA_STATUS_ERR) {
                return -1; // Error occurred
            }
        }
        
        // Write 256 words (512 bytes)
        for (int i = 0; i < 256; i++) {
            uint16_t data = buf[sector * 512 + i * 2] | 
                           (buf[sector * 512 + i * 2 + 1] << 8);
            outb(base + ATA_REG_DATA, data & 0xFF);
            outb(base + ATA_REG_DATA, (data >> 8) & 0xFF);
        }
    }
    
    return 0;
}

// Public interface functions
int disk_read_sectors(uint32_t drive, uint32_t start_sector, uint32_t count, void *buffer) {
    if (!ata_initialized) {
        return -1;
    }
    
    if (drive >= 4 || !drives[drive].exists) {
        return -1;
    }
    
    // Break large reads into smaller chunks (max 255 sectors at a time)
    uint8_t *buf = (uint8_t*)buffer;
    uint32_t remaining = count;
    uint32_t current_lba = start_sector;
    
    while (remaining > 0) {
        uint8_t chunk_size = (remaining > 255) ? 255 : (uint8_t)remaining;
        
        if (ata_read_sectors_pio(drive, current_lba, chunk_size, buf) != 0) {
            return -1;
        }
        
        remaining -= chunk_size;
        current_lba += chunk_size;
        buf += chunk_size * 512;
    }
    
    return 0;
}

int disk_write_sectors(uint32_t drive, uint32_t start_sector, uint32_t count, const void *buffer) {
    if (!ata_initialized) {
        return -1;
    }
    
    if (drive >= 4 || !drives[drive].exists) {
        return -1;
    }
    
    // Break large writes into smaller chunks (max 255 sectors at a time)
    const uint8_t *buf = (const uint8_t*)buffer;
    uint32_t remaining = count;
    uint32_t current_lba = start_sector;
    
    while (remaining > 0) {
        uint8_t chunk_size = (remaining > 255) ? 255 : (uint8_t)remaining;
        
        if (ata_write_sectors_pio(drive, current_lba, chunk_size, buf) != 0) {
            return -1;
        }
        
        remaining -= chunk_size;
        current_lba += chunk_size;
        buf += chunk_size * 512;
    }
    
    return 0;
}

struct ata_drive* ata_get_drive_info(int drive_num) {
    if (drive_num < 0 || drive_num >= 4 || !drives[drive_num].exists) {
        return NULL;
    }
    
    return &drives[drive_num];
}

void ata_print_drives(void) {
    if (!ata_initialized) {
        vga_writestring("ATA driver not initialized\n");
        return;
    }
    
    vga_writestring("ATA Drive Information:\n");
    
    for (int i = 0; i < 4; i++) {
        if (drives[i].exists) {
            const char* bus_name = (i < 2) ? "Primary" : "Secondary";
            const char* drive_name = (i % 2 == 0) ? "Master" : "Slave";
            
            vga_writestring("  Drive ");
            print_dec(i);
            vga_writestring(" (");
            vga_writestring(bus_name);
            vga_putchar(' ');
            vga_writestring(drive_name);
            vga_writestring("): ");
            vga_writestring(drives[i].model);
            vga_writestring("\n    Sectors: ");
            print_dec(drives[i].sectors);
            vga_writestring(" (");
            print_dec((drives[i].sectors * 512) / (1024 * 1024));
            vga_writestring(" MB)\n");
            vga_writestring("    Base Port: 0x");
            print_hex(drives[i].base_port);
            vga_writestring(", Control Port: 0x");
            print_hex(drives[i].control_port);
            vga_writestring("\n");
        }
    }
}