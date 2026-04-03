/*
 * fat32.c - FAT32 Filesystem Driver
 *
 * Implements a read-oriented FAT32 driver sufficient to locate, open, and
 * read files from the NumOS disk image.  Write support is intentionally
 * disabled for the boot path.
 *
 * Key data flow for a read:
 *   fat32_open()        - locate the directory entry, fill a fat32_file slot
 *   fat32_read()        - walk the FAT cluster chain, copy data to the caller
 *   fat32_close()       - release the file descriptor slot
 *
 * All sector I/O goes through fat32_read_sector(), which selects either the
 * ramdisk module or the ATA driver based on availability.
 *
 * Note: Per-cluster debug prints were removed from fat32_read_cluster().
 * They fired on every cluster read during ELF loading and flooded the
 * VGA console with thousands of lines, making the boot appear to hang.
 */

#include "fs/fat32.h"
#include "drivers/ata.h"
#include "drivers/ramdisk.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"
#include "cpu/heap.h"

/* =========================================================================
 * Module state
 * ======================================================================= */

static struct fat32_fs g_fs = {0};          /* mounted filesystem state  */

#define FAT32_NTRES_LOWER_BASE 0x08
#define FAT32_NTRES_LOWER_EXT  0x10

#define MAX_OPEN_FILES 16
static struct fat32_file g_fd_table[MAX_OPEN_FILES] = {0}; /* open files  */

/* Working sector and cluster I/O buffers; aligned for DMA safety */
static uint8_t sector_buffer[512]  __attribute__((aligned(16)));
static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));

static struct fat32_dir_entry *find_entry_in_cluster(uint32_t cluster,
                                                     char *formatted_name,
                                                     int *entry_index);
static int fat32_raw_read_sector(uint32_t sector, void *buffer);
static int fat32_raw_write_sector(uint32_t sector, const void *buffer);
static int fat32_try_mount_at_lba(uint32_t start_lba);
static int fat32_probe_mbr_partition_start(uint32_t *start_lba);
static int fat32_probe_gpt_partition_start(uint32_t *start_lba);
static uint8_t fat32_short_name_case_flags(const char *filename);
static uint32_t fat32_count_free_clusters(void);

static uint16_t fat32_le16(const uint8_t *ptr) {
    return (uint16_t)ptr[0] | ((uint16_t)ptr[1] << 8);
}

static uint32_t fat32_le32(const uint8_t *ptr) {
    return (uint32_t)ptr[0] |
           ((uint32_t)ptr[1] << 8) |
           ((uint32_t)ptr[2] << 16) |
           ((uint32_t)ptr[3] << 24);
}

static int fat32_is_power_of_two(uint8_t value) {
    return value != 0 && (value & (uint8_t)(value - 1)) == 0;
}

static int fat32_boot_sector_looks_valid(const uint8_t *sector) {
    if (!sector) return 0;
    if (sector[510] != 0x55 || sector[511] != 0xAA) return 0;

    uint16_t bytes_per_sector = fat32_le16(&sector[11]);
    uint8_t sectors_per_cluster = sector[13];
    uint16_t reserved_sectors = fat32_le16(&sector[14]);
    uint8_t num_fats = sector[16];
    uint32_t total_sectors_32 = fat32_le32(&sector[32]);
    uint32_t fat_size_32 = fat32_le32(&sector[36]);
    uint32_t root_cluster = fat32_le32(&sector[44]);

    if (bytes_per_sector != 512) return 0;
    if (!fat32_is_power_of_two(sectors_per_cluster)) return 0;
    if (reserved_sectors == 0) return 0;
    if (num_fats == 0) return 0;
    if (total_sectors_32 == 0) return 0;
    if (fat_size_32 == 0) return 0;
    if (root_cluster < 2) return 0;
    return 1;
}

/* =========================================================================
 * Low-level sector and cluster I/O
 * ======================================================================= */

/*
 * fat32_read_sector - read one 512-byte sector from the ATA primary master.
 * Returns 0 on success, -1 on error.
 */
int fat32_read_sector(uint32_t sector, void *buffer) {
    return fat32_raw_read_sector(g_fs.partition_lba_start + sector, buffer);
}

/*
 * fat32_write_sector - write one 512-byte sector to the disk device.
 * Returns 0 on success, -1 on error.
 */
int fat32_write_sector(uint32_t sector, const void *buffer) {
    return fat32_raw_write_sector(g_fs.partition_lba_start + sector, buffer);
}

static int fat32_raw_read_sector(uint32_t sector, void *buffer) {
    if (ramdisk_available()) return ramdisk_read_sector(sector, buffer);
    return ata_read_sectors(&ata_primary_master, sector, 1, buffer);
}

static int fat32_raw_write_sector(uint32_t sector, const void *buffer) {
    if (ramdisk_available()) return ramdisk_write_sector(sector, buffer);
    return ata_write_sectors(&ata_primary_master, sector, 1, buffer);
}

static int fat32_try_mount_at_lba(uint32_t start_lba) {
    uint8_t boot_sector[512];

    g_fs.partition_lba_start = start_lba;
    if (fat32_raw_read_sector(start_lba, boot_sector) != 0) return -1;
    if (!fat32_boot_sector_looks_valid(boot_sector)) return -1;
    memcpy(&g_fs.boot, boot_sector, sizeof(g_fs.boot));
    return 0;
}

static int fat32_probe_mbr_partition_start(uint32_t *start_lba) {
    uint8_t mbr[512];
    if (fat32_raw_read_sector(0, mbr) != 0) return -1;
    if (mbr[510] != 0x55 || mbr[511] != 0xAA) return -1;

    for (int i = 0; i < 4; i++) {
        const uint8_t *ent = &mbr[446 + (i * 16)];
        uint8_t part_type = ent[4];
        uint32_t lba = (uint32_t)ent[8] |
                       ((uint32_t)ent[9] << 8) |
                       ((uint32_t)ent[10] << 16) |
                       ((uint32_t)ent[11] << 24);
        uint32_t sectors = (uint32_t)ent[12] |
                           ((uint32_t)ent[13] << 8) |
                           ((uint32_t)ent[14] << 16) |
                           ((uint32_t)ent[15] << 24);
        if (part_type == 0 || lba == 0 || sectors == 0) continue;
        *start_lba = lba;
        return 0;
    }
    return -1;
}

