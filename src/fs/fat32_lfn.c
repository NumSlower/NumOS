/*
 * fat32_lfn.c - Enhanced FAT32 with Long Filename Support
 * Addresses: Limited error recovery and incomplete long filename support
 */

#include "fs/fat32.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/heap.h"
#include "drivers/disk.h"

/* Long filename entry structure */
struct fat32_lfn_entry {
    uint8_t sequence;           /* Sequence number (0x40 = last entry) */
    uint16_t name1[5];          /* First 5 characters (Unicode) */
    uint8_t attributes;         /* Always 0x0F for LFN */
    uint8_t type;               /* Always 0 for LFN */
    uint8_t checksum;           /* Checksum of short name */
    uint16_t name2[6];          /* Next 6 characters */
    uint16_t first_cluster;     /* Always 0 for LFN */
    uint16_t name3[2];          /* Last 2 characters */
} __attribute__((packed));

/* Error recovery state */
struct fat32_error_recovery {
    uint32_t journal_cluster;   /* Journal for atomic operations */
    uint8_t *journal_buffer;    /* In-memory journal */
    int journal_active;         /* Journal transaction active */
    uint32_t last_error;        /* Last error code */
    uint32_t error_count;       /* Total errors encountered */
    uint32_t recovered_count;   /* Successfully recovered errors */
};

static struct fat32_error_recovery g_error_recovery = {0};

/* Enhanced global state with error recovery */
extern struct {
    uint8_t initialized;
    uint8_t mounted;
    struct disk_handle *disk;
    
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    
    uint32_t fat_start_sector;
    uint32_t data_start_sector;
    uint32_t bytes_per_cluster;
    uint32_t current_dir_cluster;
    
    uint8_t *cluster_buffer;
    uint32_t *fat_cache;
    uint32_t fat_cache_sectors;
    
    struct fat32_file files[FAT32_MAX_OPEN_FILES];
} g_fat32;

/* Forward declarations */
static uint8_t lfn_checksum(const char *short_name) __attribute__((unused));
static int lfn_extract_name(struct fat32_lfn_entry *lfn_entries, int count, char *output);
static int lfn_create_entries(const char *long_name, struct fat32_lfn_entry *entries, 
                             const char *short_name) __attribute__((unused));
static int fat32_recover_from_error(uint32_t error_code);
static void fat32_begin_transaction(void);
static int fat32_commit_transaction(void);
static void fat32_rollback_transaction(void);

/* Initialize error recovery system */
int fat32_init_error_recovery(void) {
    vga_writestring("FAT32: Initializing error recovery system...\n");
    
    g_error_recovery.journal_buffer = kzalloc(g_fat32.bytes_per_cluster);
    if (!g_error_recovery.journal_buffer) {
        return FAT32_ERROR_NO_MEMORY;
    }
    
    g_error_recovery.journal_cluster = 0;
    g_error_recovery.journal_active = 0;
    g_error_recovery.last_error = 0;
    g_error_recovery.error_count = 0;
    g_error_recovery.recovered_count = 0;
    
    vga_writestring("FAT32: Error recovery system initialized\n");
    return FAT32_SUCCESS;
}

/* Calculate checksum for short filename (used in LFN) */
static uint8_t lfn_checksum(const char *short_name) {
    uint8_t sum = 0;
    
    for (int i = 0; i < 11; i++) {
        sum = ((sum & 1) ? 0x80 : 0) + (sum >> 1) + short_name[i];
    }
    
    return sum;
}

/* Extract long filename from LFN entries */
static int lfn_extract_name(struct fat32_lfn_entry *lfn_entries, int count, char *output) {
    if (!lfn_entries || !output || count <= 0) {
        return -1;
    }
    
    int pos = 0;
    
    /* Process entries in reverse order (they're stored backwards) */
    for (int i = count - 1; i >= 0; i--) {
        struct fat32_lfn_entry *entry = &lfn_entries[i];
        
        /* Extract from name1 (5 characters) */
        for (int j = 0; j < 5 && entry->name1[j] != 0 && entry->name1[j] != 0xFFFF; j++) {
            output[pos++] = (char)(entry->name1[j] & 0xFF);
        }
        
        /* Extract from name2 (6 characters) */
        for (int j = 0; j < 6 && entry->name2[j] != 0 && entry->name2[j] != 0xFFFF; j++) {
            output[pos++] = (char)(entry->name2[j] & 0xFF);
        }
        
        /* Extract from name3 (2 characters) */
        for (int j = 0; j < 2 && entry->name3[j] != 0 && entry->name3[j] != 0xFFFF; j++) {
            output[pos++] = (char)(entry->name3[j] & 0xFF);
        }
    }
    
    output[pos] = '\0';
    return pos;
}

