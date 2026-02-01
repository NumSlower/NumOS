/*
 * ata.c - ATA/IDE Hard Disk Driver
 * Implements PIO mode disk I/O for FAT32 filesystem
 */

#include "drivers/ata.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "kernel/kernel.h"

/* Global ATA devices */
struct ata_device ata_primary_master = {0};
struct ata_device ata_primary_slave = {0};

/*
 * Wait for status register to match mask/value with timeout
 */
uint8_t ata_status_wait(struct ata_device *dev, uint8_t mask, uint8_t value, int timeout_ms) {
    uint64_t start = timer_get_uptime_ms();
    uint8_t status;
    
    while (1) {
        status = inb(dev->base + 7);  /* Read status register */
        
        if ((status & mask) == value) {
            return status;
        }
        
        if (timer_get_uptime_ms() - start > (uint64_t)timeout_ms) {
            return status;  /* Timeout */
        }
        
        /* Small delay to avoid excessive polling */
        for (volatile int i = 0; i < 100; i++);
    }
}

/*
 * Wait for drive to be ready (not busy, ready)
 */
int ata_wait_ready(struct ata_device *dev) {
    uint8_t status = ata_status_wait(dev, ATA_STATUS_BSY | ATA_STATUS_DRDY, 
                                      ATA_STATUS_DRDY, 5000);
    
    if (status & ATA_STATUS_BSY) {
        return -1;  /* Timeout while busy */
    }
    
    if (!(status & ATA_STATUS_DRDY)) {
        return -1;  /* Not ready */
    }
    
    return 0;
}

/*
 * Wait for data request (DRQ) to be set
 */
int ata_wait_drq(struct ata_device *dev) {
    uint8_t status = ata_status_wait(dev, ATA_STATUS_BSY | ATA_STATUS_DRQ,
                                      ATA_STATUS_DRQ, 5000);
    
    if (status & ATA_STATUS_BSY) {
        return -1;  /* Still busy */
    }
    
    if (!(status & ATA_STATUS_DRQ)) {
        return -1;  /* DRQ not set */
    }
    
    return 0;
}

/*
 * 400ns delay by reading alternate status register 4 times
 */
void ata_400ns_delay(struct ata_device *dev) {
    for (int i = 0; i < 4; i++) {
        inb(dev->ctrl);
    }
}

/*
 * Select drive (master or slave)
 */
void ata_select_drive(struct ata_device *dev) {
    uint8_t drive_select = dev->is_master ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(dev->base + 6, drive_select);
    ata_400ns_delay(dev);
}

/*
 * Identify ATA device
 */
