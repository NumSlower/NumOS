/*
 * fat32.c - FAT32 Filesystem Driver
 *
 * Implements a read-oriented FAT32 driver sufficient to locate, open, and
 * read files from the NumOS disk image.  Write support stubs are present
 * for directory and file creation but are not exercised by the boot path.
 *
 * Key data flow for a read:
 *   fat32_open()        - locate the directory entry, fill a fat32_file slot
 *   fat32_read()        - walk the FAT cluster chain, copy data to the caller
 *   fat32_close()       - release the file descriptor slot
 *
 * All sector I/O goes through fat32_read_sector() / fat32_write_sector()
 * which delegate to the ATA driver.
 *
 * Note: Per-cluster debug prints were removed from fat32_read_cluster().
 * They fired on every cluster read during ELF loading and flooded the
 * VGA console with thousands of lines, making the boot appear to hang.
 */

#include "fs/fat32.h"
#include "drivers/ata.h"
#include "drivers/vga.h"
#include "kernel/kernel.h"
#include "cpu/heap.h"

/* =========================================================================
 * Module state
 * ======================================================================= */

static struct fat32_fs g_fs = {0};          /* mounted filesystem state  */

#define MAX_OPEN_FILES 16
static struct fat32_file g_fd_table[MAX_OPEN_FILES] = {0}; /* open files  */

/* Working sector and cluster I/O buffers; aligned for DMA safety */
static uint8_t sector_buffer[512]  __attribute__((aligned(16)));
static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));

/* =========================================================================
 * Low-level sector and cluster I/O
 * ======================================================================= */

/*
 * fat32_read_sector - read one 512-byte sector from the ATA primary master.
 * Returns 0 on success, -1 on error.
 */
int fat32_read_sector(uint32_t sector, void *buffer) {
    return ata_read_sectors(&ata_primary_master, sector, 1, buffer);
}

/*
 * fat32_write_sector - write one 512-byte sector to the ATA primary master.
 * Returns 0 on success, -1 on error.
 */
int fat32_write_sector(uint32_t sector, const void *buffer) {
    return ata_write_sectors(&ata_primary_master, sector, 1, buffer);
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
 * fat32_write_cluster - write one cluster to disk.
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
 * fat32_write_fat_entry - write value to the FAT entry for cluster.
 * The upper 4 bits of the existing entry are preserved (FAT32 spec requirement).
 * Writes to both FAT copies when num_fats == 2.
 */
void fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) return;

    value &= 0x0FFFFFFF;

    uint32_t fat_offset   = cluster * 4;
    uint32_t fat_sector   = g_fs.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (fat32_read_sector(fat_sector, sector_buffer) != 0) return;

    uint32_t *entry_ptr = (uint32_t *)(sector_buffer + entry_offset);
    *entry_ptr = (*entry_ptr & 0xF0000000) | value;

    fat32_write_sector(fat_sector, sector_buffer);

    /* Mirror write to the second FAT copy */
    if (g_fs.boot.num_fats == 2) {
        fat32_write_sector(fat_sector + g_fs.boot.fat_size_32, sector_buffer);
    }

    g_fs.fat_cache_dirty = 1;
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
 * fat32_alloc_cluster - find and allocate a free cluster.
 * Initialises the cluster to zero and marks it EOC in the FAT.
 * Updates the FSInfo next-free-cluster hint.
 * Returns the allocated cluster number, or 0 if the disk is full.
 */
uint32_t fat32_alloc_cluster(void) {
    uint32_t start = g_fs.fsinfo.next_free_cluster;
    if (start < 2) start = 2;

    /* Forward scan from hint */
    for (uint32_t c = start; c < g_fs.total_clusters + 2; c++) {
        if (fat32_read_fat_entry(c) == FAT32_FREE_CLUSTER) {
            fat32_write_fat_entry(c, FAT32_EOC_MAX);
            g_fs.fsinfo.next_free_cluster = c + 1;
            if (g_fs.fsinfo.free_clusters > 0) g_fs.fsinfo.free_clusters--;
            memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
            fat32_write_cluster(c, cluster_buffer);
            return c;
        }
    }

    /* Wrap-around scan from cluster 2 to the hint */
    for (uint32_t c = 2; c < start; c++) {
        if (fat32_read_fat_entry(c) == FAT32_FREE_CLUSTER) {
            fat32_write_fat_entry(c, FAT32_EOC_MAX);
            g_fs.fsinfo.next_free_cluster = c + 1;
            if (g_fs.fsinfo.free_clusters > 0) g_fs.fsinfo.free_clusters--;
            memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
            fat32_write_cluster(c, cluster_buffer);
            return c;
        }
    }

    return 0;  /* disk full */
}

