/*
 * fat32.c - FAT32 Filesystem Driver
 *
 * Fix: Removed debug vga_writestring() calls from fat32_read_cluster().
 *      Those prints fired on every cluster read, flooding the display
 *      with thousands of lines during a typical ELF load and making the
 *      boot appear to hang.
 */

#include "fs/fat32.h"
#include "drivers/ata.h"
#include "drivers/vga.h"
#include "kernel/kernel.h"
#include "cpu/heap.h"

/* Global filesystem state */
static struct fat32_fs g_fs = {0};

/* File descriptor table */
#define MAX_OPEN_FILES 16
static struct fat32_file g_fd_table[MAX_OPEN_FILES] = {0};

/* Working buffers */
static uint8_t sector_buffer[512] __attribute__((aligned(16)));
static uint8_t cluster_buffer[4096] __attribute__((aligned(16)));

/*
 * Read a sector from disk
 */
int fat32_read_sector(uint32_t sector, void *buffer) {
    return ata_read_sectors(&ata_primary_master, sector, 1, buffer);
}

/*
 * Write a sector to disk
 */
int fat32_write_sector(uint32_t sector, const void *buffer) {
    return ata_write_sectors(&ata_primary_master, sector, 1, buffer);
}

/*
 * Read an entire cluster
 * FIX: removed per-cluster debug prints that spammed the VGA console.
 */
int fat32_read_cluster(uint32_t cluster, void *buffer) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) {
        return -1;
    }

    uint32_t first_sector = g_fs.data_start_sector +
                           (cluster - 2) * g_fs.boot.sectors_per_cluster;

    for (uint8_t i = 0; i < g_fs.boot.sectors_per_cluster; i++) {
        if (fat32_read_sector(first_sector + i,
                             (uint8_t*)buffer + (i * 512)) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Write an entire cluster
 */
int fat32_write_cluster(uint32_t cluster, const void *buffer) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) {
        return -1;
    }

    uint32_t first_sector = g_fs.data_start_sector +
                           (cluster - 2) * g_fs.boot.sectors_per_cluster;

    for (uint8_t i = 0; i < g_fs.boot.sectors_per_cluster; i++) {
        if (fat32_write_sector(first_sector + i,
                              (const uint8_t*)buffer + (i * 512)) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Read FAT entry for a cluster
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

    return *(uint32_t*)(sector_buffer + entry_offset) & 0x0FFFFFFF;
}

/*
 * Write FAT entry for a cluster
 */
void fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) {
        return;
    }

    value &= 0x0FFFFFFF;

    uint32_t fat_offset   = cluster * 4;
    uint32_t fat_sector   = g_fs.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;

    if (fat32_read_sector(fat_sector, sector_buffer) != 0) {
        return;
    }

    uint32_t *entry_ptr = (uint32_t*)(sector_buffer + entry_offset);
    *entry_ptr = (*entry_ptr & 0xF0000000) | value;

    fat32_write_sector(fat_sector, sector_buffer);

    if (g_fs.boot.num_fats == 2) {
        uint32_t fat2_sector = fat_sector + g_fs.boot.fat_size_32;
        fat32_write_sector(fat2_sector, sector_buffer);
    }

    g_fs.fat_cache_dirty = 1;
}

/*
 * Get next cluster in chain
 */
uint32_t fat32_next_cluster(uint32_t cluster) {
    uint32_t next = fat32_read_fat_entry(cluster);

    if (next >= FAT32_EOC_MIN && next <= FAT32_EOC_MAX) {
        return 0;
    }

    if (next == FAT32_BAD_CLUSTER || next == FAT32_FREE_CLUSTER) {
        return 0;
    }

    return next;
}

/*
 * Allocate a new cluster
 */
uint32_t fat32_alloc_cluster(void) {
    uint32_t start_cluster = g_fs.fsinfo.next_free_cluster;

    if (start_cluster < 2) {
        start_cluster = 2;
    }

    for (uint32_t cluster = start_cluster; cluster < g_fs.total_clusters + 2; cluster++) {
        if (fat32_read_fat_entry(cluster) == FAT32_FREE_CLUSTER) {
            fat32_write_fat_entry(cluster, FAT32_EOC_MAX);
            g_fs.fsinfo.next_free_cluster = cluster + 1;
            if (g_fs.fsinfo.free_clusters > 0) {
                g_fs.fsinfo.free_clusters--;
            }
            memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
            fat32_write_cluster(cluster, cluster_buffer);
            return cluster;
        }
    }

    for (uint32_t cluster = 2; cluster < start_cluster; cluster++) {
        if (fat32_read_fat_entry(cluster) == FAT32_FREE_CLUSTER) {
            fat32_write_fat_entry(cluster, FAT32_EOC_MAX);
            g_fs.fsinfo.next_free_cluster = cluster + 1;
            if (g_fs.fsinfo.free_clusters > 0) {
                g_fs.fsinfo.free_clusters--;
            }
            memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
            fat32_write_cluster(cluster, cluster_buffer);
            return cluster;
        }
    }

    return 0;
}

