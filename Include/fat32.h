#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>

/* FAT32 Boot Sector Structure */
struct fat32_boot_sector {
    uint8_t jump_code[3];
    uint8_t oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media_descriptor;
    uint16_t sectors_per_fat_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t sectors_per_fat_32;
    uint16_t ext_flags;
    uint16_t filesystem_version;
    uint32_t root_cluster;
    uint16_t fsinfo_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t reserved1;
    uint8_t boot_signature;
    uint32_t volume_id;
    uint8_t volume_label[11];
    uint8_t filesystem_type[8];
    uint8_t boot_code[420];
    uint16_t boot_sector_signature;
} __attribute__((packed));

/* FAT32 Directory Entry */
struct fat32_dir_entry {
    uint8_t name[11];
    uint8_t attributes;
    uint8_t reserved;
    uint8_t creation_time_tenth;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t last_access_date;
    uint16_t first_cluster_high;
    uint16_t last_write_time;
    uint16_t last_write_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

/* File Attributes */
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LONG_NAME    0x0F

/* FAT32 Constants */
#define FAT32_END_OF_CHAIN      0x0FFFFFFF
#define FAT32_BAD_CLUSTER       0x0FFFFFF7
#define FAT32_FREE_CLUSTER      0x00000000
#define FAT32_SECTOR_SIZE       512
#define FAT32_MAX_FILENAME      256
#define FAT32_MAX_PATH          512

/* File System State */
struct fat32_fs {
    struct fat32_boot_sector boot_sector;
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t root_dir_cluster;
    uint32_t sectors_per_fat;
    uint32_t bytes_per_cluster;
    uint8_t *fat_cache;
    uint8_t *cluster_buffer;
    int initialized;
};

/* File Handle Structure */
struct fat32_file {
    char name[FAT32_MAX_FILENAME];
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t position;
    uint8_t attributes;
    int is_open;
    int mode;
} __attribute__((aligned(16)));

/* File Modes */
#define FAT32_MODE_READ         0x01
#define FAT32_MODE_WRITE        0x02
#define FAT32_MODE_APPEND       0x04
#define FAT32_MODE_CREATE       0x08

/* Maximum open files */
#define FAT32_MAX_OPEN_FILES    32

/* FAT32 File System Functions */
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

/* Directory Operations */
int fat32_mkdir(const char *dirname);
int fat32_rmdir(const char *dirname);
int fat32_opendir(const char *dirname);
struct fat32_dir_entry* fat32_readdir(void);
int fat32_closedir(void);

/* File Management */
int fat32_remove(const char *filename);
int fat32_rename(const char *oldname, const char *newname);
int fat32_exists(const char *filename);
uint32_t fat32_get_file_size(const char *filename);

/* Utility Functions */
void fat32_list_files(void);
void fat32_print_file_info(const char *filename);
int fat32_format_name(const char *filename, char *formatted_name);
uint32_t fat32_get_next_cluster(uint32_t cluster);
uint32_t fat32_allocate_cluster(void);
int fat32_free_cluster_chain(uint32_t start_cluster);

/* Low-level disk I/O functions (to be implemented based on your disk driver) */
int fat32_read_sector(uint32_t sector, void *buffer);
int fat32_write_sector(uint32_t sector, const void *buffer);
int fat32_read_sectors(uint32_t start_sector, uint32_t count, void *buffer);
int fat32_write_sectors(uint32_t start_sector, uint32_t count, const void *buffer);

/* Debug and Statistics */
void fat32_print_boot_sector(void);
void fat32_print_fs_info(void);
int fat32_check_fs_integrity(void);
void fat32_defragment(void);

/* Error Codes */
#define FAT32_SUCCESS           0
#define FAT32_ERROR_GENERIC     -1
#define FAT32_ERROR_NOT_FOUND   -2
#define FAT32_ERROR_NO_SPACE    -3
#define FAT32_ERROR_INVALID     -4
#define FAT32_ERROR_IO          -5
#define FAT32_ERROR_EXISTS      -6
#define FAT32_ERROR_NO_MEMORY   -7

/* Seek Origins */
#define FAT32_SEEK_SET          0
#define FAT32_SEEK_CUR          1
#define FAT32_SEEK_END          2

#endif /* FAT32_H */