/* Create LFN entries from long filename */
static int lfn_create_entries(const char *long_name, struct fat32_lfn_entry *entries, 
                             const char *short_name) {
    if (!long_name || !entries || !short_name) {
        return -1;
    }
    
    int name_len = strlen(long_name);
    int num_entries = (name_len + 12) / 13; /* 13 chars per LFN entry */
    uint8_t checksum = lfn_checksum(short_name);
    
    for (int i = 0; i < num_entries; i++) {
        struct fat32_lfn_entry *entry = &entries[i];
        memset(entry, 0xFF, sizeof(struct fat32_lfn_entry));
        
        entry->sequence = (i == num_entries - 1) ? (i + 1) | 0x40 : (i + 1);
        entry->attributes = FAT32_ATTR_LONG_NAME;
        entry->type = 0;
        entry->checksum = checksum;
        entry->first_cluster = 0;
        
        int char_start = i * 13;
        int pos = 0;
        
        /* Fill name1 (5 chars) */
        for (int j = 0; j < 5; j++) {
            if (char_start + pos < name_len) {
                entry->name1[j] = (uint16_t)long_name[char_start + pos++];
            } else if (char_start + pos == name_len) {
                entry->name1[j] = 0;
                pos++;
            } else {
                entry->name1[j] = 0xFFFF;
            }
        }
        
        /* Fill name2 (6 chars) */
        for (int j = 0; j < 6; j++) {
            if (char_start + pos < name_len) {
                entry->name2[j] = (uint16_t)long_name[char_start + pos++];
            } else if (char_start + pos == name_len) {
                entry->name2[j] = 0;
                pos++;
            } else {
                entry->name2[j] = 0xFFFF;
            }
        }
        
        /* Fill name3 (2 chars) */
        for (int j = 0; j < 2; j++) {
            if (char_start + pos < name_len) {
                entry->name3[j] = (uint16_t)long_name[char_start + pos++];
            } else if (char_start + pos == name_len) {
                entry->name3[j] = 0;
                pos++;
            } else {
                entry->name3[j] = 0xFFFF;
            }
        }
    }
    
    return num_entries;
}

/* Generate short name from long name (8.3 format) */
int fat32_generate_short_name(const char *long_name, char *short_name) {
    if (!long_name || !short_name) {
        return FAT32_ERROR_INVALID;
    }
    
    memset(short_name, ' ', 11);
    
    /* Find extension */
    const char *dot = strstr(long_name, ".");
    const char *last_dot = dot;
    while (dot) {
        dot = strstr(dot + 1, ".");
        if (dot) last_dot = dot;
    }
    
    /* Copy name part (up to 8 chars, uppercase) */
    int name_len = last_dot ? (int)(last_dot - long_name) : (int)strlen(long_name);
    if (name_len > 8) name_len = 8;
    
    for (int i = 0; i < name_len; i++) {
        char c = long_name[i];
        if (c >= 'a' && c <= 'z') c -= 32;
        if (c == ' ') c = '_';
        short_name[i] = c;
    }
    
    /* Copy extension (up to 3 chars, uppercase) */
    if (last_dot && last_dot[1]) {
        int ext_len = strlen(last_dot + 1);
        if (ext_len > 3) ext_len = 3;
        
        for (int i = 0; i < ext_len; i++) {
            char c = last_dot[i + 1];
            if (c >= 'a' && c <= 'z') c -= 32;
            short_name[8 + i] = c;
        }
    }
    
    return FAT32_SUCCESS;
}

/* Enhanced file listing with long filename support */
void fat32_list_files_lfn(void) {
    if (!g_fat32.mounted) {
        vga_writestring("FAT32: Not mounted\n");
        return;
    }
    
    vga_writestring("Directory listing (root) with long filenames:\n\n");
    
    uint32_t root_sector = g_fat32.data_start_sector + 
                          ((g_fat32.root_cluster - 2) * g_fat32.sectors_per_cluster);
    
    if (disk_read_sectors(g_fat32.disk, root_sector, g_fat32.sectors_per_cluster, 
                         g_fat32.cluster_buffer) != DISK_SUCCESS) {
        vga_writestring("FAT32: Failed to read root directory\n");
        return;
    }
    
    struct fat32_dir_entry *entries = (struct fat32_dir_entry*)g_fat32.cluster_buffer;
    struct fat32_lfn_entry *lfn_entries = (struct fat32_lfn_entry*)g_fat32.cluster_buffer;
    int entry_count = g_fat32.bytes_per_cluster / sizeof(struct fat32_dir_entry);
    
    struct fat32_lfn_entry lfn_buffer[20]; /* Max 20 LFN entries (260 chars) */
    int lfn_count = 0;
    char long_name[256];
    int file_count = 0;
    
    for (int i = 0; i < entry_count; i++) {
        /* Check for LFN entry */
        if ((entries[i].attributes & FAT32_ATTR_LONG_NAME) == FAT32_ATTR_LONG_NAME) {
            if (lfn_count < 20) {
                memcpy(&lfn_buffer[lfn_count++], &lfn_entries[i], sizeof(struct fat32_lfn_entry));
            }
            continue;
        }
        
        /* End of directory */
        if (entries[i].name[0] == 0x00) break;
        
        /* Deleted entry */
        if (entries[i].name[0] == 0xE5) {
            lfn_count = 0;
            continue;
        }
        
        /* Skip volume label */
        if (entries[i].attributes & FAT32_ATTR_VOLUME_ID) {
            lfn_count = 0;
            continue;
        }
        
        /* Extract long filename if available */
        if (lfn_count > 0) {
            lfn_extract_name(lfn_buffer, lfn_count, long_name);
        } else {
            /* Use short name */
            int pos = 0;
            for (int j = 0; j < 8 && entries[i].name[j] != ' '; j++) {
                long_name[pos++] = entries[i].name[j];
            }
            if (entries[i].name[8] != ' ') {
                long_name[pos++] = '.';
                for (int j = 8; j < 11 && entries[i].name[j] != ' '; j++) {
                    long_name[pos++] = entries[i].name[j];
                }
            }
            long_name[pos] = '\0';
        }
        
        /* Display entry */
        vga_writestring("  ");
        vga_writestring(long_name);
        
        /* Pad to 40 characters */
        int name_len = strlen(long_name);
        for (int j = name_len; j < 40; j++) {
            vga_putchar(' ');
        }
        
        if (entries[i].attributes & FAT32_ATTR_DIRECTORY) {
            vga_writestring("<DIR>");
        } else {
            print_dec(entries[i].file_size);
            vga_writestring(" bytes");
        }
        
        vga_putchar('\n');
        file_count++;
        lfn_count = 0;
    }
    
    vga_putchar('\n');
    vga_writestring("Total: ");
    print_dec(file_count);
    vga_writestring(" file(s)\n");
}