/*
 * Free cluster chain
 */
void fat32_free_cluster_chain(uint32_t start_cluster) {
    uint32_t cluster = start_cluster;

    while (cluster != 0 && cluster >= 2 && cluster < g_fs.total_clusters + 2) {
        uint32_t next = fat32_next_cluster(cluster);
        fat32_write_fat_entry(cluster, FAT32_FREE_CLUSTER);
        g_fs.fsinfo.free_clusters++;
        cluster = next;
    }
}

/*
 * Format short filename (8.3 format)
 */
int fat32_format_name(const char *filename, char *formatted) {
    memset(formatted, ' ', 11);

    int name_len = 0;
    int ext_len  = 0;
    const char *dot = strstr(filename, ".");

    if (dot) {
        name_len = dot - filename;
        ext_len  = strlen(dot + 1);
    } else {
        name_len = strlen(filename);
    }

    if (name_len > 8 || ext_len > 3) {
        return -1;
    }

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
 * Parse short filename to long format
 */
void fat32_parse_short_name(const uint8_t *short_name, char *long_name) {
    int pos = 0;

    for (int i = 0; i < 8 && short_name[i] != ' '; i++) {
        long_name[pos++] = short_name[i];
    }

    if (short_name[8] != ' ') {
        long_name[pos++] = '.';
        for (int i = 8; i < 11 && short_name[i] != ' '; i++) {
            long_name[pos++] = short_name[i];
        }
    }

    long_name[pos] = '\0';
}

/*
 * Initialize FAT32 driver
 */
int fat32_init(void) {
    vga_writestring("FAT32: Initializing filesystem driver...\n");

    memset(&g_fs, 0, sizeof(struct fat32_fs));
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
 * Mount FAT32 filesystem
 */
int fat32_mount(void) {
    vga_writestring("FAT32: Mounting filesystem...\n");

    if (fat32_read_sector(0, &g_fs.boot) != 0) {
        vga_writestring("FAT32: Failed to read boot sector\n");
        return -1;
    }

    if (strncmp((const char*)g_fs.boot.fs_type, "FAT32   ", 8) != 0) {
        vga_writestring("FAT32: Invalid filesystem type\n");
        return -1;
    }

    g_fs.fat_start_sector  = g_fs.boot.reserved_sectors;
    g_fs.data_start_sector = g_fs.fat_start_sector +
                             (g_fs.boot.num_fats * g_fs.boot.fat_size_32);
    g_fs.first_data_sector = g_fs.data_start_sector;

    uint32_t total_sectors  = g_fs.boot.total_sectors_32;
    g_fs.data_sectors       = total_sectors - g_fs.data_start_sector;
    g_fs.total_clusters     = g_fs.data_sectors / g_fs.boot.sectors_per_cluster;
    g_fs.bytes_per_cluster  = g_fs.boot.sectors_per_cluster * g_fs.boot.bytes_per_sector;

    if (g_fs.boot.fs_info_sector != 0 && g_fs.boot.fs_info_sector != 0xFFFF) {
        fat32_read_sector(g_fs.boot.fs_info_sector, &g_fs.fsinfo);
    }

    g_fs.current_directory = g_fs.boot.root_cluster;
    g_fs.mounted = 1;

    vga_writestring("FAT32: Mounted - clusters=");
    print_dec(g_fs.total_clusters);
    vga_writestring(" cluster_size=");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring("B\n");

    return 0;
}

/* forward declarations */
static struct fat32_dir_entry* find_entry_in_cluster(uint32_t cluster,
                                                     char *formatted_name);
static struct fat32_dir_entry* find_entry(const char *path,
                                          uint32_t *parent_cluster);

/*
 * Open a file by path.
 */
int fat32_open(const char *path, int flags)
{
    if (!g_fs.mounted) return -1;

    struct fat32_dir_entry *entry = find_entry(path, NULL);
    if (!entry) return -1;
    if (entry->attr & FAT32_ATTR_DIRECTORY) return -1;

    int fd = -1;
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!g_fd_table[i].in_use) { fd = i; break; }
    }
    if (fd < 0) return -1;

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
 * Read up to count bytes from an open file descriptor.
 */
ssize_t fat32_read(int fd, void *buf, size_t count)
{
    if (!g_fs.mounted) return -1;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !g_fd_table[fd].in_use) return -1;

    uint8_t  *out   = (uint8_t *)buf;
    uint32_t  pos   = g_fd_table[fd].position;
    uint32_t  size  = g_fd_table[fd].size;
    uint32_t  bpc   = g_fs.bytes_per_cluster;
    ssize_t   total = 0;

    if (pos >= size) return 0;
    if ((uint32_t)count > size - pos) count = size - pos;

    /* Walk the cluster chain to the cluster containing `pos` */
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

        uint32_t avail    = bpc - offset_in_cluster;
        size_t   remaining = count - (size_t)total;
        if (avail > remaining) avail = (uint32_t)remaining;

        memcpy(out + total, cluster_buffer + offset_in_cluster, avail);
        total += (ssize_t)avail;

        cluster = fat32_next_cluster(cluster);
        offset_in_cluster = 0;
    }

    g_fd_table[fd].position = pos + (uint32_t)total;
    return total;
}