static int fat32_probe_gpt_partition_start(uint32_t *start_lba) {
    uint8_t hdr[512];
    if (fat32_raw_read_sector(1, hdr) != 0) return -1;
    if (memcmp(hdr, "EFI PART", 8) != 0) return -1;

    uint64_t part_entry_lba = 0;
    for (int i = 0; i < 8; i++) {
        part_entry_lba |= ((uint64_t)hdr[72 + i]) << (i * 8);
    }
    if (part_entry_lba == 0 || part_entry_lba > 0xFFFFFFFFULL) return -1;

    uint32_t entry_size = (uint32_t)hdr[84] |
                          ((uint32_t)hdr[85] << 8) |
                          ((uint32_t)hdr[86] << 16) |
                          ((uint32_t)hdr[87] << 24);
    if (entry_size < 56 || entry_size > 512) return -1;

    uint8_t entries[512];
    if (fat32_raw_read_sector((uint32_t)part_entry_lba, entries) != 0) return -1;

    int max_entries = 512 / (int)entry_size;
    for (int i = 0; i < max_entries; i++) {
        const uint8_t *ent = &entries[i * entry_size];
        int empty = 1;
        for (int b = 0; b < 16; b++) {
            if (ent[b] != 0) {
                empty = 0;
                break;
            }
        }
        if (empty) continue;

        uint64_t first_lba = 0;
        for (int b = 0; b < 8; b++) {
            first_lba |= ((uint64_t)ent[32 + b]) << (b * 8);
        }
        if (first_lba == 0 || first_lba > 0xFFFFFFFFULL) continue;
        *start_lba = (uint32_t)first_lba;
        return 0;
    }
    return -1;
}

static uint32_t fat32_count_free_clusters(void) {
    uint32_t free_clusters = 0;

    for (uint32_t cluster = 2; cluster < g_fs.total_clusters + 2; cluster++) {
        uint32_t entry = fat32_read_fat_entry(cluster);
        if (entry == FAT32_BAD_CLUSTER) return FAT32_FSINFO_UNKNOWN;
        if (entry == FAT32_FREE_CLUSTER) free_clusters++;
    }

    return free_clusters;
}

/*
 * fat32_read_cluster - read one cluster (sectors_per_cluster sectors) into
 * buffer.  cluster must be >= 2 (clusters 0 and 1 are reserved).
 * Returns 0 on success, -1 on error.
 */
int fat32_read_cluster(uint32_t cluster, void *buffer) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) return -1;

    uint32_t first_sector = g_fs.data_start_sector +
                            (cluster - 2) * g_fs.boot.sectors_per_cluster;

    for (uint8_t i = 0; i < g_fs.boot.sectors_per_cluster; i++) {
        if (fat32_read_sector(first_sector + i,
                              (uint8_t *)buffer + (i * 512)) != 0) {
            return -1;
        }
    }
    return 0;
}

/*
 * fat32_write_cluster - write one cluster (sectors_per_cluster sectors) from
 * buffer. cluster must be >= 2 (clusters 0 and 1 are reserved).
 * Returns 0 on success, -1 on error.
 */
int fat32_write_cluster(uint32_t cluster, const void *buffer) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) return -1;

    uint32_t first_sector = g_fs.data_start_sector +
                            (cluster - 2) * g_fs.boot.sectors_per_cluster;

    for (uint8_t i = 0; i < g_fs.boot.sectors_per_cluster; i++) {
        if (fat32_write_sector(first_sector + i,
                               (const uint8_t *)buffer + (i * 512)) != 0) {
            return -1;
        }
    }
    return 0;
}

/* =========================================================================
 * FAT table access
 * ======================================================================= */

/*
 * fat32_read_fat_entry - return the 28-bit FAT32 entry for cluster.
 * Returns FAT32_BAD_CLUSTER on I/O error or out-of-range cluster.
 */
uint32_t fat32_read_fat_entry(uint32_t cluster) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) {
        return FAT32_BAD_CLUSTER;
    }

    uint32_t fat_offset   = cluster * 4;
    uint32_t fat_sector   = g_fs.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (fat32_read_sector(fat_sector, sector_buffer) != 0) {
        return FAT32_BAD_CLUSTER;
    }

    return *(uint32_t *)(sector_buffer + entry_offset) & 0x0FFFFFFF;
}

/*
 * fat32_next_cluster - return the next cluster in the chain after cluster.
 * Returns 0 if cluster is the last in the chain (EOC) or is bad/free.
 */
uint32_t fat32_next_cluster(uint32_t cluster) {
    uint32_t next = fat32_read_fat_entry(cluster);

    if (next >= FAT32_EOC_MIN && next <= FAT32_EOC_MAX) return 0;
    if (next == FAT32_BAD_CLUSTER || next == FAT32_FREE_CLUSTER) return 0;

    return next;
}

/*
 * fat32_write_fat_entry - update the 28-bit FAT32 entry for cluster.
 * Writes to all FAT copies. Returns 0 on success, -1 on failure.
 */
static int fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) return -1;

    uint32_t fat_offset   = cluster * 4;
    uint32_t sector_offset = fat_offset / 512;
    uint32_t entry_offset = fat_offset % 512;
    uint32_t masked = value & 0x0FFFFFFF;

    for (uint32_t fat = 0; fat < g_fs.boot.num_fats; fat++) {
        uint32_t fat_sector = g_fs.fat_start_sector +
                              (fat * g_fs.boot.fat_size_32) +
                              sector_offset;
        if (fat32_read_sector(fat_sector, sector_buffer) != 0) return -1;

        uint32_t current = *(uint32_t *)(sector_buffer + entry_offset);
        current = (current & 0xF0000000) | masked;
        *(uint32_t *)(sector_buffer + entry_offset) = current;

        if (fat32_write_sector(fat_sector, sector_buffer) != 0) return -1;
    }
    return 0;
}

