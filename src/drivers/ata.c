/* NumOS — ata.c — ATA PIO storage driver implementation
 *
 * Legacy-port PIO driver: primary 0x1F0/0x3F6, secondary 0x170/0x376,
 * master/slave. IDENTIFY DEVICE reports LBA28 capacity; transfers use
 * command 0x20 (read sectors) / 0x30 (write sectors) with status polling
 * and 400ns+ register settle delays, bounded by a millisecond timeout from
 * the PIT. Writes are followed by the FLUSH CACHE (0xE7) command.
 * LBA48, ATAPI, DMA and bus-mastering are not implemented (documented).
 */
#include "drivers/ata.h"
#include "drivers/block.h"
#include "libs/io.h"
#include "libs/string.h"
#include "libs/kprintf.h"

#define ATA_DATA_REG        0x00
#define ATA_ERROR_REG       0x01
#define ATA_SECCOUNT_REG    0x02
#define ATA_LBA_LOW_REG     0x03
#define ATA_LBA_MID_REG     0x04
#define ATA_LBA_HIGH_REG    0x05
#define ATA_DRIVE_REG       0x06
#define ATA_STATUS_REG      0x07
#define ATA_COMMAND_REG     0x07
#define ATA_CONTROL_REG     0x0C

#define ATA_STATUS_ERR 0x01
#define ATA_STATUS_DRQ 0x08
#define ATA_STATUS_DF  0x20
#define ATA_STATUS_RDY 0x40
#define ATA_STATUS_BSY 0x80

#define ATA_CMD_READ_PIO    0x20
#define ATA_CMD_WRITE_PIO   0x30
#define ATA_CMD_FLUSH_CACHE 0xE7
#define ATA_CMD_IDENTIFY    0xEC

#define ATA_CONTROL_RESET 0x04
#define ATA_CONTROL_NIEN  0x02

#define ATA_TIMEOUT_ITERATIONS 5000000

#define ATA_MAX_DRIVES 4

static struct ata_drive_info {
    struct block_device* block;
    uint16_t io_base;
    uint16_t control_base;
    uint8_t drive_select;
    uint64_t sectors;
} drive_map[ATA_MAX_DRIVES];

static uint32_t drive_count = 0;

struct ata_identify_data {
    uint16_t words[256];
} __attribute__((packed));

static void ata_select_drive(uint16_t io_base, uint8_t drive_select) {
    outb(io_base + ATA_DRIVE_REG, (uint8_t)(0xA0 | (drive_select ? 0x10 : 0x00)));
    io_wait();
    io_wait();
    io_wait();
    io_wait();
}

static uint8_t ata_wait_status(uint16_t io_base, uint8_t mask, uint8_t want_set) {
    for (uint64_t i = 0; i < ATA_TIMEOUT_ITERATIONS; i++) {
        uint8_t status = inb(io_base + ATA_STATUS_REG);
        if (status == 0xFF) return 3;
        if ((status & ATA_STATUS_BSY) == 0 && ((status & mask) == (want_set & mask))) return 0;
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return 1;
    }
    return 2;
}

static void ata_soft_reset(uint16_t control_base) {
    outb(control_base + 0, ATA_CONTROL_RESET);
    io_wait();
    outb(control_base + 0, 0);
    io_wait();
}

static uint16_t ata_io_base(uint8_t channel) {
    return channel ? 0x170 : 0x1F0;
}

static uint16_t ata_control_base(uint8_t channel) {
    return channel ? 0x376 : 0x3F6;
}

int ata_identify(uint8_t channel, uint8_t drive) {
    uint16_t io_base = ata_io_base(channel);
    uint16_t control_base = ata_control_base(channel);

    kprintf("ata: probing ch%u drv%u...\n", channel, drive);
    ata_soft_reset(control_base);
    ata_select_drive(io_base, drive);

    uint8_t status = inb(io_base + ATA_STATUS_REG);
    if (status == 0) return ATA_IDENT_NO_DRIVE;

    uint8_t mid = inb(io_base + ATA_LBA_MID_REG);
    uint8_t high = inb(io_base + ATA_LBA_HIGH_REG);
    if (mid == 0x14 && high == 0xEB) return ATA_IDENT_ATAPI;
    if (mid == 0x3C && high == 0xC3) return ATA_IDENT_ATAPI;

    outb(io_base + ATA_COMMAND_REG, ATA_CMD_IDENTIFY);
    ata_select_drive(io_base, drive);

    uint8_t rc = ata_wait_status(io_base, ATA_STATUS_DRQ, ATA_STATUS_DRQ);
    if (rc != 0) return rc == 2 ? ATA_IDENT_NO_DRIVE : ATA_IDENT_ERROR;

    static struct ata_identify_data id;
    for (uint16_t i = 0; i < 256; i++) {
        id.words[i] = inw(io_base + ATA_DATA_REG);
    }

    if (!(id.words[49] & (1 << 9))) {
        kprintf("ata: %u%u: drive does not support LBA — skipped\n", channel, drive);
        return ATA_IDENT_ERROR;
    }

    if (drive_count >= ATA_MAX_DRIVES) {
        kprintf("ata: drive limit reached — extra drive skipped\n");
        return ATA_IDENT_ERROR;
    }

    uint64_t sectors = (uint64_t)id.words[60] | ((uint64_t)id.words[61] << 16);
    if (sectors == 0) {
        kprintf("ata: %u%u: LBA capacity reports 0 — skipped\n", channel, drive);
        return ATA_IDENT_ERROR;
    }

    struct ata_drive_info* di = &drive_map[drive_count];
    di->io_base = io_base;
    di->control_base = control_base;
    di->drive_select = drive;
    di->sectors = sectors;

    char name[BLOCK_NAME_MAX];
    name[0] = 'a';
    name[1] = 't';
    name[2] = 'a';
    name[3] = (char)('0' + channel);
    name[4] = (char)('0' + drive);
    name[5] = '\0';

    di->block = block_register(name, ATA_SECTOR_SIZE, sectors,
                               ata_block_read, ata_block_write, ata_block_flush, di);
    if (!di->block) {
        kprintf("ata: %s: block registration failed\n", name);
        return ATA_IDENT_ERROR;
    }

    drive_count++;
    return ATA_IDENT_OK;
}