/*
 * Stat a file.
 */
int fat32_stat(const char *path, struct fat32_dirent *stat)
{
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

/* stubs */
ssize_t fat32_write(int fd, const void *buf, size_t count) {
    (void)fd; (void)buf; (void)count; return -1;
}

off_t fat32_seek(int fd, off_t offset, int whence) {
    (void)fd; (void)offset; (void)whence; return -1;
}

int fat32_unlink(const char *path) { (void)path; return -1; }
int fat32_rmdir(const char *path)  { (void)path; return -1; }
char* fat32_getcwd(char *buf, size_t size) { (void)buf; (void)size; return NULL; }

/*
 * Close an open file descriptor
 */
int fat32_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) return -1;
    if (!g_fd_table[fd].in_use)         return -1;
    memset(&g_fd_table[fd], 0, sizeof(struct fat32_file));
    return 0;
}

/*
 * Unmount filesystem
 */
void fat32_unmount(void) {
    if (!g_fs.mounted) return;

    if (g_fs.fat_cache_dirty) {
        fat32_update_fsinfo();
    }

    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (g_fd_table[i].in_use) fat32_close(i);
    }

    ata_flush_cache(&ata_primary_master);
    g_fs.mounted = 0;
    vga_writestring("FAT32: Unmounted\n");
}

/*
 * Find directory entry by name in a single cluster
 */
static struct fat32_dir_entry* find_entry_in_cluster(uint32_t cluster,
                                                     char *formatted_name) {
    static struct fat32_dir_entry result;

    if (fat32_read_cluster(cluster, cluster_buffer) != 0) {
        return NULL;
    }

    struct fat32_dir_entry *dir_entries = (struct fat32_dir_entry*)cluster_buffer;
    int entries_per_cluster = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);

    for (int i = 0; i < entries_per_cluster; i++) {
        struct fat32_dir_entry *entry = &dir_entries[i];

        if (entry->name[0] == 0x00) break;          /* end of directory */
        if (entry->name[0] == 0xE5) continue;        /* deleted */
        if (entry->attr == FAT32_ATTR_LONG_NAME) continue; /* LFN */

        if (memcmp(entry->name, formatted_name, 11) == 0) {
            memcpy(&result, entry, sizeof(struct fat32_dir_entry));
            return &result;
        }
    }

    return NULL;
}

/*
 * Find directory entry by traversing path
 */
static struct fat32_dir_entry* find_entry(const char *path,
                                          uint32_t *parent_cluster) {
    char     formatted_name[11];
    uint32_t current_cluster = g_fs.current_directory;

    if (path[0] == '/') {
        current_cluster = g_fs.boot.root_cluster;
        path++;
    }

    if (parent_cluster) *parent_cluster = current_cluster;

    if (path[0] == '\0') return NULL;

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

                current_cluster = ((uint32_t)entry->first_cluster_high << 16) |
                                   entry->first_cluster_low;
                comp_len = 0;
            }
            path++;
        } else {
            if (comp_len < 255) component[comp_len++] = *path;
            path++;
        }
    }

    if (comp_len > 0) {
        component[comp_len] = '\0';
        if (fat32_format_name(component, formatted_name) != 0) return NULL;
        return find_entry_in_cluster(current_cluster, formatted_name);
    }

    return NULL;
}

/*
 * Create directory
 */