static uint32_t fat32_find_free_cluster(void) {
    uint32_t start = 2;
    uint32_t end = g_fs.total_clusters + 2;

    for (uint32_t c = start; c < end; c++) {
        if (fat32_read_fat_entry(c) == FAT32_FREE_CLUSTER) return c;
    }
    return 0;
}

static uint32_t fat32_alloc_cluster_raw(void) {
    uint32_t cluster = fat32_find_free_cluster();
    if (!cluster) return 0;
    if (fat32_write_fat_entry(cluster, FAT32_EOC_MAX) != 0) return 0;
    return cluster;
}

static int fat32_zero_cluster(uint32_t cluster) {
    memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
    return fat32_write_cluster(cluster, cluster_buffer);
}

static uint32_t fat32_alloc_cluster(void) {
    uint32_t cluster = fat32_alloc_cluster_raw();
    if (!cluster) return 0;
    if (fat32_zero_cluster(cluster) != 0) return 0;
    return cluster;
}

static uint32_t fat32_last_cluster(uint32_t first_cluster) {
    if (first_cluster < 2) return 0;
    uint32_t cluster = first_cluster;
    uint32_t next = fat32_next_cluster(cluster);
    while (next != 0) {
        cluster = next;
        next = fat32_next_cluster(cluster);
    }
    return cluster;
}

static int fat32_update_entry_cluster(uint32_t dir_cluster,
                                      uint32_t dir_index,
                                      uint32_t new_cluster) {
    if (fat32_read_cluster(dir_cluster, cluster_buffer) != 0) return -1;

    struct fat32_dir_entry *dir_entries =
        (struct fat32_dir_entry *)cluster_buffer;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));

    if ((int)dir_index < 0 || (int)dir_index >= entries_per_cluster) return -1;

    dir_entries[dir_index].first_cluster_low = (uint16_t)(new_cluster & 0xFFFF);
    dir_entries[dir_index].first_cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);

    if (fat32_write_cluster(dir_cluster, cluster_buffer) != 0) return -1;
    return 0;
}

static int fat32_split_path(const char *path,
                            uint32_t *parent_cluster,
                            char *name,
                            size_t name_cap) {
    if (!path || !name || name_cap == 0) return -1;

    uint32_t current_cluster = g_fs.current_directory;
    if (path[0] == '/') {
        current_cluster = g_fs.boot.root_cluster;
        path++;
    }

    if (parent_cluster) *parent_cluster = current_cluster;
    if (path[0] == '\0') return -1;

    char component[256];
    int  comp_len = 0;

    while (*path) {
        if (*path == '/') {
            if (comp_len > 0) {
                component[comp_len] = '\0';
                char formatted_name[11];
                if (fat32_format_name(component, formatted_name) != 0) return -1;

                struct fat32_dir_entry *entry =
                    find_entry_in_cluster(current_cluster, formatted_name, NULL);
                if (!entry) return -1;
                if (!(entry->attr & FAT32_ATTR_DIRECTORY)) return -1;

                current_cluster =
                    ((uint32_t)entry->first_cluster_high << 16) |
                     entry->first_cluster_low;
                if (parent_cluster) *parent_cluster = current_cluster;
                comp_len = 0;
            }
            path++;
            continue;
        }

        if (comp_len < 255) component[comp_len++] = *path;
        path++;
    }

    if (comp_len == 0) return -1;
    component[comp_len] = '\0';

    size_t name_len = strlen(component);
    if (name_len + 1 > name_cap) return -1;
    memcpy(name, component, name_len + 1);

    if (parent_cluster) *parent_cluster = current_cluster;
    return 0;
}

static int fat32_create_file(const char *path) {
    uint32_t parent_cluster = 0;
    char name[FAT32_MAX_FILENAME];

    if (fat32_split_path(path, &parent_cluster, name, sizeof(name)) != 0) {
        return -1;
    }

    char formatted_name[11];
    if (fat32_format_name(name, formatted_name) != 0) return -1;

    if (find_entry_in_cluster(parent_cluster, formatted_name, NULL)) {
        return -1;
    }

    if (fat32_read_cluster(parent_cluster, cluster_buffer) != 0) return -1;
    struct fat32_dir_entry *dir_entries =
        (struct fat32_dir_entry *)cluster_buffer;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));

    int free_index = -1;
    for (int i = 0; i < entries_per_cluster; i++) {
        if (dir_entries[i].name[0] == 0x00 || dir_entries[i].name[0] == 0xE5) {
            free_index = i;
            break;
        }
    }
    if (free_index < 0) return -1;

    uint32_t first_cluster = fat32_alloc_cluster_raw();
    if (!first_cluster) return -1;

    struct fat32_dir_entry *entry = &dir_entries[free_index];
    memset(entry, 0, sizeof(struct fat32_dir_entry));
    memcpy(entry->name, formatted_name, 11);
    entry->attr = FAT32_ATTR_ARCHIVE;
    entry->nt_reserved = fat32_short_name_case_flags(name);
    entry->first_cluster_low  = (uint16_t)(first_cluster & 0xFFFF);
    entry->first_cluster_high = (uint16_t)((first_cluster >> 16) & 0xFFFF);
    entry->file_size = 0;

    if (fat32_write_cluster(parent_cluster, cluster_buffer) != 0) return -1;
    if (fat32_zero_cluster(first_cluster) != 0) return -1;

    return 0;
}

/* =========================================================================
 * Name formatting and parsing
 * ======================================================================= */