int ata_identify(struct ata_device *dev) {
    uint16_t identify_data[256] = {0};
    
    /* Select drive */
    ata_select_drive(dev);
    
    /* Set sector count and LBA to 0 */
    outb(dev->base + 2, 0);
    outb(dev->base + 3, 0);
    outb(dev->base + 4, 0);
    outb(dev->base + 5, 0);
    
    /* Send IDENTIFY command */
    outb(dev->base + 7, ATA_CMD_IDENTIFY);
    
    /* ATA spec: drive requires >= 400ns to assert BSY after receiving
     * a command.  Reading status before that window closes can return
     * the previous (stale) value.  Four reads of the alternate-status
     * register (ctrl port) each take ~100ns on real hardware; QEMU
     * honours the same timing model. */
    ata_400ns_delay(dev);
    
    /* Read status */
    uint8_t status = inb(dev->base + 7);
    
    if (status == 0) {
        return -1;  /* No device */
    }
    
    /* Wait for BSY to clear */
    if (ata_wait_ready(dev) != 0) {
        return -1;
    }
    
    /* Check for errors */
    status = inb(dev->base + 7);
    if (status & ATA_STATUS_ERR) {
        return -1;
    }
    
    /* Wait for DRQ */
    if (ata_wait_drq(dev) != 0) {
        return -1;
    }
    
    /* Read identify data (256 words) */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(dev->base + 0);
    }
    
    /* Extract information */
    struct ata_identify *id = (struct ata_identify*)identify_data;
    
    /* Check if LBA48 is supported */
    dev->supports_lba48 = (identify_data[83] & (1 << 10)) ? 1 : 0;
    
    /* Get sector count.
     *
     * The LBA48 capability bit (word 83 bit 10) and the LBA48 sector
     * count (words 100-103) are independent fields.  QEMU sets the
     * capability bit for every IDE disk but only populates words
     * 100-103 when the image is large enough to need 48-bit
     * addressing.  For small or empty raw images words 100-103 are 0.
     * Blindly trusting that 0 makes dev->sectors == 0, which then
     * blocks every subsequent read at the range guard.
     *
     * Resolution: prefer the LBA48 count when non-zero; fall back to
     * LBA28; clear the flag if we fell back so the read/write paths
     * stay consistent with whichever field is authoritative.
     */
    if (dev->supports_lba48 && id->lba48_capacity != 0) {
        dev->sectors = id->lba48_capacity;
    } else {
        dev->sectors = id->lba_capacity;
        dev->supports_lba48 = 0;
    }
    
    /* Extract model string (swap byte order) */
    for (int i = 0; i < 20; i++) {
        dev->model[i * 2] = id->model[i] >> 8;
        dev->model[i * 2 + 1] = id->model[i] & 0xFF;
    }
    dev->model[40] = '\0';
    
    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && dev->model[i] == ' '; i--) {
        dev->model[i] = '\0';
    }
    
    /* Extract serial string */
    for (int i = 0; i < 10; i++) {
        dev->serial[i * 2] = id->serial[i] >> 8;
        dev->serial[i * 2 + 1] = id->serial[i] & 0xFF;
    }
    dev->serial[20] = '\0';
    
    /* Extract firmware string */
    for (int i = 0; i < 4; i++) {
        dev->firmware[i * 2] = id->firmware[i] >> 8;
        dev->firmware[i * 2 + 1] = id->firmware[i] & 0xFF;
    }
    dev->firmware[8] = '\0';
    
    dev->exists = 1;
    return 0;
}

/*
 * Read sectors using PIO mode (28-bit LBA)
 */
int ata_read_sectors(struct ata_device *dev, uint64_t lba, uint8_t count, void *buffer) {
    uint16_t *buf = (uint16_t*)buffer;
    
    if (!dev->exists) {
        return -1;
    }
    
    if (dev->sectors > 0 && lba >= dev->sectors) {
        return -1;  /* LBA out of range */
    }
    
    /* Select drive and set LBA mode */
    uint8_t drive = dev->is_master ? 0xE0 : 0xF0;
    outb(dev->base + 6, drive | ((lba >> 24) & 0x0F));
    
    /* Wait for drive ready */
    if (ata_wait_ready(dev) != 0) {
        return -1;
    }
    
    /* Set sector count and LBA */
    outb(dev->base + 2, count);
    outb(dev->base + 3, (uint8_t)lba);
    outb(dev->base + 4, (uint8_t)(lba >> 8));
    outb(dev->base + 5, (uint8_t)(lba >> 16));
    
    /* Send read command */
    outb(dev->base + 7, ATA_CMD_READ_SECTORS);
    
    /* Read sectors */
    for (int sector = 0; sector < count; sector++) {
        /* Wait for DRQ */
        if (ata_wait_drq(dev) != 0) {
            return -1;
        }
        
        /* Read 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            buf[sector * 256 + i] = inw(dev->base + 0);
        }
        
        /* 400ns delay */
        ata_400ns_delay(dev);
    }
    
    return 0;
}

/*
 * Write sectors using PIO mode (28-bit LBA)
 */
