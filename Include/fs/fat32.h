#ifndef FAT32_H
#define FAT32_H

#include "lib/base.h"


/* FAT32 Constants */
#define FAT32_END_OF_CHAIN      0x0FFFFFFF
#define FAT32_BAD_CLUSTER       0x0FFFFFF7
#define FAT32_FREE_CLUSTER      0x00000000
#define FAT32_SECTOR_SIZE       512
#define FAT32_MAX_FILENAME      256
#define FAT32_MAX_PATH          512
#define FAT32_MAX_OPEN_FILES    32

/* File Attributes */
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LONG_NAME    0x0F

/* File Modes */
#define FAT32_MODE_READ         0x01
#define FAT32_MODE_WRITE        0x02
#define FAT32_MODE_APPEND       0x04
#define FAT32_MODE_CREATE       0x08

/* Seek Origins */
#define FAT32_SEEK_SET          0
#define FAT32_SEEK_CUR          1
#define FAT32_SEEK_END          2

/* Error Codes */
#define FAT32_SUCCESS           0
#define FAT32_ERROR_GENERIC     1
#define FAT32_ERROR_NOT_FOUND   2
#define FAT32_ERROR_NO_SPACE    3
#define FAT32_ERROR_INVALID     4
#define FAT32_ERROR_IO          5
#define FAT32_ERROR_EXISTS      6
#define FAT32_ERROR_NO_MEMORY   7

/* FAT32 Boot Sector Structure */
struct fat32_boot_sector {
    uint8_t jump_code[3];           /* Boot jump instruction */
    uint8_t oem_name[8];           /* OEM identifier */
    uint16_t bytes_per_sector;     /* Bytes per sector (usually 512) */
    uint8_t sectors_per_cluster;   /* Sectors per cluster */
    uint16_t reserved_sectors;     /* Number of reserved sectors */
    uint8_t num_fats;             /* Number of FAT tables */
    uint16_t root_entries;        /* Root directory entries (0 for FAT32) */
    uint16_t total_sectors_16;    /* Total sectors (if < 65536) */
    uint8_t media_descriptor;     /* Media descriptor byte */
    uint16_t sectors_per_fat_16;  /* Sectors per FAT (FAT12/16 only) */
    uint16_t sectors_per_track;   /* Sectors per track */
    uint16_t num_heads;           /* Number of heads */
    uint32_t hidden_sectors;      /* Hidden sectors */
    uint32_t total_sectors_32;    /* Total sectors (if >= 65536) */
    uint32_t sectors_per_fat_32;  /* Sectors per FAT (FAT32) */
    uint16_t ext_flags;           /* Extended flags */
    uint16_t filesystem_version;  /* Filesystem version */
    uint32_t root_cluster;        /* Root directory cluster */
    uint16_t fsinfo_sector;       /* FSInfo sector number */
    uint16_t backup_boot_sector;  /* Backup boot sector */
    uint8_t reserved[12];         /* Reserved bytes */
    uint8_t drive_number;         /* Physical drive number */
    uint8_t reserved1;            /* Reserved byte */
    uint8_t boot_signature;       /* Extended boot signature */
    uint32_t volume_id;           /* Volume serial number */
    uint8_t volume_label[11];     /* Volume label */
    uint8_t filesystem_type[8];   /* Filesystem type string */
    uint8_t boot_code[420];       /* Boot code */
    uint16_t boot_sector_signature; /* Boot sector signature (0xAA55) */
} __attribute__((packed));

/* FAT32 Directory Entry */
struct fat32_dir_entry {
    uint8_t name[11];             /* Filename in 8.3 format */
    uint8_t attributes;           /* File attributes */
    uint8_t reserved;             /* Reserved for Windows NT */
    uint8_t creation_time_tenth;  /* Creation time (tenths of second) */
    uint16_t creation_time;       /* Creation time */
    uint16_t creation_date;       /* Creation date */
    uint16_t last_access_date;    /* Last access date */
    uint16_t first_cluster_high;  /* High 16 bits of first cluster */
    uint16_t last_write_time;     /* Last write time */
    uint16_t last_write_date;     /* Last write date */
    uint16_t first_cluster_low;   /* Low 16 bits of first cluster */
    uint32_t file_size;           /* File size in bytes */
} __attribute__((packed));

/* File System State */
struct fat32_fs {
    struct fat32_boot_sector boot_sector; /* Cached boot sector */
    uint32_t fat_start_sector;            /* First sector of FAT */
    uint32_t data_start_sector;           /* First sector of data area */
    uint32_t root_dir_cluster;            /* Root directory cluster */
    uint32_t sectors_per_fat;             /* Sectors per FAT table */
    uint32_t bytes_per_cluster;           /* Bytes per cluster */
    uint8_t *fat_cache;                   /* Cached FAT table */
    uint8_t *cluster_buffer;              /* Cluster-sized buffer */
    int initialized;                      /* Initialization flag */
};

/* File Handle Structure */
struct fat32_file {
    char name[FAT32_MAX_FILENAME];  /* Original filename */
    uint32_t first_cluster;         /* First cluster of file */
    uint32_t current_cluster;       /* Current cluster position */
    uint32_t file_size;             /* File size in bytes */
    uint32_t position;              /* Current file position */
    uint8_t attributes;             /* File attributes */
    int is_open;                    /* Open status flag */
    int mode;                       /* File access mode */
} __attribute__((aligned(16)));

/* Core File System Functions */
int fat32_init(void);
int fat32_mount(void);
void fat32_unmount(void);

/* File Operations */
struct fat32_file* fat32_fopen(const char *filename, const char *mode);
int fat32_fclose(struct fat32_file *file);
size_t fat32_fread(void *buffer, size_t size, size_t count, struct fat32_file *file);
size_t fat32_fwrite(const void *buffer, size_t size, size_t count, struct fat32_file *file);
int fat32_fseek(struct fat32_file *file, long offset, int whence);
long fat32_ftell(struct fat32_file *file);
int fat32_feof(struct fat32_file *file);
int fat32_fflush(struct fat32_file *file);

/* File Management */
int fat32_exists(const char *filename);
uint32_t fat32_get_file_size(const char *filename);

/* Directory Operations */
void fat32_list_files(void);
void fat32_print_file_info(const char *filename);

/* Utility Functions */
int fat32_format_name(const char *filename, char *formatted_name);
uint32_t fat32_get_next_cluster(uint32_t cluster);
uint32_t fat32_allocate_cluster(void);
int fat32_free_cluster_chain(uint32_t start_cluster);

/* Debug Functions */
void fat32_print_boot_sector(void);
void fat32_print_fs_info(void);

/* Low-level disk I/O functions */
int fat32_read_sector(uint32_t sector, void *buffer);
int fat32_write_sector(uint32_t sector, const void *buffer);
int fat32_read_sectors(uint32_t start_sector, uint32_t count, void *buffer);
int fat32_write_sectors(uint32_t start_sector, uint32_t count, const void *buffer);

#endif /* FAT32_H */