/*
 * fat32_format_name - convert a POSIX filename to 8.3 FAT short-name format.
 * The output formatted is 11 bytes, space-padded, upper-cased.
 * Returns 0 on success, -1 if the name exceeds 8.3 limits.
 */
static int fat32_component_has_lowercase(const char *component, int len) {
    for (int i = 0; i < len; i++) {
        if (component[i] >= 'a' && component[i] <= 'z') return 1;
    }
    return 0;
}

static int fat32_component_has_uppercase(const char *component, int len) {
    for (int i = 0; i < len; i++) {
        if (component[i] >= 'A' && component[i] <= 'Z') return 1;
    }
    return 0;
}

static uint8_t fat32_short_name_case_flags(const char *filename) {
    const char *dot      = strstr(filename, ".");
    int         name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    int         ext_len  = dot ? (int)strlen(dot + 1) : 0;
    uint8_t     flags    = 0;

    if (name_len > 0 &&
        fat32_component_has_lowercase(filename, name_len) &&
        !fat32_component_has_uppercase(filename, name_len)) {
        flags |= FAT32_NTRES_LOWER_BASE;
    }

    if (ext_len > 0 &&
        fat32_component_has_lowercase(dot + 1, ext_len) &&
        !fat32_component_has_uppercase(dot + 1, ext_len)) {
        flags |= FAT32_NTRES_LOWER_EXT;
    }

    return flags;
}

int fat32_format_name(const char *filename, char *formatted) {
    memset(formatted, ' ', 11);

    const char *dot      = strstr(filename, ".");
    int         name_len = dot ? (int)(dot - filename) : (int)strlen(filename);
    int         ext_len  = dot ? (int)strlen(dot + 1) : 0;

    if (name_len > 8 || ext_len > 3) return -1;

    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        formatted[i] = c;
    }

    if (dot) {
        for (int i = 0; i < ext_len; i++) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') c -= 32;
            formatted[8 + i] = c;
        }
    }

    return 0;
}

/*
 * fat32_parse_short_name - convert an 11-byte 8.3 short name to a
 * null-terminated string with a '.' separator if an extension is present.
 * Honors the short-name case bits stored in nt_reserved.
 */
void fat32_parse_short_name(const uint8_t *short_name,
                            uint8_t nt_reserved,
                            char *long_name) {
    int pos = 0;
    int name_lower = (nt_reserved & FAT32_NTRES_LOWER_BASE) != 0;
    int ext_lower  = (nt_reserved & FAT32_NTRES_LOWER_EXT) != 0;

    for (int i = 0; i < 8 && short_name[i] != ' '; i++) {
        char c = (char)short_name[i];
        if (name_lower && c >= 'A' && c <= 'Z') c = (char)(c + 32);
        long_name[pos++] = c;
    }

    if (short_name[8] != ' ') {
        long_name[pos++] = '.';
        for (int i = 8; i < 11 && short_name[i] != ' '; i++) {
            char c = (char)short_name[i];
            if (ext_lower && c >= 'A' && c <= 'Z') c = (char)(c + 32);
            long_name[pos++] = c;
        }
    }

    long_name[pos] = '\0';
}

/* =========================================================================
 * Internal directory search helpers
 * ======================================================================= */

/*
 * find_entry_in_cluster - scan one cluster's worth of directory entries for
 * a name matching formatted_name (11 bytes, 8.3 format).
 *
 * Returns a pointer to a static copy of the matching entry, or NULL if not
 * found.  The copy is overwritten on each call.
 */
static struct fat32_dir_entry *find_entry_in_cluster(uint32_t cluster,
                                                     char *formatted_name,
                                                     int *entry_index) {
    static struct fat32_dir_entry result;

    if (fat32_read_cluster(cluster, cluster_buffer) != 0) return NULL;

    struct fat32_dir_entry *dir_entries =
        (struct fat32_dir_entry *)cluster_buffer;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));

    for (int i = 0; i < entries_per_cluster; i++) {
        struct fat32_dir_entry *entry = &dir_entries[i];

        if (entry->name[0] == 0x00) break;           /* end of directory */
        if (entry->name[0] == 0xE5) continue;         /* deleted entry    */
        if (entry->attr == FAT32_ATTR_LONG_NAME) continue; /* LFN entry   */

        if (memcmp(entry->name, formatted_name, 11) == 0) {
            memcpy(&result, entry, sizeof(struct fat32_dir_entry));
            if (entry_index) *entry_index = i;
            return &result;
        }
    }

    return NULL;
}

/*
 * find_entry - traverse path components from the current (or root) directory,
 * returning a pointer to the final directory entry, or NULL if not found.
 *
 * If parent_cluster is non-NULL, it receives the cluster number of the
 * directory containing the returned entry.
 *
 * Paths beginning with '/' are resolved from the root cluster.
 * All other paths are resolved from g_fs.current_directory.
 */
static struct fat32_dir_entry *find_entry(const char *path,
                                          uint32_t   *parent_cluster,
                                          int        *entry_index) {
    char     formatted_name[11];
    uint32_t current_cluster = g_fs.current_directory;

    if (path[0] == '/') {
        current_cluster = g_fs.boot.root_cluster;
        path++;
    }

    if (parent_cluster) *parent_cluster = current_cluster;
    if (path[0] == '\0') return NULL;

    /* Walk each path component separated by '/' */
    char component[256];
    int  comp_len = 0;

    while (*path) {
        if (*path == '/') {
            if (comp_len > 0) {
                component[comp_len] = '\0';
                if (fat32_format_name(component, formatted_name) != 0) return NULL;

                struct fat32_dir_entry *entry =
                    find_entry_in_cluster(current_cluster, formatted_name, NULL);
                if (!entry) return NULL;
                if (!(entry->attr & FAT32_ATTR_DIRECTORY)) return NULL;

                if (parent_cluster) *parent_cluster = current_cluster;
                current_cluster =
                    ((uint32_t)entry->first_cluster_high << 16) |
                     entry->first_cluster_low;
                comp_len = 0;
            }
            path++;
        } else {
            if (comp_len < 255) component[comp_len++] = *path;
            path++;
        }
    }

    /* Resolve the final component */
    if (comp_len > 0) {
        component[comp_len] = '\0';
        if (fat32_format_name(component, formatted_name) != 0) return NULL;
        return find_entry_in_cluster(current_cluster, formatted_name, entry_index);
    }

    return NULL;
}

