#ifndef ATA_H
#define ATA_H

#include "lib/base.h"

/* ATA I/O Ports (Primary Bus) */
#define ATA_PRIMARY_DATA        0x1F0
#define ATA_PRIMARY_ERROR       0x1F1
#define ATA_PRIMARY_FEATURES    0x1F1
#define ATA_PRIMARY_SECCOUNT    0x1F2
#define ATA_PRIMARY_LBALO       0x1F3
#define ATA_PRIMARY_LBAMID      0x1F4
#define ATA_PRIMARY_LBAHI       0x1F5
#define ATA_PRIMARY_DRIVE       0x1F6
#define ATA_PRIMARY_STATUS      0x1F7
#define ATA_PRIMARY_COMMAND     0x1F7
#define ATA_PRIMARY_CONTROL     0x3F6
#define ATA_PRIMARY_ALTSTAT     0x3F6

/* ATA I/O Ports (Secondary Bus) */
#define ATA_SECONDARY_DATA      0x170
#define ATA_SECONDARY_ERROR     0x171
#define ATA_SECONDARY_FEATURES  0x171
#define ATA_SECONDARY_SECCOUNT  0x172
#define ATA_SECONDARY_LBALO     0x173
#define ATA_SECONDARY_LBAMID    0x174
#define ATA_SECONDARY_LBAHI     0x175
#define ATA_SECONDARY_DRIVE     0x176
#define ATA_SECONDARY_STATUS    0x177
#define ATA_SECONDARY_COMMAND   0x177
#define ATA_SECONDARY_CONTROL   0x376
#define ATA_SECONDARY_ALTSTAT   0x376

/* ATA Commands */
#define ATA_CMD_READ_SECTORS    0x20
#define ATA_CMD_READ_SECTORS_EXT 0x24
#define ATA_CMD_WRITE_SECTORS   0x30
#define ATA_CMD_WRITE_SECTORS_EXT 0x34
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_CACHE_FLUSH     0xE7

/* ATA Status Register Bits */
#define ATA_STATUS_ERR          0x01  /* Error */
#define ATA_STATUS_IDX          0x02  /* Index */
#define ATA_STATUS_CORR         0x04  /* Corrected data */
#define ATA_STATUS_DRQ          0x08  /* Data request */
#define ATA_STATUS_DSC          0x10  /* Drive seek complete */
#define ATA_STATUS_DF           0x20  /* Drive fault */
#define ATA_STATUS_DRDY         0x40  /* Drive ready */
#define ATA_STATUS_BSY          0x80  /* Busy */

/* ATA Error Register Bits */
#define ATA_ERROR_AMNF          0x01  /* Address mark not found */
#define ATA_ERROR_TK0NF         0x02  /* Track 0 not found */
#define ATA_ERROR_ABRT          0x04  /* Aborted command */
#define ATA_ERROR_MCR           0x08  /* Media change request */
#define ATA_ERROR_IDNF          0x10  /* ID not found */
#define ATA_ERROR_MC            0x20  /* Media changed */
#define ATA_ERROR_UNC           0x40  /* Uncorrectable data error */
#define ATA_ERROR_BBK           0x80  /* Bad block */

/* Drive Selection */
#define ATA_DRIVE_MASTER        0xA0
#define ATA_DRIVE_SLAVE         0xB0

/* Sector Size */
#define ATA_SECTOR_SIZE         512

/* ATA Device Information */
struct ata_identify {
    uint16_t config;
    uint16_t cylinders;
    uint16_t reserved1;
    uint16_t heads;
    uint16_t reserved2[2];
    uint16_t sectors;
    uint16_t reserved3[3];
    uint16_t serial[10];
    uint16_t reserved4[3];
    uint16_t firmware[4];
    uint16_t model[20];
    uint16_t reserved5[13];
    uint32_t lba_capacity;
    uint16_t reserved6[38];
    uint64_t lba48_capacity;
    uint16_t reserved7[152];
} __attribute__((packed));

/* ATA Device Structure */
struct ata_device {
    int exists;
    int is_master;
    uint16_t base;
    uint16_t ctrl;
    
    uint64_t sectors;
    char model[41];
    char serial[21];
    char firmware[9];
    
    int supports_lba48;
};

/* Global ATA devices */
extern struct ata_device ata_primary_master;
extern struct ata_device ata_primary_slave;

/* ATA Driver Functions */
void ata_init(void);
int ata_detect_devices(void);
int ata_identify(struct ata_device *dev);
void ata_print_device_info(struct ata_device *dev);

/* Low-level I/O */
uint8_t ata_status_wait(struct ata_device *dev, uint8_t mask, uint8_t value, int timeout_ms);
int ata_wait_ready(struct ata_device *dev);
int ata_wait_drq(struct ata_device *dev);

/* Sector Read/Write */
int ata_read_sectors(struct ata_device *dev, uint64_t lba, uint8_t count, void *buffer);
int ata_write_sectors(struct ata_device *dev, uint64_t lba, uint8_t count, const void *buffer);

/* Cache Control */
int ata_flush_cache(struct ata_device *dev);

/* Utility */
void ata_400ns_delay(struct ata_device *dev);
void ata_select_drive(struct ata_device *dev);

#endif /* ATA_H */