/*
 * fat32_free_cluster_chain - mark every cluster in the chain starting at
 * start_cluster as free in the FAT.
 */
void fat32_free_cluster_chain(uint32_t start_cluster) {
    uint32_t cluster = start_cluster;
    while (cluster != 0 && cluster >= 2 &&
           cluster < g_fs.total_clusters + 2) {
        uint32_t next = fat32_next_cluster(cluster);
        fat32_write_fat_entry(cluster, FAT32_FREE_CLUSTER);
        g_fs.fsinfo.free_clusters++;
        cluster = next;
    }
}

/* =========================================================================
 * Name formatting and parsing
 * ======================================================================= */

/*
 * fat32_format_name - convert a POSIX filename to 8.3 FAT short-name format.
 * The output formatted is 11 bytes, space-padded, upper-cased.
 * Returns 0 on success, -1 if the name exceeds 8.3 limits.
 */
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
 */
void fat32_parse_short_name(const uint8_t *short_name, char *long_name) {
    int pos = 0;

    for (int i = 0; i < 8 && short_name[i] != ' '; i++) {
        long_name[pos++] = (char)short_name[i];
    }

    if (short_name[8] != ' ') {
        long_name[pos++] = '.';
        for (int i = 8; i < 11 && short_name[i] != ' '; i++) {
            long_name[pos++] = (char)short_name[i];
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
                                                     char *formatted_name) {
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
                                          uint32_t   *parent_cluster) {
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
                    find_entry_in_cluster(current_cluster, formatted_name);
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
        return find_entry_in_cluster(current_cluster, formatted_name);
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

    if (!ata_primary_master.exists) {
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

    if (fat32_read_sector(0, &g_fs.boot) != 0) {
        vga_writestring("FAT32: Failed to read boot sector\n");
        return -1;
    }

    if (strncmp((const char *)g_fs.boot.fs_type, "FAT32   ", 8) != 0) {
        vga_writestring("FAT32: Invalid filesystem type\n");
        return -1;
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

    g_fs.current_directory = g_fs.boot.root_cluster;
    g_fs.mounted           = 1;

    vga_writestring("FAT32: Mounted - clusters=");
    print_dec(g_fs.total_clusters);
    vga_writestring(" cluster_size=");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring("B\n");

    return 0;
}

/*
 * fat32_unmount - flush FSInfo if dirty, close all open files, flush the
 * ATA write cache, and mark the filesystem as unmounted.
 */
void fat32_unmount(void) {
    if (!g_fs.mounted) return;

    if (g_fs.fat_cache_dirty) fat32_update_fsinfo();

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (g_fd_table[i].in_use) fat32_close(i);
    }

    ata_flush_cache(&ata_primary_master);
    g_fs.mounted = 0;
    vga_writestring("FAT32: Unmounted\n");
}

/* =========================================================================
 * File operations
 * ======================================================================= */

/*
 * fat32_open - open the file at path for reading.
 * Returns a non-negative file descriptor on success, -1 on failure.
 */
int fat32_open(const char *path, int flags) {
    if (!g_fs.mounted) return -1;

    struct fat32_dir_entry *entry = find_entry(path, NULL);
    if (!entry) return -1;
    if (entry->attr & FAT32_ATTR_DIRECTORY) return -1;  /* not a file */

    /* Find a free descriptor slot */
    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_fd_table[i].in_use) { fd = i; break; }
    }
    if (fd < 0) return -1;  /* no free slots */

    uint32_t cluster = ((uint32_t)entry->first_cluster_high << 16) |
                        entry->first_cluster_low;

    fat32_parse_short_name(entry->name, g_fd_table[fd].name);
    g_fd_table[fd].first_cluster   = cluster;
    g_fd_table[fd].current_cluster = cluster;
    g_fd_table[fd].size            = entry->file_size;
    g_fd_table[fd].position        = 0;
    g_fd_table[fd].attr            = entry->attr;
    g_fd_table[fd].flags           = flags;
    g_fd_table[fd].in_use          = 1;

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
 * fat32_stat - fill in a fat32_dirent for the file or directory at path.
 * Returns 0 on success, -1 if not found.
 */
int fat32_stat(const char *path, struct fat32_dirent *stat) {
    if (!g_fs.mounted) return -1;

    struct fat32_dir_entry *entry = find_entry(path, NULL);
    if (!entry) return -1;

    fat32_parse_short_name(entry->name, stat->name);
    stat->size    = entry->file_size;
    stat->attr    = entry->attr;
    stat->cluster = ((uint32_t)entry->first_cluster_high << 16) |
                     entry->first_cluster_low;
    return 0;
}

/* ---- Write / seek / unlink stubs (not implemented) -------------------- */

/* These operations are not needed for the read-only boot path.
 * They return -1 to signal "not implemented" without crashing. */

ssize_t fat32_write(int fd, const void *buf, size_t count) {
    (void)fd; (void)buf; (void)count;
    return -1;
}

off_t fat32_seek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence;
    return -1;
}

int fat32_unlink(const char *path) {
    (void)path;
    return -1;
}

/* =========================================================================
 * Directory operations
 * ======================================================================= */

/*
 * fat32_mkdir - create a new directory at path.
 * Allocates a cluster, writes . and .. entries, then adds a directory
 * entry in the parent cluster.
 * Returns 0 on success, -1 on failure.
 */
int fat32_mkdir(const char *path) {
    if (!g_fs.mounted) return -1;

    const char *name       = path;
    const char *last_slash = strstr(path, "/");
    if (last_slash) name = last_slash + 1;

    char formatted_name[11];
    if (fat32_format_name(name, formatted_name) != 0) return -1;

    uint32_t parent_cluster;
    if (find_entry(path, &parent_cluster) != NULL) return -1;  /* already exists */

    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) return -1;

    /* Write . and .. entries into the new cluster */
    memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
    struct fat32_dir_entry *entries = (struct fat32_dir_entry *)cluster_buffer;

    memcpy(entries[0].name, ".          ", 11);
    entries[0].attr               = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);
    entries[0].first_cluster_low  = (uint16_t)( new_cluster        & 0xFFFF);

    memcpy(entries[1].name, "..         ", 11);
    entries[1].attr               = FAT32_ATTR_DIRECTORY;
    entries[1].first_cluster_high = (uint16_t)((parent_cluster >> 16) & 0xFFFF);
    entries[1].first_cluster_low  = (uint16_t)( parent_cluster        & 0xFFFF);

    if (fat32_write_cluster(new_cluster, cluster_buffer) != 0) {
        fat32_free_cluster_chain(new_cluster);
        return -1;
    }

    /* Add the new directory entry in the parent cluster */
    if (fat32_read_cluster(parent_cluster, cluster_buffer) != 0) {
        fat32_free_cluster_chain(new_cluster);
        return -1;
    }

    entries = (struct fat32_dir_entry *)cluster_buffer;
    int entries_per_cluster =
        (int)(g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry));

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            memcpy(entries[i].name, formatted_name, 11);
            entries[i].attr               = FAT32_ATTR_DIRECTORY;
            entries[i].first_cluster_high = (uint16_t)((new_cluster >> 16) & 0xFFFF);
            entries[i].first_cluster_low  = (uint16_t)( new_cluster        & 0xFFFF);
            entries[i].file_size          = 0;
            fat32_write_cluster(parent_cluster, cluster_buffer);
            return 0;
        }
    }

    fat32_free_cluster_chain(new_cluster);
    return -1;
}