/* =========================================================================
 * Filesystem mount / unmount
 * ======================================================================= */

/*
 * fat32_init - verify a disk is present and clear driver state.
 * Returns 0 on success, -1 if no ATA disk is detected.
 */
int fat32_init(void) {
    vga_writestring("FAT32: Initializing filesystem driver...\n");

    memset(&g_fs,      0, sizeof(g_fs));
    memset(g_fd_table, 0, sizeof(g_fd_table));

    if (!ata_primary_master.exists && !ramdisk_available()) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAT32: ERROR - No disk detected!\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return -1;
    }

    vga_writestring("FAT32: Driver initialized\n");
    return 0;
}

/*
 * fat32_mount - read the boot sector, validate the FAT32 signature, and
 * compute all derived layout fields.  Reads the FSInfo sector if present.
 * Returns 0 on success, -1 on validation or I/O failure.
 */
int fat32_mount(void) {
    vga_writestring("FAT32: Mounting filesystem...\n");
    g_fs.partition_lba_start = 0;
    if (fat32_try_mount_at_lba(0) != 0) {
        uint32_t part_lba = 0;
        if (fat32_probe_mbr_partition_start(&part_lba) == 0 &&
            fat32_try_mount_at_lba(part_lba) == 0) {
            vga_writestring("FAT32: Using MBR partition at LBA ");
            print_dec(part_lba);
            vga_writestring("\n");
        } else if (fat32_probe_gpt_partition_start(&part_lba) == 0 &&
                   fat32_try_mount_at_lba(part_lba) == 0) {
            vga_writestring("FAT32: Using GPT partition at LBA ");
            print_dec(part_lba);
            vga_writestring("\n");
        } else {
            vga_writestring("FAT32: Failed to read FAT32 boot sector\n");
            return -1;
        }
    }

    /* Compute layout constants */
    g_fs.fat_start_sector  = g_fs.boot.reserved_sectors;
    g_fs.data_start_sector = g_fs.fat_start_sector +
                             (g_fs.boot.num_fats * g_fs.boot.fat_size_32);
    g_fs.first_data_sector = g_fs.data_start_sector;

    uint32_t total_sectors  = g_fs.boot.total_sectors_32;
    g_fs.data_sectors       = total_sectors - g_fs.data_start_sector;
    g_fs.total_clusters     = g_fs.data_sectors / g_fs.boot.sectors_per_cluster;
    g_fs.bytes_per_cluster  = g_fs.boot.sectors_per_cluster *
                              g_fs.boot.bytes_per_sector;

    /* Read FSInfo if the boot sector points to a valid sector */
    if (g_fs.boot.fs_info_sector != 0 &&
        g_fs.boot.fs_info_sector != 0xFFFF) {
        fat32_read_sector(g_fs.boot.fs_info_sector, &g_fs.fsinfo);
    }

    if (g_fs.bytes_per_cluster > sizeof(cluster_buffer)) {
        vga_writestring("FAT32: Cluster size too large\n");
        return -1;
    }

    g_fs.current_directory = g_fs.boot.root_cluster;
    struct fat32_dir_entry *home = find_entry("/home", NULL, NULL);
    if (home && (home->attr & FAT32_ATTR_DIRECTORY)) {
        g_fs.current_directory =
            ((uint32_t)home->first_cluster_high << 16) |
             home->first_cluster_low;
    }
    g_fs.mounted           = 1;

    vga_writestring("FAT32: Mounted - clusters=");
    print_dec(g_fs.total_clusters);
    vga_writestring(" cluster_size=");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring("B\n");

    return 0;
}

uint32_t fat32_get_current_directory(void) {
    return g_fs.current_directory;
}

void fat32_set_current_directory(uint32_t cluster) {
    g_fs.current_directory = cluster;
}

/* =========================================================================
 * File operations
 * ======================================================================= */

static int fat32_update_entry_size(uint32_t dir_cluster,
                                   uint32_t dir_index,
                                   uint32_t new_size) {
    if (fat32_read_cluster(dir_cluster, cluster_buffer) != 0) return -1;

    struct fat32_dir_entry *dir_entries =
        (struct fat32_dir_entry *)cluster_buffer;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));

    if ((int)dir_index < 0 || (int)dir_index >= entries_per_cluster) return -1;
    dir_entries[dir_index].file_size = new_size;

    if (fat32_write_cluster(dir_cluster, cluster_buffer) != 0) return -1;
    return 0;
}

/*
 * fat32_open - open the file at path for reading or writing.
 * Returns a non-negative file descriptor on success, -1 on failure.
 */
