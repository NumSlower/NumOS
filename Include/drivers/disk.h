#ifndef DISK_H
#define DISK_H

#include "lib/base.h"

/* Disk Configuration Constants */
#define DISK_SECTOR_SIZE        512                    /* Standard sector size */
#define DISK_IMAGE_PATH         "numos_disk.img"      /* Disk image filename */
#define DISK_DEFAULT_SIZE       (4 * 1024 * 1024)    /* 64MB default disk */
#define DISK_MAX_DISKS          4                      /* Maximum number of disks */
#define DISK_CACHE_SECTORS      32                     /* Number of sectors to cache */

/* Disk Types */
#define DISK_TYPE_UNKNOWN       0
#define DISK_TYPE_FLOPPY        1
#define DISK_TYPE_HDD           2
#define DISK_TYPE_SSD           3
#define DISK_TYPE_CDROM         4
#define DISK_TYPE_IMAGE         5  /* Disk image file */

/* Disk Status Flags */
#define DISK_STATUS_READY       0x01
#define DISK_STATUS_MOUNTED     0x02
#define DISK_STATUS_WRITABLE    0x04
#define DISK_STATUS_CACHED      0x08
#define DISK_STATUS_ERROR       0x10

/* Error Codes */
#define DISK_SUCCESS            0
#define DISK_ERROR_GENERIC      1
#define DISK_ERROR_NOT_FOUND    2
#define DISK_ERROR_IO           3
#define DISK_ERROR_INVALID      4
#define DISK_ERROR_NO_MEMORY    5
#define DISK_ERROR_READ_ONLY    6
#define DISK_ERROR_NOT_READY    7

/* Disk Information Structure */
struct disk_info {
    uint8_t disk_id;                 /* Disk identifier */
    uint8_t disk_type;               /* Disk type */
    uint8_t status;                  /* Status flags */
    uint32_t sector_count;           /* Total sectors */
    uint32_t sector_size;            /* Bytes per sector */
    uint64_t total_size;             /* Total disk size in bytes */
    char label[32];                  /* Disk label */
    char serial[16];                 /* Serial number */
};

/* Disk Cache Entry */
struct disk_cache_entry {
    uint32_t sector;                 /* Sector number */
    uint8_t *data;                   /* Cached sector data */
    uint8_t dirty;                   /* Modified flag */
    uint8_t valid;                   /* Valid data flag */
    uint64_t last_access;            /* Last access time */
};

/* Disk Handle Structure */
struct disk_handle {
    uint8_t disk_id;                 /* Disk ID */
    struct disk_info info;           /* Disk information */
    void *private_data;              /* Driver-specific data */
    struct disk_cache_entry cache[DISK_CACHE_SECTORS]; /* Sector cache */
    
    /* Function pointers for disk operations */
    int (*read_sectors)(struct disk_handle *disk, uint32_t start_sector, 
                       uint32_t count, void *buffer);
    int (*write_sectors)(struct disk_handle *disk, uint32_t start_sector, 
                        uint32_t count, const void *buffer);
    int (*flush)(struct disk_handle *disk);
    int (*identify)(struct disk_handle *disk);
};

/* Core Disk Functions */
int disk_init(void);
void disk_shutdown(void);

/* Disk Management */
struct disk_handle* disk_open(uint8_t disk_id);
int disk_close(struct disk_handle *disk);
int disk_create_image(const char *filename, uint64_t size_bytes);
int disk_mount_image(const char *filename, uint8_t disk_id);
int disk_unmount(uint8_t disk_id);

/* Disk I/O Operations */
int disk_read_sector(struct disk_handle *disk, uint32_t sector, void *buffer);
int disk_write_sector(struct disk_handle *disk, uint32_t sector, const void *buffer);
int disk_read_sectors(struct disk_handle *disk, uint32_t start_sector, 
                     uint32_t count, void *buffer);
int disk_write_sectors(struct disk_handle *disk, uint32_t start_sector, 
                      uint32_t count, const void *buffer);

/* Cache Management */
int disk_flush_cache(struct disk_handle *disk);
int disk_invalidate_cache(struct disk_handle *disk);
void disk_enable_cache(struct disk_handle *disk, int enable);

/* Disk Information */
struct disk_info* disk_get_info(uint8_t disk_id);
int disk_get_sector_size(uint8_t disk_id);
uint32_t disk_get_sector_count(uint8_t disk_id);
uint64_t disk_get_size(uint8_t disk_id);
int disk_is_ready(uint8_t disk_id);

/* Utility Functions */
int disk_validate_sector(struct disk_handle *disk, uint32_t sector);
int disk_validate_range(struct disk_handle *disk, uint32_t start_sector, uint32_t count);

/* Debug and Statistics */
void disk_print_info(uint8_t disk_id);
void disk_print_cache_stats(uint8_t disk_id);
void disk_list_disks(void);

/* Low-level Hardware Interface */
int disk_detect_hardware(void);
int disk_initialize_ata(void);

/* File-based Disk Image Operations */
int disk_image_create(const char *filename, uint64_t size);
int disk_image_open(const char *filename);
int disk_image_close(int handle);
int disk_image_read(int handle, uint32_t sector, void *buffer);
int disk_image_write(int handle, uint32_t sector, const void *buffer);
int disk_image_sync(int handle);

#endif /* DISK_H */