/*
 * fat32_rmdir - stub; directory removal is not implemented.
 */
int fat32_rmdir(const char *path) {
    (void)path;
    return -1;
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

    struct fat32_dir_entry *entry = find_entry(path, NULL);
    if (!entry) return -1;
    if (!(entry->attr & FAT32_ATTR_DIRECTORY)) return -1;

    g_fs.current_directory =
        ((uint32_t)entry->first_cluster_high << 16) | entry->first_cluster_low;
    return 0;
}

/*
 * fat32_getcwd - stub; not implemented.
 */
char *fat32_getcwd(char *buf, size_t size) {
    (void)buf; (void)size;
    return NULL;
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

        fat32_parse_short_name(e->name, entries[count].name);
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
 * FSInfo helpers
 * ======================================================================= */

/*
 * fat32_update_fsinfo - write the in-memory FSInfo structure back to disk.
 * Clears the fat_cache_dirty flag on success.
 */
void fat32_update_fsinfo(void) {
    if (g_fs.boot.fs_info_sector != 0 &&
        g_fs.boot.fs_info_sector != 0xFFFF) {
        fat32_write_sector(g_fs.boot.fs_info_sector, &g_fs.fsinfo);
        g_fs.fat_cache_dirty = 0;
    }
}

/*
 * fat32_get_free_clusters - return the free cluster count from FSInfo.
 */
uint32_t fat32_get_free_clusters(void) {
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
    print_dec(g_fs.fsinfo.free_clusters);
    vga_writestring("\n");

    vga_writestring("Cluster Size:   ");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring(" bytes\n");

    vga_writestring("Total Size:     ");
    print_dec((uint64_t)g_fs.total_clusters *
              g_fs.bytes_per_cluster / (1024 * 1024));
    vga_writestring(" MB\n");

    vga_writestring("Free Space:     ");
    print_dec((uint64_t)g_fs.fsinfo.free_clusters *
              g_fs.bytes_per_cluster / (1024 * 1024));
    vga_writestring(" MB\n");
}