int fat32_open(const char *path, int flags) {
    if (!g_fs.mounted) return -1;

    uint32_t parent_cluster = 0;
    int      entry_index    = -1;
    struct fat32_dir_entry *entry = find_entry(path, &parent_cluster, &entry_index);
    if (!entry) {
        if (flags & FAT32_O_CREAT) {
            if (fat32_create_file(path) != 0) return -1;
            entry_index = -1;
            parent_cluster = 0;
            entry = find_entry(path, &parent_cluster, &entry_index);
        }
        if (!entry) return -1;
    }
    if (entry_index < 0) return -1;
    if (entry->attr & FAT32_ATTR_DIRECTORY) return -1;  /* not a file */
    if ((flags & (FAT32_O_WRONLY | FAT32_O_RDWR)) &&
        (entry->attr & FAT32_ATTR_READ_ONLY)) return -1;

    /* Find a free descriptor slot */
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_fd_table[i].in_use) { fd = i; break; }
    }
    if (fd < 0) return -1;  /* no free slots */

    uint32_t cluster = ((uint32_t)entry->first_cluster_high << 16) |
                        entry->first_cluster_low;

    fat32_parse_short_name(entry->name, entry->nt_reserved, g_fd_table[fd].name);
    g_fd_table[fd].first_cluster   = cluster;
    g_fd_table[fd].current_cluster = cluster;
    g_fd_table[fd].size            = entry->file_size;
    g_fd_table[fd].position        = 0;
    g_fd_table[fd].dir_cluster     = parent_cluster;
    g_fd_table[fd].dir_index       = (uint32_t)entry_index;
    g_fd_table[fd].attr            = entry->attr;
    g_fd_table[fd].flags           = flags;
    g_fd_table[fd].in_use          = 1;

    uint32_t cap = 0;
    uint32_t scan = cluster;
    while (scan != 0) {
        cap += g_fs.bytes_per_cluster;
        scan = fat32_next_cluster(scan);
    }
    g_fd_table[fd].capacity = cap;

    if (flags & FAT32_O_APPEND) {
        g_fd_table[fd].position = g_fd_table[fd].size;
    }

    if (flags & FAT32_O_TRUNC) {
        g_fd_table[fd].size = 0;
        g_fd_table[fd].position = 0;
        fat32_update_entry_size(parent_cluster, (uint32_t)entry_index, 0);
    }

    return fd;
}

/*
 * fat32_close - release an open file descriptor.
 * Returns 0 on success, -1 if fd is invalid or not open.
 */
int fat32_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -1;
    if (!g_fd_table[fd].in_use) return -1;
    memset(&g_fd_table[fd], 0, sizeof(struct fat32_file));
    return 0;
}

/*
 * fat32_read - read up to count bytes from an open file descriptor into buf.
 *
 * Walks the FAT cluster chain from the cluster containing the current file
 * position.  Each cluster is read into the static cluster_buffer and the
 * relevant portion is copied to the caller's buffer.
 *
 * Returns the number of bytes read, 0 at EOF, or -1 on error.
 */
ssize_t fat32_read(int fd, void *buf, size_t count) {
    if (!g_fs.mounted) return -1;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !g_fd_table[fd].in_use) return -1;

    uint8_t  *out      = (uint8_t *)buf;
    uint32_t  pos      = g_fd_table[fd].position;
    uint32_t  filesize = g_fd_table[fd].size;
    uint32_t  bpc      = g_fs.bytes_per_cluster;
    ssize_t   total    = 0;

    if (pos >= filesize) return 0;  /* already at EOF */
    if ((uint32_t)count > filesize - pos) count = filesize - pos;

    /* Advance the cluster chain to the cluster containing pos */
    uint32_t target_idx = pos / bpc;
    uint32_t cluster    = g_fd_table[fd].first_cluster;

    for (uint32_t i = 0; i < target_idx; i++) {
        cluster = fat32_next_cluster(cluster);
        if (cluster == 0) return -1;
    }

    uint32_t offset_in_cluster = pos % bpc;

    while ((size_t)total < count) {
        if (cluster == 0) break;

        if (fat32_read_cluster(cluster, cluster_buffer) != 0) {
            return (total > 0) ? total : -1;
        }

        uint32_t avail     = bpc - offset_in_cluster;
        size_t   remaining = count - (size_t)total;
        if (avail > (uint32_t)remaining) avail = (uint32_t)remaining;

        memcpy(out + total, cluster_buffer + offset_in_cluster, avail);
        total += (ssize_t)avail;

        cluster            = fat32_next_cluster(cluster);
        offset_in_cluster  = 0;
    }

    g_fd_table[fd].position = pos + (uint32_t)total;
    return total;
}

/*
 * fat32_write - write up to count bytes to an open file descriptor from buf.
 *
 * Writes are limited to the file's allocated cluster chain. If the write
 * extends the file, the directory entry size is updated.
 *
 * Returns the number of bytes written, or -1 on error.
 */
ssize_t fat32_write(int fd, const void *buf, size_t count) {
    if (!g_fs.mounted) return -1;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !g_fd_table[fd].in_use) return -1;
    if (!(g_fd_table[fd].flags & (FAT32_O_WRONLY | FAT32_O_RDWR))) return -1;
    if (!buf) return -1;
    if (!count) return 0;

    const uint8_t *in = (const uint8_t *)buf;
    uint32_t pos      = g_fd_table[fd].position;
    uint32_t bpc      = g_fs.bytes_per_cluster;
    ssize_t  total    = 0;

    uint32_t first_cluster = g_fd_table[fd].first_cluster;
    if (first_cluster == 0) {
        uint32_t new_cluster = fat32_alloc_cluster();
        if (!new_cluster) return -1;
        if (fat32_update_entry_cluster(g_fd_table[fd].dir_cluster,
                                       g_fd_table[fd].dir_index,
                                       new_cluster) != 0) {
            return -1;
        }
        g_fd_table[fd].first_cluster = new_cluster;
        g_fd_table[fd].current_cluster = new_cluster;
        g_fd_table[fd].capacity = bpc;
        first_cluster = new_cluster;
    }

    uint32_t end_pos = pos + (uint32_t)count;
    if (end_pos < pos) return -1;

    uint32_t cap = g_fd_table[fd].capacity;
    if (end_pos > cap) {
        uint32_t extra = end_pos - cap;
        uint32_t add_clusters = (extra + bpc - 1) / bpc;
        uint32_t last = fat32_last_cluster(first_cluster);
        if (!last) return -1;

        for (uint32_t i = 0; i < add_clusters; i++) {
            uint32_t new_cluster = fat32_alloc_cluster();
            if (!new_cluster) return -1;
            if (fat32_write_fat_entry(last, new_cluster) != 0) return -1;
            last = new_cluster;
        }

        g_fd_table[fd].capacity = cap + (add_clusters * bpc);
        cap = g_fd_table[fd].capacity;
    }

    uint32_t target_idx = pos / bpc;
    uint32_t cluster    = first_cluster;

    for (uint32_t i = 0; i < target_idx; i++) {
        cluster = fat32_next_cluster(cluster);
        if (cluster == 0) return -1;
    }

    uint32_t offset_in_cluster = pos % bpc;

    while ((size_t)total < count) {
        if (cluster == 0) break;

        if (fat32_read_cluster(cluster, cluster_buffer) != 0) {
            return (total > 0) ? total : -1;
        }

        uint32_t avail     = bpc - offset_in_cluster;
        size_t   remaining = count - (size_t)total;
        if (avail > (uint32_t)remaining) avail = (uint32_t)remaining;

        memcpy(cluster_buffer + offset_in_cluster, in + total, avail);
        if (fat32_write_cluster(cluster, cluster_buffer) != 0) {
            return (total > 0) ? total : -1;
        }

        total += (ssize_t)avail;
        cluster = fat32_next_cluster(cluster);
        offset_in_cluster = 0;
    }

    g_fd_table[fd].position = pos + (uint32_t)total;

    uint32_t new_size = g_fd_table[fd].size;
    if (g_fd_table[fd].position > new_size) new_size = g_fd_table[fd].position;

    if (new_size != g_fd_table[fd].size) {
        if (fat32_update_entry_size(g_fd_table[fd].dir_cluster,
                                    g_fd_table[fd].dir_index,
                                    new_size) != 0) {
            return -1;
        }
        g_fd_table[fd].size = new_size;
    }

    return total;
}