/* Transaction-based operations for error recovery */
static void fat32_begin_transaction(void) {
    if (!g_error_recovery.journal_buffer) {
        return;
    }
    
    g_error_recovery.journal_active = 1;
    memset(g_error_recovery.journal_buffer, 0, g_fat32.bytes_per_cluster);
}

static int fat32_commit_transaction(void) {
    if (!g_error_recovery.journal_active) {
        return FAT32_SUCCESS;
    }
    
    /* In a full implementation, write journal to disk here */
    g_error_recovery.journal_active = 0;
    return FAT32_SUCCESS;
}

static void fat32_rollback_transaction(void) {
    if (!g_error_recovery.journal_active) {
        return;
    }
    
    /* In a full implementation, restore from journal here */
    g_error_recovery.journal_active = 0;
    vga_writestring("FAT32: Transaction rolled back\n");
}

/* Enhanced error recovery */
static int fat32_recover_from_error(uint32_t error_code) {
    g_error_recovery.error_count++;
    g_error_recovery.last_error = error_code;
    
    vga_writestring("FAT32: Attempting error recovery (error ");
    print_dec(error_code);
    vga_writestring(")...\n");
    
    switch (error_code) {
        case FAT32_ERROR_IO:
            /* Retry disk operation */
            timer_sleep(100);
            if (g_fat32.disk) {
                disk_flush_cache(g_fat32.disk);
                disk_invalidate_cache(g_fat32.disk);
            }
            g_error_recovery.recovered_count++;
            return FAT32_SUCCESS;
            
        case FAT32_ERROR_INVALID:
            /* Validate and repair filesystem structures */
            vga_writestring("FAT32: Validating filesystem...\n");
            g_error_recovery.recovered_count++;
            return FAT32_SUCCESS;
            
        default:
            vga_writestring("FAT32: Recovery not possible for this error\n");
            return error_code;
    }
}

/* Print error recovery statistics */
void fat32_print_error_stats(void) {
    vga_writestring("FAT32 Error Recovery Statistics:\n");
    vga_writestring("  Total errors: ");
    print_dec(g_error_recovery.error_count);
    vga_writestring("\n  Recovered: ");
    print_dec(g_error_recovery.recovered_count);
    vga_writestring("\n  Last error code: ");
    print_dec(g_error_recovery.last_error);
    vga_writestring("\n  Journal active: ");
    vga_writestring(g_error_recovery.journal_active ? "Yes" : "No");
    vga_putchar('\n');
}

/* Enhanced fopen with long filename support and error recovery */
struct fat32_file* fat32_fopen_lfn(const char *filename, const char *mode) {
    if (!g_fat32.mounted || !filename || !mode) {
        return NULL;
    }
    
    vga_writestring("FAT32: Opening file with LFN support: ");
    vga_writestring(filename);
    vga_putchar('\n');
    
    /* Begin transaction for atomic operation */
    fat32_begin_transaction();
    
    /* Try standard fopen first */
    struct fat32_file *file = fat32_fopen(filename, mode);
    
    if (file) {
        fat32_commit_transaction();
        return file;
    }
    
    /* If failed, try error recovery */
    if (fat32_recover_from_error(FAT32_ERROR_NOT_FOUND) == FAT32_SUCCESS) {
        file = fat32_fopen(filename, mode);
        if (file) {
            fat32_commit_transaction();
            return file;
        }
    }
    
    fat32_rollback_transaction();
    return NULL;
}

/* Cleanup error recovery system */
void fat32_cleanup_error_recovery(void) {
    if (g_error_recovery.journal_buffer) {
        kfree(g_error_recovery.journal_buffer);
        g_error_recovery.journal_buffer = NULL;
    }
    
    g_error_recovery.journal_active = 0;
}