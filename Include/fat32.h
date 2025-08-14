#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

/* FAT32 Boot Sector Structure */
struct fat32_boot_sector {
    uint8_t  jump[3];           // Jump instruction
    char     oem_name[8];       // OEM name
    uint16_t bytes_per_sector;  // Bytes per sector (usually 512)
    uint8_t  sectors_per_cluster; // Sectors per cluster
    uint16_t reserved_sectors;  // Reserved sectors
    uint8_t  num_fats;         // Number of FATs
    uint16_t root_entries;     // Root directory entries (0 for FAT32)
    uint16_t total_sectors_16; // Total sectors (0 for FAT32)
    uint8_t  media_descriptor; // Media descriptor
    uint16_t sectors_per_fat_16; // Sectors per FAT (0 for FAT32)
    uint16_t sectors_per_track;  // Sectors per track
    uint16_t num_heads;         // Number of heads
    uint32_t hidden_sectors;    // Hidden sectors
    uint32_t total_sectors_32;  // Total sectors (FAT32)
    
    // FAT32 extended fields
    uint32_t sectors_per_fat_32; // Sectors per FAT (FAT32)
    uint16_t flags;             // Flags
    uint16_t version;           // Version
    uint32_t root_cluster;      // Root directory cluster
    uint16_t fs_info_sector;    // FS Info sector
    uint16_t backup_boot_sector; // Backup boot sector
    uint8_t  reserved[12];      // Reserved
    uint8_t  drive_number;      // Drive number
    uint8_t  reserved1;         // Reserved
    uint8_t  boot_signature;    // Boot signature (0x29)
    uint32_t volume_serial;     // Volume serial number
    char     volume_label[11];  // Volume label
    char     fs_type[8];        // File system type ("FAT32   ")
} __attribute__((packed));

/* FAT32 Directory Entry */
struct fat32_dir_entry {
    char     name[11];          // File name (8.3 format)
    uint8_t  attributes;        // File attributes
    uint8_t  reserved;          // Reserved
    uint8_t  creation_time_ms;  // Creation time (milliseconds)
    uint16_t creation_time;     // Creation time
    uint16_t creation_date;     // Creation date
    uint16_t access_date;       // Last access date
    uint16_t cluster_high;      // High 16 bits of cluster
    uint16_t modify_time;       // Last modify time
    uint16_t modify_date;       // Last modify date
    uint16_t cluster_low;       // Low 16 bits of cluster
    uint32_t file_size;         // File size
} __attribute__((packed));

/* Long File Name (LFN) Entry */
struct fat32_lfn_entry {
    uint8_t  sequence;          // Sequence number
    uint16_t name1[5];          // Name characters 1-5 (UTF-16)
    uint8_t  attributes;        // Attributes (always 0x0F)
    uint8_t  type;              // Type (always 0)
    uint8_t  checksum;          // Checksum
    uint16_t name2[6];          // Name characters 6-11 (UTF-16)
    uint16_t cluster;           // Cluster (always 0)
    uint16_t name3[2];          // Name characters 12-13 (UTF-16)
} __attribute__((packed));

/* File attributes */
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_LABEL 0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LFN          0x0F

/* Special cluster values */
#define FAT32_EOC               0x0FFFFFF8  // End of cluster chain
#define FAT32_FREE_CLUSTER      0x00000000  // Free cluster
#define FAT32_BAD_CLUSTER       0x0FFFFFF7  // Bad cluster

/* File system limits */
#define FAT32_MAX_PATH          260
#define FAT32_MAX_FILENAME      255
#define FAT32_SECTOR_SIZE       512
#define FAT32_MAX_OPEN_FILES    32

/* File handle structure */
struct fat32_file {
    char     name[FAT32_MAX_FILENAME];
    uint32_t cluster;           // Current cluster
    uint32_t first_cluster;     // First cluster of file
    uint32_t size;              // File size
    uint32_t position;          // Current position
    uint8_t  attributes;        // File attributes
    int      is_open;           // Is file open?
    int      is_directory;      // Is this a directory?
    uint32_t dir_cluster;       // Parent directory cluster
};