/*
 * fat32_stat - fill in a fat32_dirent for the file or directory at path.
 * Returns 0 on success, -1 if not found.
 */
int fat32_stat(const char *path, struct fat32_dirent *stat) {
    if (!g_fs.mounted) return -1;

    struct fat32_dir_entry *entry = find_entry(path, NULL, NULL);
    if (!entry) return -1;

    fat32_parse_short_name(entry->name, entry->nt_reserved, stat->name);
    stat->size    = entry->file_size;
    stat->attr    = entry->attr;
    stat->cluster = ((uint32_t)entry->first_cluster_high << 16) |
                     entry->first_cluster_low;
    return 0;
}

/*
 * fat32_chdir - change the current working directory.
 * Accepts "/" to return to the root cluster.
 * Returns 0 on success, -1 if path is not a directory or not found.
 */
int fat32_chdir(const char *path) {
    if (!g_fs.mounted) return -1;

    if (strcmp(path, "/") == 0) {
        g_fs.current_directory = g_fs.boot.root_cluster;
        return 0;
    }

    struct fat32_dir_entry *entry = find_entry(path, NULL, NULL);
    if (!entry) return -1;
    if (!(entry->attr & FAT32_ATTR_DIRECTORY)) return -1;

    g_fs.current_directory =
        ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
    return 0;
}

/*
 * fat32_readdir - fill entries with up to max_entries directory entries from
 * the current directory cluster.
 * Skips deleted, LFN, and dot entries.
 * Returns the number of entries filled, or -1 on error.
 */
int fat32_readdir(struct fat32_dirent *entries, int max_entries) {
    if (!g_fs.mounted) return -1;

    if (fat32_read_cluster(g_fs.current_directory, cluster_buffer) != 0) {
        return -1;
    }

    struct fat32_dir_entry *dir_entries =
        (struct fat32_dir_entry *)cluster_buffer;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));
    int count = 0;

    for (int i = 0; i < entries_per_cluster && count < max_entries; i++) {
        struct fat32_dir_entry *e = &dir_entries[i];

        if (e->name[0] == 0x00) break;                  /* end of directory */
        if (e->name[0] == 0xE5) continue;                /* deleted         */
        if (e->attr == FAT32_ATTR_LONG_NAME) continue;   /* LFN             */
        if (e->name[0] == '.') continue;                 /* . and ..        */

        fat32_parse_short_name(e->name, e->nt_reserved, entries[count].name);
        entries[count].size    = e->file_size;
        entries[count].attr    = e->attr;
        entries[count].cluster = ((uint32_t)e->first_cluster_high << 16) |
                                  e->first_cluster_low;
        count++;
    }

    return count;
}

/*
 * fat32_list_directory - print the contents of path (or the current directory
 * if path is empty) to the VGA console.
 */
void fat32_list_directory(const char *path) {
    struct fat32_dirent entries[64];
    uint32_t saved_dir = g_fs.current_directory;

    if (path && path[0] != '\0') {
        if (fat32_chdir(path) != 0) {
            vga_writestring("Directory not found\n");
            return;
        }
    }

    int count = fat32_readdir(entries, 64);
    if (count < 0) {
        vga_writestring("Failed to read directory\n");
        g_fs.current_directory = saved_dir;
        return;
    }

    vga_writestring("\nDirectory listing:\n");
    vga_writestring("==================\n");

    for (int i = 0; i < count; i++) {
        if (entries[i].attr & FAT32_ATTR_DIRECTORY) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("[DIR]  ");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            vga_writestring("[FILE] ");
        }

        vga_writestring(entries[i].name);

        if (!(entries[i].attr & FAT32_ATTR_DIRECTORY)) {
            vga_writestring(" (");
            print_dec(entries[i].size);
            vga_writestring(" bytes)");
        }
        vga_writestring("\n");
    }

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("\nTotal: ");
    print_dec((uint64_t)count);
    vga_writestring(" entries\n");

    g_fs.current_directory = saved_dir;
}

/* =========================================================================
 * Recursive directory listing
 * ========================================================================= */

#define FAT32_LIST_MAX_DEPTH 8

static void fat32_print_indent(int depth) {
    for (int i = 0; i < depth; i++) {
        vga_writestring("  ");
    }
}

