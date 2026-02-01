/*
 * fat32.c - FAT32 Filesystem Driver
 * Complete implementation of FAT32 for NumOS
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
    
    /* Calculate FAT sector and offset */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_fs.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;
    
    /* Read FAT sector */
    if (fat32_read_sector(fat_sector, sector_buffer) != 0) {
        return FAT32_BAD_CLUSTER;
    }
    
    /* Extract entry (mask off upper 4 bits) */
    uint32_t entry = *(uint32_t*)(sector_buffer + entry_offset) & 0x0FFFFFFF;
    
    return entry;
}

/*
 * Write FAT entry for a cluster
 */
void fat32_write_fat_entry(uint32_t cluster, uint32_t value) {
    if (cluster < 2 || cluster >= g_fs.total_clusters + 2) {
        return;
    }
    
    /* Mask off upper 4 bits */
    value &= 0x0FFFFFFF;
    
    /* Calculate FAT sector and offset */
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = g_fs.fat_start_sector + (fat_offset / 512);
    uint32_t entry_offset = fat_offset % 512;
    
    /* Read FAT sector */
    if (fat32_read_sector(fat_sector, sector_buffer) != 0) {
        return;
    }
    
    /* Preserve upper 4 bits, update entry */
    uint32_t *entry_ptr = (uint32_t*)(sector_buffer + entry_offset);
    *entry_ptr = (*entry_ptr & 0xF0000000) | value;
    
    /* Write back */
    fat32_write_sector(fat_sector, sector_buffer);
    
    /* Write to both FATs if there are 2 */
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
        return 0;  /* End of chain */
    }
    
    if (next == FAT32_BAD_CLUSTER || next == FAT32_FREE_CLUSTER) {
        return 0;  /* Invalid */
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
    
    /* Search for free cluster */
    for (uint32_t cluster = start_cluster; cluster < g_fs.total_clusters + 2; cluster++) {
        uint32_t entry = fat32_read_fat_entry(cluster);
        
        if (entry == FAT32_FREE_CLUSTER) {
            /* Mark as end of chain */
            fat32_write_fat_entry(cluster, FAT32_EOC_MAX);
            
            /* Update FSInfo */
            g_fs.fsinfo.next_free_cluster = cluster + 1;
            if (g_fs.fsinfo.free_clusters > 0) {
                g_fs.fsinfo.free_clusters--;
            }
            
            /* Zero the cluster */
            memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
            fat32_write_cluster(cluster, cluster_buffer);
            
            return cluster;
        }
    }
    
    /* Wrap around */
    for (uint32_t cluster = 2; cluster < start_cluster; cluster++) {
        uint32_t entry = fat32_read_fat_entry(cluster);
        
        if (entry == FAT32_FREE_CLUSTER) {
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
    
    return 0;  /* Disk full */
}

/*
 * Free cluster chain
 */
void fat32_free_cluster_chain(uint32_t start_cluster) {
    uint32_t cluster = start_cluster;
    
    while (cluster != 0 && cluster >= 2 && cluster < g_fs.total_clusters + 2) {
        uint32_t next = fat32_next_cluster(cluster);
        fat32_write_fat_entry(cluster, FAT32_FREE_CLUSTER);
        
        /* Update FSInfo */
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
    int ext_len = 0;
    const char *dot = strstr(filename, ".");
    
    if (dot) {
        name_len = dot - filename;
        ext_len = strlen(dot + 1);
    } else {
        name_len = strlen(filename);
    }
    
    /* Check lengths */
    if (name_len > 8 || ext_len > 3) {
        return -1;
    }
    
    /* Copy name */
    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') {
            c -= 32;  /* Convert to uppercase */
        }
        formatted[i] = c;
    }
    
    /* Copy extension */
    if (dot) {
        for (int i = 0; i < ext_len; i++) {
            char c = dot[1 + i];
            if (c >= 'a' && c <= 'z') {
                c -= 32;
            }
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
    
    /* Copy name part (8 chars) */
    for (int i = 0; i < 8 && short_name[i] != ' '; i++) {
        long_name[pos++] = short_name[i];
    }
    
    /* Add dot if there's an extension */
    if (short_name[8] != ' ') {
        long_name[pos++] = '.';
        
        /* Copy extension (3 chars) */
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
    
    /* Clear filesystem state */
    memset(&g_fs, 0, sizeof(struct fat32_fs));
    memset(g_fd_table, 0, sizeof(g_fd_table));
    
    /* Check if disk exists */
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
    
    /* Read boot sector */
    if (fat32_read_sector(0, &g_fs.boot) != 0) {
        vga_writestring("FAT32: Failed to read boot sector\n");
        return -1;
    }
    
    /* Verify FAT32 signature */
    if (strncmp((const char*)g_fs.boot.fs_type, "FAT32   ", 8) != 0) {
        vga_writestring("FAT32: Invalid filesystem type\n");
        return -1;
    }
    
    /* Calculate filesystem parameters */
    g_fs.fat_start_sector = g_fs.boot.reserved_sectors;
    g_fs.data_start_sector = g_fs.fat_start_sector + 
                             (g_fs.boot.num_fats * g_fs.boot.fat_size_32);
    g_fs.first_data_sector = g_fs.data_start_sector;
    
    uint32_t total_sectors = g_fs.boot.total_sectors_32;
    g_fs.data_sectors = total_sectors - g_fs.data_start_sector;
    g_fs.total_clusters = g_fs.data_sectors / g_fs.boot.sectors_per_cluster;
    g_fs.bytes_per_cluster = g_fs.boot.sectors_per_cluster * g_fs.boot.bytes_per_sector;
    
    /* Read FSInfo */
    if (g_fs.boot.fs_info_sector != 0 && g_fs.boot.fs_info_sector != 0xFFFF) {
        if (fat32_read_sector(g_fs.boot.fs_info_sector, &g_fs.fsinfo) != 0) {
            vga_writestring("FAT32: Warning - Failed to read FSInfo\n");
        }
    }
    
    /* Set current directory to root */
    g_fs.current_directory = g_fs.boot.root_cluster;
    g_fs.mounted = 1;
    
    vga_writestring("FAT32: Mounted successfully\n");
    vga_writestring("  Volume: ");
    for (int i = 0; i < 11; i++) {
        if (g_fs.boot.volume_label[i] != ' ' && g_fs.boot.volume_label[i] != 0) {
            vga_putchar(g_fs.boot.volume_label[i]);
        }
    }
    vga_writestring("\n  Clusters: ");
    print_dec(g_fs.total_clusters);
    vga_writestring("\n  Cluster size: ");
    print_dec(g_fs.bytes_per_cluster);
    vga_writestring(" bytes\n");
    
    return 0;
}

/*
 * Close an open file descriptor
 * Returns 0 on success, -1 if fd is invalid or not in use.
 * No disk I/O is performed: any dirty data in the fd's cluster chain
 * is flushed at unmount time via ata_flush_cache.
 */
int fat32_close(int fd) {
    if (fd < 0 || fd >= MAX_OPEN_FILES) {
        return -1;  /* fd out of valid range */
    }

    if (!g_fd_table[fd].in_use) {
        return -1;  /* fd slot not open */
    }

    /* Zero the entire slot — clears in_use, name, position, flags */
    memset(&g_fd_table[fd], 0, sizeof(struct fat32_file));
    return 0;
}

/*
 * Unmount filesystem
 */
void fat32_unmount(void) {
    if (!g_fs.mounted) {
        return;
    }
    
    /* Update FSInfo if dirty */
    if (g_fs.fat_cache_dirty) {
        fat32_update_fsinfo();
    }
    
    /* Close all open files */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (g_fd_table[i].in_use) {
            fat32_close(i);
        }
    }
    
    /* Flush disk cache */
    ata_flush_cache(&ata_primary_master);
    
    g_fs.mounted = 0;
    vga_writestring("FAT32: Filesystem unmounted\n");
}

/*
 * Find directory entry by name
 */
static struct fat32_dir_entry* find_entry_in_cluster(uint32_t cluster,
                                                     char *formatted_name) {
    static struct fat32_dir_entry entries[16];  /* 512 bytes / 32 bytes per entry */
    
    /* Read cluster */
    if (fat32_read_cluster(cluster, cluster_buffer) != 0) {
        return NULL;
    }
    
    /* Search entries */
    struct fat32_dir_entry *dir_entries = (struct fat32_dir_entry*)cluster_buffer;
    int entries_per_cluster = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < entries_per_cluster; i++) {
        struct fat32_dir_entry *entry = &dir_entries[i];
        
        /* End of directory */
        if (entry->name[0] == 0x00) {
            break;
        }
        
        /* Deleted entry */
        if (entry->name[0] == 0xE5) {
            continue;
        }
        
        /* Skip LFN entries */
        if (entry->attr == FAT32_ATTR_LONG_NAME) {
            continue;
        }
        
        /* Compare names */
        if (memcmp(entry->name, formatted_name, 11) == 0) {
            memcpy(&entries[0], entry, sizeof(struct fat32_dir_entry));
            return &entries[0];
        }
    }
    
    return NULL;
}

/*
 * Find directory entry by traversing path
 */
static struct fat32_dir_entry* find_entry(const char *path, uint32_t *parent_cluster) {
    char formatted_name[11];
    uint32_t current_cluster = g_fs.current_directory;
    
    /* Handle absolute paths */
    if (path[0] == '/') {
        current_cluster = g_fs.boot.root_cluster;
        path++;
    }
    
    if (parent_cluster) {
        *parent_cluster = current_cluster;
    }
    
    /* Empty path = current directory */
    if (path[0] == '\0') {
        return NULL;
    }
    
    /* Parse path components */
    char component[256];
    int comp_len = 0;
    
    while (*path) {
        if (*path == '/') {
            if (comp_len > 0) {
                component[comp_len] = '\0';
                
                /* Format and search */
                if (fat32_format_name(component, formatted_name) != 0) {
                    return NULL;
                }
                
                struct fat32_dir_entry *entry = find_entry_in_cluster(current_cluster, 
                                                                      formatted_name);
                if (!entry) {
                    return NULL;
                }
                
                if (!(entry->attr & FAT32_ATTR_DIRECTORY)) {
                    return NULL;  /* Not a directory */
                }
                
                if (parent_cluster) {
                    *parent_cluster = current_cluster;
                }
                
                current_cluster = ((uint32_t)entry->first_cluster_high << 16) | 
                                 entry->first_cluster_low;
                comp_len = 0;
            }
            path++;
        } else {
            component[comp_len++] = *path++;
        }
    }
    
    /* Final component */
    if (comp_len > 0) {
        component[comp_len] = '\0';
        
        if (fat32_format_name(component, formatted_name) != 0) {
            return NULL;
        }
        
        return find_entry_in_cluster(current_cluster, formatted_name);
    }
    
    return NULL;
}

/*
 * Create directory
 */
int fat32_mkdir(const char *path) {
    if (!g_fs.mounted) {
        return -1;
    }
    
    /* Extract directory name */
    const char *last_slash = strstr(path, "/");
    const char *name = path;
    
    if (last_slash) {
        name = last_slash + 1;
    }
    
    char formatted_name[11];
    if (fat32_format_name(name, formatted_name) != 0) {
        return -1;
    }
    
    /* Check if already exists */
    uint32_t parent_cluster;
    if (find_entry(path, &parent_cluster) != NULL) {
        return -1;  /* Already exists */
    }
    
    /* Allocate cluster for new directory */
    uint32_t new_cluster = fat32_alloc_cluster();
    if (new_cluster == 0) {
        return -1;  /* Disk full */
    }
    
    /* Create . and .. entries */
    memset(cluster_buffer, 0, g_fs.bytes_per_cluster);
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)cluster_buffer;
    
    /* . entry (current directory) */
    memcpy(entries[0].name, ".          ", 11);
    entries[0].attr = FAT32_ATTR_DIRECTORY;
    entries[0].first_cluster_high = (new_cluster >> 16) & 0xFFFF;
    entries[0].first_cluster_low = new_cluster & 0xFFFF;
    
    /* .. entry (parent directory) */
    memcpy(entries[1].name, "..         ", 11);
    entries[1].attr = FAT32_ATTR_DIRECTORY;
    entries[1].first_cluster_high = (parent_cluster >> 16) & 0xFFFF;
    entries[1].first_cluster_low = parent_cluster & 0xFFFF;
    
    /* Write directory cluster */
    if (fat32_write_cluster(new_cluster, cluster_buffer) != 0) {
        fat32_free_cluster_chain(new_cluster);
        return -1;
    }
    
    /* Add entry to parent directory */
    if (fat32_read_cluster(parent_cluster, cluster_buffer) != 0) {
        fat32_free_cluster_chain(new_cluster);
        return -1;
    }
    
    /* Find free entry */
    entries = (struct fat32_dir_entry*)cluster_buffer;
    int entries_per_cluster = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    for (int i = 0; i < entries_per_cluster; i++) {
        if (entries[i].name[0] == 0x00 || entries[i].name[0] == 0xE5) {
            /* Found free entry */
            memcpy(entries[i].name, formatted_name, 11);
            entries[i].attr = FAT32_ATTR_DIRECTORY;
            entries[i].first_cluster_high = (new_cluster >> 16) & 0xFFFF;
            entries[i].first_cluster_low = new_cluster & 0xFFFF;
            entries[i].file_size = 0;
            
            /* Write back parent directory */
            fat32_write_cluster(parent_cluster, cluster_buffer);
            return 0;
        }
    }
    
    /* Parent directory full - would need to allocate more clusters */
    fat32_free_cluster_chain(new_cluster);
    return -1;
}

/*
 * Change directory
 */
int fat32_chdir(const char *path) {
    if (!g_fs.mounted) {
        return -1;
    }
    
    /* Handle root directory */
    if (strcmp(path, "/") == 0) {
        g_fs.current_directory = g_fs.boot.root_cluster;
        return 0;
    }
    
    /* Find directory */
    struct fat32_dir_entry *entry = find_entry(path, NULL);
    
    if (!entry) {
        return -1;  /* Not found */
    }
    
    if (!(entry->attr & FAT32_ATTR_DIRECTORY)) {
        return -1;  /* Not a directory */
    }
    
    uint32_t cluster = ((uint32_t)entry->first_cluster_high << 16) | 
                       entry->first_cluster_low;
    
    g_fs.current_directory = cluster;
    return 0;
}

/*
 * Read directory contents
 */
int fat32_readdir(struct fat32_dirent *entries, int max_entries) {
    if (!g_fs.mounted) {
        return -1;
    }
    
    /* Read current directory cluster */
    if (fat32_read_cluster(g_fs.current_directory, cluster_buffer) != 0) {
        return -1;
    }
    
    struct fat32_dir_entry *dir_entries = (struct fat32_dir_entry*)cluster_buffer;
    int entries_per_cluster = g_fs.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    int count = 0;
    
    for (int i = 0; i < entries_per_cluster && count < max_entries; i++) {
        struct fat32_dir_entry *entry = &dir_entries[i];
        
        /* End of directory */
        if (entry->name[0] == 0x00) {
            break;
        }
        
        /* Skip deleted and LFN entries */
        if (entry->name[0] == 0xE5 || entry->attr == FAT32_ATTR_LONG_NAME) {
            continue;
        }
        
        /* Skip . and .. */
        if (entry->name[0] == '.') {
            continue;
        }
        
        /* Parse and add entry */
        fat32_parse_short_name(entry->name, entries[count].name);
        entries[count].size = entry->file_size;
        entries[count].attr = entry->attr;
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
    
    /* Change to target directory if specified */
    uint32_t saved_dir = g_fs.current_directory;
    
    if (path && path[0] != '\0') {
        if (fat32_chdir(path) != 0) {
            vga_writestring("Directory not found\n");
            return;
        }
    }
    
    /* Read directory */
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
    
    /* Restore directory */
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
        if (g_fs.boot.volume_label[i] != ' ' && g_fs.boot.volume_label[i] != 0) {
            vga_putchar(g_fs.boot.volume_label[i]);
        }
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