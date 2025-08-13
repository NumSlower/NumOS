#ifndef FAT_H
#define FAT_H

#include <stdint.h>
#include <stddef.h>

/* FAT filesystem types */
typedef enum {
    FAT_TYPE_UNKNOWN = 0,
    FAT_TYPE_FAT12,
    FAT_TYPE_FAT16,
    FAT_TYPE_FAT32
} fat_type_t;

/* FAT Boot Sector structure (common fields) */
struct fat_boot_sector {
    uint8_t jump_boot[3];           // Jump instruction
    char oem_name[8];               // OEM name
    uint16_t bytes_per_sector;      // Bytes per sector
    uint8_t sectors_per_cluster;    // Sectors per cluster
    uint16_t reserved_sectors;      // Reserved sectors
    uint8_t num_fats;              // Number of FATs
    uint16_t root_entries;         // Root directory entries (FAT12/16 only)
    uint16_t total_sectors_16;     // Total sectors (if < 65536)
    uint8_t media_type;            // Media descriptor
    uint16_t fat_size_16;          // FAT size in sectors (FAT12/16)
    uint16_t sectors_per_track;    // Sectors per track
    uint16_t num_heads;            // Number of heads
    uint32_t hidden_sectors;       // Hidden sectors
    uint32_t total_sectors_32;     // Total sectors (if >= 65536)
    
    /* FAT12/16 specific fields */
    uint8_t drive_number;          // Drive number
    uint8_t reserved1;             // Reserved
    uint8_t boot_signature;        // Boot signature (0x29)
    uint32_t volume_id;            // Volume ID
    char volume_label[11];         // Volume label
    char filesystem_type[8];       // Filesystem type
} __attribute__((packed));

/* Directory entry structure */
struct fat_directory_entry {
    char name[11];                 // 8.3 filename
    uint8_t attributes;            // File attributes
    uint8_t nt_reserved;           // Reserved for NT
    uint8_t creation_time_tenth;   // Creation time (tenths of second)
    uint16_t creation_time;        // Creation time
    uint16_t creation_date;        // Creation date
    uint16_t last_access_date;     // Last access date
    uint16_t first_cluster_high;   // High 16 bits of first cluster (FAT32)
    uint16_t write_time;           // Last write time
    uint16_t write_date;           // Last write date
    uint16_t first_cluster_low;    // Low 16 bits of first cluster
    uint32_t file_size;            // File size in bytes
} __attribute__((packed));

/* File attributes */
#define FAT_ATTR_READ_ONLY  0x01
#define FAT_ATTR_HIDDEN     0x02
#define FAT_ATTR_SYSTEM     0x04
#define FAT_ATTR_VOLUME_ID  0x08
#define FAT_ATTR_DIRECTORY  0x10
#define FAT_ATTR_ARCHIVE    0x20
#define FAT_ATTR_LONG_NAME  0x0F

/* Special cluster values */
#define FAT12_EOC           0xFF8   // End of cluster chain (FAT12)
#define FAT16_EOC           0xFFF8  // End of cluster chain (FAT16)
#define FAT_BAD_CLUSTER     0xFF7   // Bad cluster marker

/* FAT filesystem structure */
struct fat_filesystem {
    fat_type_t type;
    struct fat_boot_sector boot_sector;
    uint32_t fat_start_sector;
    uint32_t root_dir_start_sector;
    uint32_t data_start_sector;
    uint32_t cluster_count;
    uint32_t bytes_per_cluster;
    uint32_t fat_entries_per_sector;
    uint8_t *fat_cache;            // Cached FAT table
    uint32_t fat_cache_size;
    int initialized;
};

/* File handle structure */
struct fat_file {
    struct fat_directory_entry dir_entry;
    uint32_t current_cluster;
    uint32_t current_offset;
    uint32_t position;
    int is_open;
    int is_directory;
};

/* Directory iterator */
struct fat_dir_iterator {
    uint32_t current_sector;
    uint32_t current_entry;
    uint32_t cluster;
    struct fat_filesystem *fs;
    int is_root;
};

/* Function prototypes */
int fat_init(void);
int fat_detect_type(struct fat_boot_sector *boot_sector);
int fat_load_fat_table(struct fat_filesystem *fs);
uint32_t fat_get_next_cluster(struct fat_filesystem *fs, uint32_t cluster);
uint32_t fat_cluster_to_sector(struct fat_filesystem *fs, uint32_t cluster);

/* File operations */
struct fat_file *fat_open(const char *filename);
void fat_close(struct fat_file *file);
size_t fat_read(struct fat_file *file, void *buffer, size_t size);
int fat_seek(struct fat_file *file, uint32_t offset);
uint32_t fat_tell(struct fat_file *file);
int fat_eof(struct fat_file *file);

/* Directory operations */
struct fat_dir_iterator *fat_opendir(const char *path);
struct fat_directory_entry *fat_readdir(struct fat_dir_iterator *dir);
void fat_closedir(struct fat_dir_iterator *dir);

/* Utility functions */
void fat_parse_filename(const char *filename, char *fat_name);
void fat_format_filename(const char *fat_name, char *filename);
int fat_filename_match(const char *fat_name, const char *filename);
void fat_dump_boot_sector(struct fat_boot_sector *boot);
void fat_list_root_directory(void);

/* Global filesystem instance */
extern struct fat_filesystem g_fat_fs;

#endif /* FAT_H */