/* File system state */
struct fat32_fs {
    struct fat32_boot_sector boot_sector;
    uint32_t fat_start_sector;      // First FAT sector
    uint32_t data_start_sector;     // First data sector
    uint32_t cluster_size;          // Cluster size in bytes
    uint32_t sectors_per_cluster;   // Sectors per cluster
    uint32_t total_clusters;        // Total clusters
    uint32_t root_cluster;          // Root directory cluster
    uint8_t  *fat_cache;            // FAT cache in memory
    int      fat_cache_dirty;       // Is FAT cache dirty?
    struct fat32_file open_files[FAT32_MAX_OPEN_FILES];
};

/* File operations */
typedef enum {
    FAT32_SEEK_SET = 0,
    FAT32_SEEK_CUR = 1,
    FAT32_SEEK_END = 2
} fat32_seek_type_t;

/* Function prototypes */

/* File system initialization */
int fat32_init(uint32_t drive_number);
int fat32_mount(void);
void fat32_unmount(void);

/* File operations */
struct fat32_file* fat32_open(const char *path, const char *mode);
int fat32_close(struct fat32_file *file);
size_t fat32_read(struct fat32_file *file, void *buffer, size_t size);
size_t fat32_write(struct fat32_file *file, const void *buffer, size_t size);
int fat32_seek(struct fat32_file *file, long offset, fat32_seek_type_t whence);
long fat32_tell(struct fat32_file *file);
int fat32_eof(struct fat32_file *file);

/* Directory operations */
struct fat32_file* fat32_opendir(const char *path);
struct fat32_dir_entry* fat32_readdir(struct fat32_file *dir);
int fat32_closedir(struct fat32_file *dir);

/* File management */
int fat32_create(const char *path);
int fat32_delete(const char *path);
int fat32_mkdir(const char *path);
int fat32_rmdir(const char *path);
int fat32_rename(const char *old_path, const char *new_path);

/* File information */
int fat32_stat(const char *path, struct fat32_dir_entry *stat_buf);
int fat32_exists(const char *path);
int fat32_is_directory(const char *path);
uint32_t fat32_get_file_size(const char *path);

/* Disk I/O (to be implemented by driver) */
extern int disk_read_sectors(uint32_t drive, uint32_t start_sector, uint32_t count, void *buffer);
extern int disk_write_sectors(uint32_t drive, uint32_t start_sector, uint32_t count, const void *buffer);

/* Utility functions */
void fat32_print_file_info(struct fat32_dir_entry *entry);
void fat32_print_fs_info(void);
char* fat32_format_filename(const char *fat_name);
int fat32_parse_path(const char *path, char *components[], int max_components);
uint32_t fat32_cluster_to_sector(uint32_t cluster);
uint32_t fat32_get_next_cluster(uint32_t cluster);
int fat32_set_next_cluster(uint32_t cluster, uint32_t next_cluster);
uint32_t fat32_find_free_cluster(void);
int fat32_allocate_cluster_chain(uint32_t start_cluster, uint32_t num_clusters);

/* Cache management */
void fat32_flush_fat_cache(void);
void fat32_sync(void);

/* Error codes */
#define FAT32_SUCCESS           0
#define FAT32_ERROR_INVALID     -1
#define FAT32_ERROR_NOT_FOUND   -2
#define FAT32_ERROR_NO_SPACE    -3
#define FAT32_ERROR_READ_ONLY   -4
#define FAT32_ERROR_IO          -5
#define FAT32_ERROR_EXISTS      -6
#define FAT32_ERROR_NOT_DIR     -7
#define FAT32_ERROR_IS_DIR      -8
#define FAT32_ERROR_NOT_EMPTY   -9
#define FAT32_ERROR_TOO_MANY    -10

#endif /* FAT32_H */