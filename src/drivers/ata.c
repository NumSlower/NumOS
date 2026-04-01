/*
 * ata.c - ATA/IDE hard disk driver (PIO mode)
 *
 * Supports 28-bit LBA reads and writes via Programmed I/O on the primary
 * ATA bus.  Sector-level DMA is out of scope for this driver; all transfers use the
 * REP INSW / REP OUTSW approach via the inw()/outw() port helpers.
 *
 * Device detection:
 *   ata_init()            - detect and identify both primary bus devices
 *
 * Sector I/O:
 *   ata_read_sectors()    - read up to 255 sectors from an LBA address
 *
 * Low-level helpers:
 *   ata_status_wait()     - poll the status register with a timeout
 *   ata_wait_ready()      - wait for BSY=0 and DRDY=1
 *   ata_wait_drq()        - wait for BSY=0 and DRQ=1
 *   ata_400ns_delay()     - four alternate-status reads (~400 ns each)
 *   ata_select_drive()    - write the drive-select byte and delay
 *   ata_identify()        - send IDENTIFY and parse the returned data
 *
 * LBA48 note:
 *   The driver sets supports_lba48 only when the disk reports a non-zero
 *   48-bit sector count.  QEMU and many small raw images report the LBA48
 *   capability bit but leave words 100-103 at zero; in that case the driver
 *   falls back to the 28-bit LBA28 capacity field.
 */

#include "drivers/ata.h"
#include "drivers/graphices/vga.h"
#include "drivers/timer.h"
#include "kernel/kernel.h"

/* =========================================================================
 * Global device instances (extern'd in ata.h)
 * ======================================================================= */

struct ata_device ata_primary_master = {0};
struct ata_device ata_primary_slave  = {0};

static uint64_t ata_identify_lba28_capacity(const uint16_t *identify_data) {
    return (uint64_t)identify_data[60] |
           ((uint64_t)identify_data[61] << 16);
}

static uint64_t ata_identify_lba48_capacity(const uint16_t *identify_data) {
    return (uint64_t)identify_data[100] |
           ((uint64_t)identify_data[101] << 16) |
           ((uint64_t)identify_data[102] << 32) |
           ((uint64_t)identify_data[103] << 48);
}

static uint64_t ata_identify_chs_capacity(const uint16_t *identify_data) {
    uint64_t cylinders = identify_data[1];
    uint64_t heads = identify_data[3];
    uint64_t sectors = identify_data[6];

    if (cylinders == 0 || heads == 0 || sectors == 0) return 0;
    return cylinders * heads * sectors;
}

/* =========================================================================
 * Low-level status helpers
 * ======================================================================= */

/*
 * ata_status_wait - poll the status register until (status & mask) == value
 * or timeout_ms milliseconds have elapsed.
 *
 * Returns the last status byte read regardless of whether the condition was
 * met; the caller must check the returned value.
 */
uint8_t ata_status_wait(struct ata_device *dev,
                        uint8_t mask, uint8_t value,
                        int timeout_ms) {
    uint64_t start = timer_get_uptime_ms();
    uint8_t  status;

    while (1) {
        status = inb(dev->base + 7);

        if ((status & mask) == value) return status;

        if (timer_get_uptime_ms() - start > (uint64_t)timeout_ms) {
            return status;  /* timeout */
        }

        /* Short busy-wait to avoid hammering the bus */
        for (volatile int i = 0; i < 100; i++);
    }
}

/*
 * ata_wait_ready - wait for the drive to report DRDY=1 and BSY=0.
 * Returns 0 on success, -1 on timeout.
 */
int ata_wait_ready(struct ata_device *dev) {
    uint8_t status = ata_status_wait(dev,
                                     ATA_STATUS_BSY | ATA_STATUS_DRDY,
                                     ATA_STATUS_DRDY,
                                     5000);
    if (status & ATA_STATUS_BSY)    return -1;
    if (!(status & ATA_STATUS_DRDY)) return -1;
    return 0;
}

/*
 * ata_wait_drq - wait for DRQ=1 and BSY=0 (data transfer ready).
 * Returns 0 on success, -1 on timeout.
 */
int ata_wait_drq(struct ata_device *dev) {
    uint8_t status = ata_status_wait(dev,
                                     ATA_STATUS_BSY | ATA_STATUS_DRQ,
                                     ATA_STATUS_DRQ,
                                     5000);
    if (status & ATA_STATUS_BSY)   return -1;
    if (!(status & ATA_STATUS_DRQ)) return -1;
    return 0;
}

/*
 * ata_400ns_delay - read the alternate-status register four times.
 *
 * The ATA spec requires a minimum 400 ns setup time after writing a command
 * register before reading the status register.  Each read of the alternate-
 * status port (0x3F6 / 0x376) consumes roughly 100 ns on real hardware.
 */
void ata_400ns_delay(struct ata_device *dev) {
    inb(dev->ctrl);
    inb(dev->ctrl);
    inb(dev->ctrl);
    inb(dev->ctrl);
}

