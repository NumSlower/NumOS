#ifndef ATA_H
#define ATA_H

#include <stdint.h>

/* ATA/IDE port addresses */
#define ATA_PRIMARY_BASE        0x1F0
#define ATA_PRIMARY_CONTROL     0x3F6
#define ATA_SECONDARY_BASE      0x170
#define ATA_SECONDARY_CONTROL   0x376

/* ATA drive structure */
struct ata_drive {
    int exists;                 // Does this drive exist?
    uint16_t base_port;         // Base I/O port
    uint16_t control_port;      // Control/status port
    int drive_num;              // Drive number (0-3)
    int is_slave;               // Is this a slave drive?
    uint32_t sectors;           // Total sectors on drive
    char model[41];             // Drive model string
};

/* Function prototypes */
int ata_init(void);
struct ata_drive* ata_get_drive_info(int drive_num);
void ata_print_drives(void);

/* Disk I/O functions (used by file systems) */
int disk_read_sectors(uint32_t drive, uint32_t start_sector, uint32_t count, void *buffer);
int disk_write_sectors(uint32_t drive, uint32_t start_sector, uint32_t count, const void *buffer);

#endif /* ATA_H */