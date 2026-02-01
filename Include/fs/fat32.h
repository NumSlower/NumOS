#ifndef FAT32_H
#define FAT32_H

#include "lib/base.h"

/* FAT32 Boot Sector Structure (512 bytes) */
struct fat32_boot_sector {
    uint8_t jump_boot[3];           /* Jump instruction to boot code */
    uint8_t oem_name[8];            /* OEM name */
    uint16_t bytes_per_sector;      /* Bytes per sector (typically 512) */
    uint8_t sectors_per_cluster;    /* Sectors per cluster */
    uint16_t reserved_sectors;      /* Reserved sectors (including boot sector) */
    uint8_t num_fats;               /* Number of FAT copies (typically 2) */
    uint16_t root_entry_count;      /* Root directory entries (0 for FAT32) */
    uint16_t total_sectors_16;      /* Total sectors (0 if >65535) */
    uint8_t media_type;             /* Media descriptor */
    uint16_t fat_size_16;           /* Sectors per FAT (0 for FAT32) */
    uint16_t sectors_per_track;     /* Sectors per track */
    uint16_t num_heads;             /* Number of heads */
    uint32_t hidden_sectors;        /* Hidden sectors */
    uint32_t total_sectors_32;      /* Total sectors (FAT32) */
    
    /* FAT32 Extended Boot Record */
    uint32_t fat_size_32;           /* Sectors per FAT (FAT32) */
    uint16_t ext_flags;             /* Extension flags */
    uint16_t fs_version;            /* Filesystem version */
    uint32_t root_cluster;          /* Root directory cluster */
    uint16_t fs_info_sector;        /* FSInfo sector number */
    uint16_t backup_boot_sector;    /* Backup boot sector */
    uint8_t reserved[12];           /* Reserved */
    uint8_t drive_number;           /* Drive number */
    uint8_t reserved1;              /* Reserved */
    uint8_t boot_signature;         /* Extended boot signature (0x29) */
    uint32_t volume_id;             /* Volume serial number */
    uint8_t volume_label[11];       /* Volume label */
    uint8_t fs_type[8];             /* Filesystem type ("FAT32   ") */
} __attribute__((packed));

/* FAT32 FSInfo Structure (512 bytes) */
struct fat32_fsinfo {
    uint32_t lead_signature;        /* 0x41615252 */
    uint8_t reserved1[480];         /* Reserved */
    uint32_t struct_signature;      /* 0x61417272 */
    uint32_t free_clusters;         /* Number of free clusters */
    uint32_t next_free_cluster;     /* Next free cluster */
    uint8_t reserved2[12];          /* Reserved */
    uint32_t trail_signature;       /* 0xAA550000 */
} __attribute__((packed));

/* FAT32 Directory Entry (32 bytes) */
struct fat32_dir_entry {
    uint8_t name[11];               /* Short filename (8.3 format) */
    uint8_t attr;                   /* File attributes */
    uint8_t nt_reserved;            /* Reserved for Windows NT */
    uint8_t create_time_tenth;      /* Creation time (tenths of second) */
    uint16_t create_time;           /* Creation time */
    uint16_t create_date;           /* Creation date */
    uint16_t access_date;           /* Last access date */
    uint16_t first_cluster_high;    /* High word of first cluster */
    uint16_t write_time;            /* Last write time */
    uint16_t write_date;            /* Last write date */
    uint16_t first_cluster_low;     /* Low word of first cluster */
    uint32_t file_size;             /* File size in bytes */
} __attribute__((packed));

/* FAT32 Long Filename Entry (LFN) */
struct fat32_lfn_entry {
    uint8_t order;                  /* Sequence number */
    uint16_t name1[5];              /* First 5 characters (Unicode) */
    uint8_t attr;                   /* Always 0x0F (LFN marker) */
    uint8_t type;                   /* Always 0 */
    uint8_t checksum;               /* Checksum of short name */
    uint16_t name2[6];              /* Next 6 characters */
    uint16_t first_cluster;         /* Always 0 */
    uint16_t name3[2];              /* Last 2 characters */
} __attribute__((packed));