/*
 * ata_select_drive - write the drive-select byte and wait 400 ns.
 *
 * LBA mode is always selected (bits 6:7 = 0xE0 for master, 0xF0 for slave).
 * The upper 4 bits of the LBA address (bits 27:24) are zeroed here and
 * set by the caller when issuing sector commands.
 */
void ata_select_drive(struct ata_device *dev) {
    uint8_t select = dev->is_master ? ATA_DRIVE_MASTER : ATA_DRIVE_SLAVE;
    outb(dev->base + 6, select);
    ata_400ns_delay(dev);
}

/* =========================================================================
 * Device identification
 * ======================================================================= */

/*
 * ata_identify - send the IDENTIFY DEVICE command and parse the response.
 *
 * Fills dev->sectors, dev->model, dev->serial, dev->firmware, and
 * dev->supports_lba48.  Sets dev->exists = 1 on success.
 * Returns 0 on success, -1 if no device is present or the command fails.
 */
int ata_identify(struct ata_device *dev) {
    uint16_t identify_data[256] = {0};
    uint64_t lba48_capacity = 0;
    uint64_t lba28_capacity = 0;
    uint64_t chs_capacity = 0;

    ata_select_drive(dev);

    /* Zero sector count and LBA registers before the IDENTIFY command */
    outb(dev->base + 2, 0);
    outb(dev->base + 3, 0);
    outb(dev->base + 4, 0);
    outb(dev->base + 5, 0);

    outb(dev->base + 7, ATA_CMD_IDENTIFY);

    /* The ATA spec requires the drive to assert BSY within 400 ns.
     * Read the alternate-status port four times to satisfy that window. */
    ata_400ns_delay(dev);

    uint8_t status = inb(dev->base + 7);
    if (status == 0) return -1;  /* no device */

    if (ata_wait_ready(dev) != 0) return -1;

    status = inb(dev->base + 7);
    if (status & ATA_STATUS_ERR) return -1;

    if (ata_wait_drq(dev) != 0) return -1;

    /* Read the 256-word identify buffer */
    for (int i = 0; i < 256; i++) {
        identify_data[i] = inw(dev->base + 0);
    }

    struct ata_identify *id = (struct ata_identify *)identify_data;

    /*
     * LBA48 sector count selection:
     *
     * QEMU sets the LBA48 capability bit (word 83, bit 10) for every IDE
     * disk but only fills words 100-103 when the image is large enough to
     * require 48-bit addressing.  For small images words 100-103 are zero.
     * Using a zero capacity would block every subsequent I/O at the range
     * guard in ata_read_sectors.
     *
     * Resolution: use the LBA48 count (words 100-103) when non-zero;
     * otherwise fall back to the LBA28 count (word 60-61) and clear
     * supports_lba48 so the R/W paths stay consistent.
     */
    lba48_capacity = ata_identify_lba48_capacity(identify_data);
    lba28_capacity = ata_identify_lba28_capacity(identify_data);
    chs_capacity = ata_identify_chs_capacity(identify_data);

    dev->supports_lba48 = (identify_data[83] & (1 << 10)) ? 1 : 0;

    if (dev->supports_lba48 && lba48_capacity != 0) {
        dev->sectors = lba48_capacity;
    } else if (lba28_capacity != 0) {
        dev->sectors = lba28_capacity;
        dev->supports_lba48 = 0;
    } else {
        dev->sectors = chs_capacity;
        dev->supports_lba48 = 0;
    }

    if (dev->sectors == 0) return -1;

    /* Model string: 20 big-endian words, byte-swap each word */
    for (int i = 0; i < 20; i++) {
        dev->model[i * 2]     = (char)(id->model[i] >> 8);
        dev->model[i * 2 + 1] = (char)(id->model[i] & 0xFF);
    }
    dev->model[40] = '\0';

    /* Trim trailing spaces */
    for (int i = 39; i >= 0 && dev->model[i] == ' '; i--) {
        dev->model[i] = '\0';
    }

    /* Serial string: 10 big-endian words */
    for (int i = 0; i < 10; i++) {
        dev->serial[i * 2]     = (char)(id->serial[i] >> 8);
        dev->serial[i * 2 + 1] = (char)(id->serial[i] & 0xFF);
    }
    dev->serial[20] = '\0';

    /* Firmware revision: 4 big-endian words */
    for (int i = 0; i < 4; i++) {
        dev->firmware[i * 2]     = (char)(id->firmware[i] >> 8);
        dev->firmware[i * 2 + 1] = (char)(id->firmware[i] & 0xFF);
    }
    dev->firmware[8] = '\0';

    dev->exists = 1;
    return 0;
}

/* =========================================================================
 * Sector I/O
 * ======================================================================= */

/*
 * ata_read_sectors - read count sectors starting at LBA address lba into
 * buffer using 28-bit LBA PIO mode.
 *
 * buffer must be at least count * ATA_SECTOR_SIZE bytes.
 * Returns 0 on success, -1 on error.
 */