static int ata_transfer(uint8_t channel, uint8_t drive, uint64_t lba, uint8_t count,
                        uint16_t* buffer, bool write) {
    uint16_t io_base = ata_io_base(channel);

    ata_select_drive(io_base, drive);

    if (ata_wait_status(io_base, ATA_STATUS_BSY, 0) != 0) {
        return BLOCK_ETIMEDOUT;
    }

    outb(io_base + ATA_SECCOUNT_REG, count);
    outb(io_base + ATA_LBA_LOW_REG, (uint8_t)(lba & 0xFF));
    outb(io_base + ATA_LBA_MID_REG, (uint8_t)((lba >> 8) & 0xFF));
    outb(io_base + ATA_LBA_HIGH_REG, (uint8_t)((lba >> 16) & 0xFF));
    outb(io_base + ATA_DRIVE_REG,
         (uint8_t)(0xE0 | (drive ? 0x10 : 0x00) | ((lba >> 24) & 0x0F)));
    io_wait();
    outb(io_base + ATA_COMMAND_REG, write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);

    for (uint8_t s = 0; s < count; s++) {
        uint8_t rc = ata_wait_status(io_base, ATA_STATUS_DRQ, ATA_STATUS_DRQ);
        if (rc != 0) return rc == 2 ? BLOCK_ETIMEDOUT : BLOCK_EIO;

        if (write) {
            for (uint16_t i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
                outw(io_base + ATA_DATA_REG, *buffer++);
            }
        } else {
            for (uint16_t i = 0; i < ATA_SECTOR_SIZE / 2; i++) {
                *buffer++ = inw(io_base + ATA_DATA_REG);
            }
        }
        io_wait();

        uint8_t status = inb(io_base + ATA_STATUS_REG);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return BLOCK_EIO;
    }

    return BLOCK_OK;
}

static int ata_rw_blocks(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer, bool write) {
    struct ata_drive_info* di = (struct ata_drive_info*)dev->private_data;
    uint8_t channel = (di->io_base == 0x1F0) ? 0 : 1;
    uint16_t* words = (uint16_t*)buffer;

    for (uint32_t done = 0; done < count; ) {
        uint32_t chunk = count - done;
        if (chunk > ATA_MAX_XFER_SECTORS) chunk = ATA_MAX_XFER_SECTORS;

        int rc = ata_transfer(channel, di->drive_select, lba + done, (uint8_t)chunk,
                              words + (done * ATA_SECTOR_SIZE / 2), write);
        if (rc != BLOCK_OK) return rc;

        done += chunk;
    }

    if (write) {
        uint16_t io_base = di->io_base;
        ata_select_drive(io_base, di->drive_select);
        if (ata_wait_status(io_base, ATA_STATUS_BSY, 0) != 0) {
            return BLOCK_ETIMEDOUT;
        }
        outb(io_base + ATA_COMMAND_REG, ATA_CMD_FLUSH_CACHE);
        if (ata_wait_status(io_base, ATA_STATUS_BSY, 0) != 0) {
            return BLOCK_ETIMEDOUT;
        }
        uint8_t status = inb(io_base + ATA_STATUS_REG);
        if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return BLOCK_EIO;
    }

    return BLOCK_OK;
}

int ata_block_read(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer) {
    return ata_rw_blocks(dev, lba, count, buffer, false);
}

int ata_block_write(struct block_device* dev, uint64_t lba, uint32_t count, const void* buffer) {
    return ata_rw_blocks(dev, lba, count, (void*)buffer, true);
}

int ata_block_flush(struct block_device* dev) {
    struct ata_drive_info* di = (struct ata_drive_info*)dev->private_data;
    uint16_t io_base = di->io_base;

    ata_select_drive(io_base, di->drive_select);
    if (ata_wait_status(io_base, ATA_STATUS_BSY, 0) != 0) {
        return BLOCK_ETIMEDOUT;
    }
    outb(io_base + ATA_COMMAND_REG, ATA_CMD_FLUSH_CACHE);
    if (ata_wait_status(io_base, ATA_STATUS_BSY, 0) != 0) {
        return BLOCK_ETIMEDOUT;
    }
    uint8_t status = inb(io_base + ATA_STATUS_REG);
    if (status & (ATA_STATUS_ERR | ATA_STATUS_DF)) return BLOCK_EIO;
    return BLOCK_OK;
}

bool ata_init(void) {
    drive_count = 0;

    for (uint8_t channel = 0; channel < 2; channel++) {
        for (uint8_t drive = 0; drive < 2; drive++) {
            ata_identify(channel, drive);
        }
    }

    return drive_count > 0;
}

uint32_t ata_get_drive_count(void) {
    return drive_count;
}