/* File Attributes */
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LONG_NAME    0x0F  /* LFN marker */

/* FAT Entry Values */
#define FAT32_FREE_CLUSTER      0x00000000
#define FAT32_BAD_CLUSTER       0x0FFFFFF7
#define FAT32_EOC_MIN           0x0FFFFFF8  /* End of chain minimum */
#define FAT32_EOC_MAX           0x0FFFFFFF  /* End of chain maximum */

/* Maximum path and filename lengths */
#define FAT32_MAX_PATH          260
#define FAT32_MAX_FILENAME      255

/* FAT32 Filesystem State */
struct fat32_fs {
    struct fat32_boot_sector boot;
    struct fat32_fsinfo fsinfo;
    
    uint32_t first_data_sector;
    uint32_t data_sectors;
    uint32_t total_clusters;
    uint32_t bytes_per_cluster;
    
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    
    uint8_t *fat_cache;             /* Cached FAT table */
    int fat_cache_dirty;            /* FAT needs to be written */
    
    uint32_t current_directory;     /* Current directory cluster */
    int mounted;                    /* Filesystem mounted flag */
};

/* File descriptor for open files */
struct fat32_file {
    char name[FAT32_MAX_FILENAME];
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t size;
    uint32_t position;
    uint8_t attr;
    int flags;
    int in_use;
};

/* Directory entry for listing */
struct fat32_dirent {
    char name[FAT32_MAX_FILENAME];
    uint32_t size;
    uint8_t attr;
    uint32_t cluster;
};

/* Open flags */
#define FAT32_O_RDONLY      0x01
#define FAT32_O_WRONLY      0x02
#define FAT32_O_RDWR        0x03
#define FAT32_O_CREAT       0x04
#define FAT32_O_TRUNC       0x08
#define FAT32_O_APPEND      0x10

/* Seek origins */
#define FAT32_SEEK_SET      0
#define FAT32_SEEK_CUR      1
#define FAT32_SEEK_END      2

/* Core FAT32 Functions */
int fat32_init(void);
int fat32_mount(void);
void fat32_unmount(void);

/* Directory Operations */
int fat32_mkdir(const char *path);
int fat32_rmdir(const char *path);
int fat32_chdir(const char *path);
char* fat32_getcwd(char *buf, size_t size);
int fat32_readdir(struct fat32_dirent *entries, int max_entries);

/* File Operations */
int fat32_open(const char *path, int flags);
int fat32_close(int fd);
ssize_t fat32_read(int fd, void *buf, size_t count);
ssize_t fat32_write(int fd, const void *buf, size_t count);
off_t fat32_seek(int fd, off_t offset, int whence);
int fat32_stat(const char *path, struct fat32_dirent *stat);
int fat32_unlink(const char *path);

/* Cluster Operations */
uint32_t fat32_read_fat_entry(uint32_t cluster);
void fat32_write_fat_entry(uint32_t cluster, uint32_t value);
uint32_t fat32_alloc_cluster(void);
void fat32_free_cluster_chain(uint32_t start_cluster);
uint32_t fat32_next_cluster(uint32_t cluster);

/* Sector I/O */
int fat32_read_sector(uint32_t sector, void *buffer);
int fat32_write_sector(uint32_t sector, const void *buffer);
int fat32_read_cluster(uint32_t cluster, void *buffer);
int fat32_write_cluster(uint32_t cluster, const void *buffer);

/* Utility Functions */
void fat32_print_info(void);
void fat32_list_directory(const char *path);
int fat32_format_name(const char *filename, char *formatted);
void fat32_parse_short_name(const uint8_t *short_name, char *long_name);

/* FSInfo Functions */
void fat32_update_fsinfo(void);
uint32_t fat32_get_free_clusters(void);

#endif /* FAT32_H */