int fat32_mkdir(const char *path) {
    if (!g_fs.mounted) return -1;

    const char *name = path;
    const char *last_slash = strstr(path, "/");
    if (last_slash) name = last_slash + 1;

    char formatted_name[11];
    if (fat32_format_name(name, formatted_name) != 0) return -1;

    uint32_t parent_cluster;
    if (find_entry(path, &parent_cluster) != NULL) return -1;

    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) return -1;

    memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buffer;

    memcpy(entries[0].name, ".          ", 11);
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_high = (new_cluster >> 16) & 0xFFFF;
    entries[0].first_cluster_low  = new_cluster & 0xFFFF;

    memcpy(entries[1].name, "..         ", 11);
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    entries[1].first_cluster_high = (parent_cluster >> 16) & 0xFFFF;
    entries[1].first_cluster_low  = parent_cluster & 0xFFFF;

    if (fat32_write_cluster(new_cluster, cluster_buffer) != 0) {
        fat32_free_cluster_chain(new_cluster);
        return -1;
    }

    if (fat32_read_cluster(parent_cluster, cluster_buffer) != 0) {
        fat32_free_cluster_chain(new_cluster);
        return -1;
    }

    entries = (struct fat32_dir_entry*)cluster_buffer;
    int entries_per_cluster = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);

    for (int i = 0; i < entries_per_cluster; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            memcpy(entries[i].name, formatted_name, 11);
            entries[i].attr = FAT32_ATTR_DIRECTORY;
            entries[i].first_cluster_high = (new_cluster >> 16) & 0xFFFF;
            entries[i].first_cluster_low  = new_cluster & 0xFFFF;
            entries[i].file_size = 0;
            fat32_write_cluster(parent_cluster, cluster_buffer);
            return 0;
        }
    }

    fat32_free_cluster_chain(new_cluster);
    return -1;
}

/*
 * Change directory
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

    g_fs.current_directory = ((uint32_t)entry->first_cluster_high << 16) |
                              entry->first_cluster_low;
    return 0;
}

/*
 * Read directory contents
 */
int fat32_readdir(struct fat32_dirent *entries, int max_entries) {
    if (!g_fs.mounted) return -1;

    if (fat32_read_cluster(g_fs.current_directory, cluster_buffer) != 0) return -1;

    struct fat32_dir_entry *dir_entries = (struct fat32_dir_entry*)cluster_buffer;
    int entries_per_cluster = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    int count = 0;

    for (int i = 0; i < entries_per_cluster && count < max_entries; i++) {
        struct fat32_dir_entry *entry = &dir_entries[i];

        if (entry->name[0] == 0x00) break;
        if (entry->name[0] == 0xE5) continue;
        if (entry->attr == FAT32_ATTR_LONG_NAME) continue;
        if (entry->name[0] == '.') continue;

        fat32_parse_short_name(entry->name, entries[count].name);
        entries[count].size    = entry->file_size;
        entries[count].attr    = entry->attr;
        entries[count].cluster = ((uint32_t)entry->first_cluster_high << 16) |
                                  entry->first_cluster_low;
        count++;
    }

    return count;
}

/*
 * List directory contents
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
    print_dec(count);
    vga_writestring(" entries\n");

    g_fs.current_directory = saved_dir;
}

/*
 * Update FSInfo sector
 */
void fat32_update_fsinfo(void) {
    if (g_fs.boot.fs_info_sector != 0 && g_fs.boot.fs_info_sector != 0xFFFF) {
        fat32_write_sector(g_fs.boot.fs_info_sector, &g_fs.fsinfo);
        g_fs.fat_cache_dirty = 0;
    }
}

/*
 * Get free cluster count
 */
uint32_t fat32_get_free_clusters(void) {
    return g_fs.fsinfo.free_clusters;
}

/*
 * Print filesystem information
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
        if (g_fs.boot.volume_label[i] != ' ' && g_fs.boot.volume_label[i] != 0)
            vga_putchar(g_fs.boot.volume_label[i]);
    }
    vga_writestring("\n");

    vga_writestring("Total Clusters: ");
    print_dec(g_fs.total_clusters);
    vga_writestring("\n");

    vga_writestring("Free Clusters: ");
    print_dec(g_fs.fsinfo.free_clusters);
    vga_writestring("\n");

    vga_writestring("Cluster Size: ");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring(" bytes\n");

    vga_writestring("Total Size: ");
    print_dec((uint64_t)g_fs.total_clusters * g_fs.bytes_per_cluster / (1024 * 1024));
    vga_writestring(" MB\n");

    vga_writestring("Free Space: ");
    print_dec((uint64_t)g_fs.fsinfo.free_clusters * g_fs.bytes_per_cluster / (1024 * 1024));
    vga_writestring(" MB\n");
}