int ata_write_sectors(struct ata_device *dev, uint64_t lba, uint8_t count, const void *buffer) {
    const uint16_t *buf = (const uint16_t*)buffer;
    
    if (!dev->exists) {
        return -1;
    }
    
    if (dev->sectors > 0 && lba >= dev->sectors) {
        return -1;  /* LBA out of range */
    }
    
    /* Select drive and set LBA mode */
    uint8_t drive = dev->is_master ? 0xE0 : 0xF0;
    outb(dev->base + 6, drive | ((lba >> 24) & 0x0F));
    
    /* Wait for drive ready */
    if (ata_wait_ready(dev) != 0) {
        return -1;
    }
    
    /* Set sector count and LBA */
    outb(dev->base + 2, count);
    outb(dev->base + 3, (uint8_t)lba);
    outb(dev->base + 4, (uint8_t)(lba >> 8));
    outb(dev->base + 5, (uint8_t)(lba >> 16));
    
    /* Send write command */
    outb(dev->base + 7, ATA_CMD_WRITE_SECTORS);
    
    /* Write sectors */
    for (int sector = 0; sector < count; sector++) {
        /* Wait for DRQ */
        if (ata_wait_drq(dev) != 0) {
            return -1;
        }
        
        /* Write 256 words (512 bytes) */
        for (int i = 0; i < 256; i++) {
            outw(dev->base + 0, buf[sector * 256 + i]);
        }
        
        /* 400ns delay */
        ata_400ns_delay(dev);
    }
    
    /* Flush cache */
    outb(dev->base + 7, ATA_CMD_CACHE_FLUSH);
    if (ata_wait_ready(dev) != 0) {
        return -1;
    }
    
    return 0;
}

/*
 * Flush disk cache
 */
int ata_flush_cache(struct ata_device *dev) {
    if (!dev->exists) {
        return -1;
    }
    
    ata_select_drive(dev);
    
    if (ata_wait_ready(dev) != 0) {
        return -1;
    }
    
    outb(dev->base + 7, ATA_CMD_CACHE_FLUSH);
    
    return ata_wait_ready(dev);
}

/*
 * Detect and initialize ATA devices
 */
int ata_detect_devices(void) {
    int detected = 0;
    
    /* Initialize primary master */
    ata_primary_master.exists = 0;
    ata_primary_master.is_master = 1;
    ata_primary_master.base = ATA_PRIMARY_DATA;
    ata_primary_master.ctrl = ATA_PRIMARY_CONTROL;
    
    if (ata_identify(&ata_primary_master) == 0) {
        detected++;
    }
    
    /* Initialize primary slave */
    ata_primary_slave.exists = 0;
    ata_primary_slave.is_master = 0;
    ata_primary_slave.base = ATA_PRIMARY_DATA;
    ata_primary_slave.ctrl = ATA_PRIMARY_CONTROL;
    
    if (ata_identify(&ata_primary_slave) == 0) {
        detected++;
    }
    
    return detected;
}

/*
 * Print device information
 */
void ata_print_device_info(struct ata_device *dev) {
    if (!dev->exists) {
        vga_writestring("  Device not present\n");
        return;
    }
    
    vga_writestring("  Model: ");
    vga_writestring(dev->model);
    vga_writestring("\n  Capacity: ");
    print_dec(dev->sectors * 512 / (1024 * 1024));
    vga_writestring(" MB (");
    print_dec(dev->sectors);
    vga_writestring(" sectors)\n");
    
    if (dev->supports_lba48) {
        vga_writestring("  LBA48: Supported\n");
    } else {
        vga_writestring("  LBA48: Not supported\n");
    }
}

/*
 * Initialize ATA subsystem
 */
void ata_init(void) {
    vga_writestring("ATA: Initializing disk controller...\n");
    
    int detected = ata_detect_devices();
    
    vga_writestring("ATA: Detected ");
    print_dec(detected);
    vga_writestring(" device(s)\n");
    
    if (ata_primary_master.exists) {
        vga_writestring("ATA: Primary Master:\n");
        ata_print_device_info(&ata_primary_master);
    }
    
    if (ata_primary_slave.exists) {
        vga_writestring("ATA: Primary Slave:\n");
        ata_print_device_info(&ata_primary_slave);
    }
    
    if (detected == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ATA: WARNING - No disks detected!\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
}