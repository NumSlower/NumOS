#ifndef ATA_H
#define ATA_H

#include <stdint.h>
#include <stddef.h>

/* ATA ports */
#define ATA_PRIMARY_BASE    0x1F0
#define ATA_SECONDARY_BASE  0x170

/* ATA registers (offsets from base) */
#define ATA_REG_DATA        0x00    // Data register
#define ATA_REG_FEATURES    0x01    // Features register
#define ATA_REG_SECCOUNT    0x02    // Sector count register
#define ATA_REG_LBA_LOW     0x03    // LBA low register
#define ATA_REG_LBA_MID     0x04    // LBA mid register
#define ATA_REG_LBA_HIGH    0x05    // LBA high register
#define ATA_REG_DRIVE       0x06    // Drive select register
#define ATA_REG_COMMAND     0x07    // Command register
#define ATA_REG_STATUS      0x07    // Status register (same as command)

/* ATA control ports */
#define ATA_REG_CONTROL     0x206   // Control register (base + 0x206)
#define ATA_REG_ALTSTATUS   0x206   // Alternative status (same as control)

/* ATA commands */
#define ATA_CMD_READ_SECTORS    0x20    // Read sectors
#define ATA_CMD_WRITE_SECTORS   0x30    // Write sectors
#define ATA_CMD_IDENTIFY        0xEC    // Identify device

/* ATA status bits */
#define ATA_STATUS_ERR          0x01    // Error
#define ATA_STATUS_DRQ          0x08    // Data request
#define ATA_STATUS_SRV          0x10    // Service request
#define ATA_STATUS_DF           0x20    // Drive fault
#define ATA_STATUS_RDY          0x40    // Ready
#define ATA_STATUS_BSY          0x80    // Busy

/* Drive selection bits */
#define ATA_DRIVE_MASTER        0xA0    // Master drive
#define ATA_DRIVE_SLAVE         0xB0    // Slave drive

/* ATA device structure */
struct ata_device {
    uint16_t base_port;         // Base I/O port
    uint16_t control_port;      // Control port
    uint8_t drive_select;       // Drive selection value
    int present;                // Device present flag
    uint32_t sectors;           // Total sectors
    char model[41];             // Device model string
    char serial[21];            // Device serial number
};

/* ATA identify data structure (simplified) */
struct ata_identify {
    uint16_t general_info;          // General information
    uint16_t cylinders;             // Number of cylinders
    uint16_t reserved1;             // Reserved
    uint16_t heads;                 // Number of heads
    uint16_t bytes_per_track;       // Bytes per track
    uint16_t bytes_per_sector;      // Bytes per sector
    uint16_t sectors_per_track;     // Sectors per track
    uint16_t vendor_specific[3];    // Vendor specific
    char serial_number[20];         // Serial number (swapped bytes)
    uint16_t buffer_type;           // Buffer type
    uint16_t buffer_size;           // Buffer size
    uint16_t ecc_bytes;             // ECC bytes
    char firmware_revision[8];      // Firmware revision (swapped bytes)
    char model_number[40];          // Model number (swapped bytes)
    // ... many more fields, we'll keep it simple for now
} __attribute__((packed));

/* ATA drive indices */
#define ATA_PRIMARY_MASTER      0
#define ATA_PRIMARY_SLAVE       1
#define ATA_SECONDARY_MASTER    2
#define ATA_SECONDARY_SLAVE     3
#define ATA_MAX_DRIVES          4

/* Function prototypes */
void ata_init(void);
int ata_detect_drives(void);
int ata_identify_drive(int drive);
int ata_read_sectors(int drive, uint32_t lba, uint8_t sector_count, void *buffer);
int ata_write_sectors(int drive, uint32_t lba, uint8_t sector_count, const void *buffer);

/* Low-level functions */
void ata_wait_busy(uint16_t base_port);
void ata_wait_drq(uint16_t base_port);
uint8_t ata_read_status(uint16_t base_port);
void ata_soft_reset(uint16_t base_port);
void ata_select_drive(int drive);

/* Utility functions */
void ata_string_fixup(char *str, int len);
void ata_dump_drive_info(int drive);
int ata_get_drive_count(void);
struct ata_device *ata_get_drive(int drive);

/* Global ATA devices */
extern struct ata_device ata_drives[ATA_MAX_DRIVES];

#endif /* ATA_H */