static void fat32_list_cluster_recursive(uint32_t cluster,
                                         const char *path,
                                         int depth,
                                         int max_depth) {
    if (!g_fs.mounted) return;
    if (depth > max_depth) return;
    if (fat32_read_cluster(cluster, cluster_buffer) != 0) {
        fat32_print_indent(depth);
        vga_writestring("[ERR] ");
        vga_writestring(path);
        vga_writestring("\n");
        return;
    }

    uint8_t *dir_copy = (uint8_t *)kmalloc(g_fs.bytes_per_cluster);
    if (!dir_copy) {
        fat32_print_indent(depth);
        vga_writestring("[ERR] out of memory\n");
        return;
    }
    memcpy(dir_copy, cluster_buffer, g_fs.bytes_per_cluster);

    struct fat32_dir_entry *dir_entries =
        (struct fat32_dir_entry *)dir_copy;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));

    for (int i = 0; i < entries_per_cluster; i++) {
        struct fat32_dir_entry *e = &dir_entries[i];

        if (e->name[0] == 0x00) break;                /* end of directory */
        if (e->name[0] == 0xE5) continue;             /* deleted         */
        if (e->attr == FAT32_ATTR_LONG_NAME) continue;/* LFN             */
        if (e->name[0] == '.') continue;              /* . and ..        */

        char name[FAT32_MAX_FILENAME];
        fat32_parse_short_name(e->name, e->nt_reserved, name);

        fat32_print_indent(depth);
        int is_dir = (e->attr & FAT32_ATTR_DIRECTORY) ? 1 : 0;

        if (is_dir) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("[DIR]  ");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            vga_writestring("[FILE] ");
        }
        vga_writestring(name);

        if (!is_dir) {
            vga_writestring(" (");
            print_dec(e->file_size);
            vga_writestring(" bytes)");
        }
        vga_writestring("\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

        if (is_dir && depth < max_depth) {
            char child_path[FAT32_MAX_PATH];
            size_t pos = 0;
            size_t base_len = strlen(path);
            size_t name_len = strlen(name);

            if (base_len == 1 && path[0] == '/') {
                child_path[pos++] = '/';
            } else {
                if (base_len + 1 < sizeof(child_path)) {
                    memcpy(child_path, path, base_len);
                    pos += base_len;
                    child_path[pos++] = '/';
                }
            }

            if (pos + name_len < sizeof(child_path)) {
                memcpy(child_path + pos, name, name_len);
                pos += name_len;
                child_path[pos] = '\0';
            } else {
                child_path[pos ? pos - 1 : 0] = '\0';
            }

            uint32_t child_cluster =
                ((uint32_t)e->first_cluster_high << 16) |
                 e->first_cluster_low;
            fat32_list_cluster_recursive(child_cluster,
                                         child_path,
                                         depth + 1,
                                         max_depth);
        }
    }
    kfree(dir_copy);
}

void fat32_list_directory_recursive(const char *path) {
    if (!g_fs.mounted) {
        vga_writestring("FAT32: Not mounted\n");
        return;
    }

    const char *start_path = path && path[0] ? path : "/";
    uint32_t start_cluster = g_fs.boot.root_cluster;

    if (!(start_path[0] == '/' && start_path[1] == '\0')) {
        struct fat32_dir_entry *entry = find_entry(start_path, NULL, NULL);
        if (!entry) {
            vga_writestring("Directory not found\n");
            return;
        }
        if (!(entry->attr & FAT32_ATTR_DIRECTORY)) {
            vga_writestring("Not a directory\n");
            return;
        }
        start_cluster =
            ((uint32_t)entry->first_cluster_high << 16) |
             entry->first_cluster_low;
    }

    vga_writestring("\nRecursive directory listing:\n");
    vga_writestring("=============================\n");
    vga_writestring(start_path);
    vga_writestring("\n");

    fat32_list_cluster_recursive(start_cluster,
                                 start_path,
                                 1,
                                 FAT32_LIST_MAX_DEPTH);
}

/* =========================================================================
 * FSInfo helpers
 * ======================================================================= */

/*
 * fat32_get_free_clusters - return the free cluster count from FSInfo.
 */
uint32_t fat32_get_free_clusters(void) {
    if (g_fs.fsinfo.free_clusters == FAT32_FSINFO_UNKNOWN ||
        g_fs.fsinfo.free_clusters > g_fs.total_clusters) {
        g_fs.fsinfo.free_clusters = fat32_count_free_clusters();
    }

    return g_fs.fsinfo.free_clusters;
}

/* =========================================================================
 * Diagnostics
 * ======================================================================= */

/*
 * fat32_print_info - write volume label, cluster count, free space, and
 * cluster size to the VGA console.
 */
void fat32_print_info(void) {
    if (!g_fs.mounted) {
        vga_writestring("FAT32: Not mounted\n");
        return;
    }

    uint32_t free_clusters = fat32_get_free_clusters();

    vga_writestring("\nFAT32 Filesystem Information:\n");
    vga_writestring("==============================\n");

    vga_writestring("Volume Label: ");
    for (int i = 0; i < 11; i++) {
        uint8_t c = g_fs.boot.volume_label[i];
        if (c != ' ' && c != 0) vga_putchar((char)c);
    }
    vga_writestring("\n");

    vga_writestring("Total Clusters: ");
    print_dec(g_fs.total_clusters);
    vga_writestring("\n");

    vga_writestring("Free Clusters:  ");
    print_dec(free_clusters);
    vga_writestring("\n");

    vga_writestring("Cluster Size:   ");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring(" bytes\n");

    vga_writestring("Total Size:     ");
    print_dec((uint64_t)g_fs.total_clusters *
              g_fs.bytes_per_cluster / (1024 * 1024));
    vga_writestring(" MB\n");

    vga_writestring("Free Space:     ");
    print_dec((uint64_t)free_clusters *
              g_fs.bytes_per_cluster / (1024 * 1024));
    vga_writestring(" MB\n");
}