int ata_read_sectors(struct ata_device *dev,
                     uint64_t lba, uint8_t count,
                     void *buffer) {
    uint16_t *buf = (uint16_t *)buffer;

    if (!dev->exists) return -1;
    if (dev->sectors > 0 && lba >= dev->sectors) return -1;
    if (dev->sectors > 0 && count > 0 && lba + (uint64_t)count > dev->sectors) return -1;

    /* Select drive and set LBA mode; provide bits 27:24 of LBA */
    uint8_t drive = dev->is_master ? 0xE0 : 0xF0;
    outb(dev->base + 6, (uint8_t)(drive | ((lba >> 24) & 0x0F)));

    if (ata_wait_ready(dev) != 0) return -1;

    /* Set sector count and 24-bit LBA address */
    outb(dev->base + 2, count);
    outb(dev->base + 3, (uint8_t) lba);
    outb(dev->base + 4, (uint8_t)(lba >> 8));
    outb(dev->base + 5, (uint8_t)(lba >> 16));

    outb(dev->base + 7, ATA_CMD_READ_SECTORS);

    for (int sector = 0; sector < count; sector++) {
        if (ata_wait_drq(dev) != 0) return -1;

        /* Read one sector: 256 words = 512 bytes */
        for (int i = 0; i < 256; i++) {
            buf[sector * 256 + i] = inw(dev->base + 0);
        }

        ata_400ns_delay(dev);
    }

    return 0;
}

/*
 * ata_write_sectors - write count sectors starting at LBA address lba from
 * buffer using 28-bit LBA PIO mode.
 *
 * buffer must contain at least count * ATA_SECTOR_SIZE bytes.
 * Returns 0 on success, -1 on error.
 */
int ata_write_sectors(struct ata_device *dev,
                      uint64_t lba, uint8_t count,
                      const void *buffer) {
    const uint16_t *buf = (const uint16_t *)buffer;

    if (!dev->exists) return -1;
    if (dev->sectors > 0 && lba >= dev->sectors) return -1;
    if (dev->sectors > 0 && count > 0 && lba + (uint64_t)count > dev->sectors) return -1;

    /* Select drive and set LBA mode; provide bits 27:24 of LBA */
    uint8_t drive = dev->is_master ? 0xE0 : 0xF0;
    outb(dev->base + 6, (uint8_t)(drive | ((lba >> 24) & 0x0F)));

    if (ata_wait_ready(dev) != 0) return -1;

    /* Set sector count and 24-bit LBA address */
    outb(dev->base + 2, count);
    outb(dev->base + 3, (uint8_t) lba);
    outb(dev->base + 4, (uint8_t)(lba >> 8));
    outb(dev->base + 5, (uint8_t)(lba >> 16));

    outb(dev->base + 7, ATA_CMD_WRITE_SECTORS);

    for (int sector = 0; sector < count; sector++) {
        if (ata_wait_drq(dev) != 0) return -1;

        /* Write one sector: 256 words = 512 bytes */
        for (int i = 0; i < 256; i++) {
            outw(dev->base + 0, buf[sector * 256 + i]);
        }

        ata_400ns_delay(dev);
    }

    outb(dev->base + 7, ATA_CMD_CACHE_FLUSH);
    ata_wait_ready(dev);

    return 0;
}

/* =========================================================================
 * Device information display
 * ======================================================================= */

/*
 * ata_print_device_info - write model, capacity, and LBA48 support to VGA.
 */
void ata_print_device_info(struct ata_device *dev) {
    if (!dev->exists) {
        vga_writestring("  Device not present\n");
        return;
    }

    vga_writestring("  Model:    ");
    vga_writestring(dev->model);
    vga_writestring("\n  Capacity: ");
    print_dec(dev->sectors * 512 / (1024 * 1024));
    vga_writestring(" MB (");
    print_dec(dev->sectors);
    vga_writestring(" sectors)\n");
    vga_writestring(dev->supports_lba48 ? "  LBA48:   Supported\n"
                                        : "  LBA48:   Not supported\n");
}

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * ata_detect_devices - probe the primary master and slave slots.
 * Returns the number of devices found (0, 1, or 2).
 */
int ata_detect_devices(void) {
    int detected = 0;

    /* Primary Master */
    ata_primary_master.exists    = 0;
    ata_primary_master.is_master = 1;
    ata_primary_master.base      = ATA_PRIMARY_DATA;
    ata_primary_master.ctrl      = ATA_PRIMARY_CONTROL;

    if (ata_identify(&ata_primary_master) == 0) detected++;

    /* Primary Slave */
    ata_primary_slave.exists    = 0;
    ata_primary_slave.is_master = 0;
    ata_primary_slave.base      = ATA_PRIMARY_DATA;
    ata_primary_slave.ctrl      = ATA_PRIMARY_CONTROL;

    if (ata_identify(&ata_primary_slave) == 0) detected++;

    return detected;
}

/*
 * ata_init - detect ATA devices and log the results to VGA.
 */
void ata_init(void) {
    vga_writestring("ATA: Initializing disk controller...\n");

    int detected = ata_detect_devices();

    vga_writestring("ATA: Detected ");
    print_dec((uint64_t)detected);
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
