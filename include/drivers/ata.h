/* NumOS — ata.h — ATA PIO storage driver (initial real storage path)
 *
 * Detects drives on the primary/secondary channels (master/slave) via
 * IDENTIFY DEVICE, registers each LBA-capable drive in the block-device
 * layer, and services block requests with LBA28 PIO transfers.
 */
#ifndef NUMOS_DRIVERS_ATA_H
#define NUMOS_DRIVERS_ATA_H

#include "libs/types.h"
#include "drivers/block.h"

#define ATA_SECTOR_SIZE 512
#define ATA_MAX_XFER_SECTORS 8

#define ATA_IDENT_OK 0
#define ATA_IDENT_NO_DRIVE 1
#define ATA_IDENT_ATAPI 2
#define ATA_IDENT_ERROR 3

bool ata_init(void);
uint32_t ata_get_drive_count(void);
int ata_identify(uint8_t channel, uint8_t drive);
int ata_block_read(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
int ata_block_write(struct block_device* dev, uint64_t lba, uint32_t count, const void* buffer);
int ata_block_flush(struct block_device* dev);

#endif
