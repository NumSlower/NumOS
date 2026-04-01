/*
 * install.c - Native NumOS disk writer for simple install flows.
 *
 * This tool mirrors the host-side create_disk.py logic inside user space so
 * the OS can lay down its own FAT32 payload when running from a live image.
 */

#include "libc.h"
#include "syscalls.h"

/* Fixed image layout shared with the host-side disk builder. */

#define BYTES_PER_SECTOR    512u
#define SECTORS_PER_CLUSTER 8u
#define RESERVED_SECTORS    32u
#define NUM_FATS            2u
#define FAT_SIZE_SECTORS    160u
#define PARTITION_START_LBA 2048u
#define FS_TOTAL_SECTORS    61440u
#define PARTITION_TYPE_FAT32_LBA 0x0Cu
#define BYTES_PER_CLUSTER   (BYTES_PER_SECTOR * SECTORS_PER_CLUSTER)
#define DATA_START_SECTOR   (RESERVED_SECTORS + (NUM_FATS * FAT_SIZE_SECTORS))
#define TOTAL_CLUSTERS      ((FS_TOTAL_SECTORS - DATA_START_SECTOR) / SECTORS_PER_CLUSTER)

#define ROOT_CLUSTER    2u
#define INIT_CLUSTER    3u
#define BIN_CLUSTER     4u
#define RUN_CLUSTER     5u
#define HOME_CLUSTER    6u
#define INCLUDE_CLUSTER 7u
#define FIRST_FILE_CLUSTER 8u

#define MAX_STAGE_FILES 96
#define FILE_POOL_BYTES (2u * 1024u * 1024u)

#define FAT32_ATTR_ARCHIVE   0x20u
#define FAT32_EOC            0x0FFFFFFFu

/* Each staged file records the short FAT name and the payload location. */
struct staged_file {
    char name[13];
    char short_name[11];
    uint32_t size;
    uint32_t cluster;
    uint32_t clusters;
    const uint8_t *data;
};

static struct staged_file bin_files[MAX_STAGE_FILES];
static struct staged_file run_files[MAX_STAGE_FILES];
static struct staged_file home_files[MAX_STAGE_FILES];
static struct staged_file include_files[MAX_STAGE_FILES];

static int bin_count = 0;
static int run_count = 0;
static int home_count = 0;
static int include_count = 0;

static uint8_t file_pool[FILE_POOL_BYTES];
static uint32_t file_pool_used = 0;

static uint8_t fat_buffer[FAT_SIZE_SECTORS * BYTES_PER_SECTOR];
static uint8_t cluster_buffer[BYTES_PER_CLUSTER];
static uint8_t zero_buffer[BYTES_PER_CLUSTER];

/* Console helpers keep status output small and dependency free. */

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, strlen(s));
}

static void write_dec(uint64_t value) {
    char buf[32];
    char tmp[32];
    int pos = 0;
    int t = 0;

    if (value == 0) {
        sys_write(FD_STDOUT, "0", 1);
        return;
    }

    while (value > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (value % 10u));
        value /= 10u;
    }
    while (t > 0) {
        buf[pos++] = tmp[--t];
    }
    sys_write(FD_STDOUT, buf, (size_t)pos);
}

static void write_size_pretty(uint64_t bytes) {
    if (bytes >= 1024u * 1024u * 1024u) {
        uint64_t whole = bytes / (1024u * 1024u * 1024u);
        uint64_t frac = ((bytes % (1024u * 1024u * 1024u)) * 10u) /
                        (1024u * 1024u * 1024u);
        write_dec(whole);
        write_str(".");
        write_dec(frac);
        write_str(" GB");
        return;
    }

    write_dec(bytes / (1024u * 1024u));
    write_str(" MB");
}

static void store_u16_le(uint8_t *dst, uint16_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
}

static void store_u32_le(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24) & 0xFFu);
}

static char ascii_upper(char c) {
    if (c >= 'a' && c <= 'z') return (char)(c - 'a' + 'A');
    return c;
}

static int ascii_casecmp(const char *a, const char *b) {
    if (!a || !b) return -1;

    while (*a && *b) {
        char ca = ascii_upper(*a++);
        char cb = ascii_upper(*b++);
        if (ca != cb) return (int)((unsigned char)ca - (unsigned char)cb);
    }

    return (int)((unsigned char)ascii_upper(*a) -
                 (unsigned char)ascii_upper(*b));
}

static uint32_t clusters_for(uint32_t size) {
    if (size == 0) return 0;
    return (size + BYTES_PER_CLUSTER - 1u) / BYTES_PER_CLUSTER;
}

static int append_path(char *out, size_t cap, const char *dir, const char *name) {
    size_t pos = 0;
    if (!out || cap == 0) return -1;
    while (*dir && pos + 1 < cap) out[pos++] = *dir++;
    if (pos + 1 >= cap) return -1;
    if (pos > 0 && out[pos - 1] != '/') out[pos++] = '/';
    while (*name && pos + 1 < cap) out[pos++] = *name++;
    if (*name) return -1;
    out[pos] = '\0';
    return 0;
}

static int fat_format_name(const char *filename, char out[11]) {
    const char *dot = 0;
    int name_len = 0;
    int ext_len = 0;

    memset(out, ' ', 11);
    for (const char *p = filename; *p; p++) {
        if (*p == '.') dot = p;
    }

    if (dot) {
        name_len = (int)(dot - filename);
        ext_len = (int)strlen(dot + 1);
    } else {
        name_len = (int)strlen(filename);
        ext_len = 0;
    }

    if (name_len <= 0 || name_len > 8 || ext_len > 3) return -1;

    for (int i = 0; i < name_len; i++) {
        char c = filename[i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[i] = c;
    }

    for (int i = 0; i < ext_len; i++) {
        char c = dot[1 + i];
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        out[8 + i] = c;
    }

    return 0;
}

static int load_file_data(const char *path, uint32_t size, const uint8_t **out_data) {
    int fd;
    uint32_t total = 0;
    uint8_t *dst;

    if (!out_data) return -1;
    if (file_pool_used + size > FILE_POOL_BYTES) return -1;

    dst = file_pool + file_pool_used;
    fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return -1;

    while (total < size) {
        int64_t got = sys_read(fd, dst + total, size - total);
        if (got <= 0) {
            sys_close(fd);
            return -1;
        }
        total += (uint32_t)got;
    }

    sys_close(fd);
    *out_data = dst;
    file_pool_used += size;
    return 0;
}

static int stage_named_file(const char *dir, const char *name, uint32_t size,
                            struct staged_file *files, int *count) {
    char path[128];
    char short_name[11];
    struct staged_file *f;

    if (!files || !count || *count >= MAX_STAGE_FILES) return -1;
    if (fat_format_name(name, short_name) != 0) return 0;
    if (append_path(path, sizeof(path), dir, name) != 0) return -1;

    f = &files[*count];
    memset(f, 0, sizeof(*f));
    strncpy(f->name, name, sizeof(f->name) - 1);
    memcpy(f->short_name, short_name, 11);
    f->size = size;
    f->clusters = clusters_for(size);

    if (load_file_data(path, size, &f->data) != 0) return -1;

    (*count)++;
    return 0;
}

static int stage_directory(const char *dir, struct staged_file *files, int *count) {
    struct fat32_dirent entries[64];
    int64_t n = sys_listdir(dir, entries, 64);
    if (n < 0) return -1;

    for (int i = 0; i < n; i++) {
        if (entries[i].attr & FAT32_ATTR_DIRECTORY) continue;
        if (stage_named_file(dir, entries[i].name, entries[i].size, files, count) != 0) {
            return -1;
        }
    }
    return 0;
}

static int find_staged_file(const struct staged_file *files, int count, const char *name) {
    for (int i = 0; i < count; i++) {
        if (ascii_casecmp(files[i].name, name) == 0) return i;
    }
    return -1;
}

static uint32_t sectors_for_bytes(uint32_t size) {
    if (size == 0) return 0;
    return (size + BYTES_PER_SECTOR - 1u) / BYTES_PER_SECTOR;
}

static void assign_clusters(struct staged_file *files, int count, uint32_t *next_cluster) {
    for (int i = 0; i < count; i++) {
        if (files[i].clusters == 0) continue;
        files[i].cluster = *next_cluster;
        *next_cluster += files[i].clusters;
    }
}

static void set_fat_entry(uint32_t cluster, uint32_t value) {
    store_u32_le(&fat_buffer[cluster * 4u], value);
}

static void build_fat(void) {
    memset(fat_buffer, 0, sizeof(fat_buffer));

    set_fat_entry(0, 0x0FFFFFF8u);
    set_fat_entry(1, 0x0FFFFFFFu);
    set_fat_entry(ROOT_CLUSTER, FAT32_EOC);
    set_fat_entry(INIT_CLUSTER, FAT32_EOC);
    set_fat_entry(BIN_CLUSTER, FAT32_EOC);
    set_fat_entry(RUN_CLUSTER, FAT32_EOC);
    set_fat_entry(HOME_CLUSTER, FAT32_EOC);
    set_fat_entry(INCLUDE_CLUSTER, FAT32_EOC);

    for (int group = 0; group < 4; group++) {
        struct staged_file *files = 0;
        int count = 0;

        if (group == 0) { files = include_files; count = include_count; }
        if (group == 1) { files = bin_files; count = bin_count; }
        if (group == 2) { files = run_files; count = run_count; }
        if (group == 3) { files = home_files; count = home_count; }

        for (int i = 0; i < count; i++) {
            if (files[i].clusters == 0) continue;
            for (uint32_t c = 0; c < files[i].clusters; c++) {
                uint32_t cluster = files[i].cluster + c;
                uint32_t value = (c + 1u == files[i].clusters) ? FAT32_EOC : (cluster + 1u);
                set_fat_entry(cluster, value);
            }
        }
    }
}

static void create_directory_entry(uint8_t *entry, const char short_name[11],
                                   uint8_t attr, uint32_t cluster, uint32_t size) {
    memset(entry, 0, 32);
    memcpy(entry + 0, short_name, 11);
    entry[11] = attr;
    store_u16_le(entry + 20, (uint16_t)((cluster >> 16) & 0xFFFFu));
    store_u16_le(entry + 26, (uint16_t)(cluster & 0xFFFFu));
    store_u32_le(entry + 28, size);
}

static void build_root_directory(void) {
    static const char init_name[11] = { 'I','N','I','T',' ',' ',' ',' ',' ',' ',' ' };
    static const char bin_name[11] = { 'B','I','N',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char run_name[11] = { 'R','U','N',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char home_name[11] = { 'H','O','M','E',' ',' ',' ',' ',' ',' ',' ' };
    static const char incl_name[11] = { 'I','N','C','L','U','D','E',' ',' ',' ',' ' };

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    create_directory_entry(cluster_buffer + 0, init_name, FAT32_ATTR_DIRECTORY, INIT_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 32, bin_name, FAT32_ATTR_DIRECTORY, BIN_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 64, run_name, FAT32_ATTR_DIRECTORY, RUN_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 96, home_name, FAT32_ATTR_DIRECTORY, HOME_CLUSTER, 0);
    create_directory_entry(cluster_buffer + 128, incl_name, FAT32_ATTR_DIRECTORY, INCLUDE_CLUSTER, 0);
}

static int build_child_directory(uint32_t self_cluster, uint32_t parent_cluster,
                                 struct staged_file *files, int count) {
    static const char dot_name[11] = { '.',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ' };
    static const char dotdot_name[11] = { '.','.',' ',' ',' ',' ',' ',' ',' ',' ',' ' };

    if (count > ((int)BYTES_PER_CLUSTER / 32) - 2) return -1;

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    create_directory_entry(cluster_buffer + 0, dot_name, FAT32_ATTR_DIRECTORY, self_cluster, 0);
    create_directory_entry(cluster_buffer + 32, dotdot_name, FAT32_ATTR_DIRECTORY, parent_cluster, 0);

    for (int i = 0; i < count; i++) {
        create_directory_entry(cluster_buffer + ((i + 2) * 32),
                               files[i].short_name,
                               FAT32_ATTR_ARCHIVE,
                               files[i].cluster,
                               files[i].size);
    }
    return 0;
}

static void create_boot_sector(uint8_t *boot) {
    memset(boot, 0, BYTES_PER_SECTOR);
    boot[0] = 0xEBu;
    boot[1] = 0x58u;
    boot[2] = 0x90u;
    memcpy(boot + 3, "NUMOS1.0", 8);
    store_u16_le(boot + 11, BYTES_PER_SECTOR);
    boot[13] = SECTORS_PER_CLUSTER;
    store_u16_le(boot + 14, RESERVED_SECTORS);
    boot[16] = NUM_FATS;
    store_u16_le(boot + 17, 0);
    store_u16_le(boot + 19, 0);
    boot[21] = 0xF8u;
    store_u16_le(boot + 22, 0);
    store_u16_le(boot + 24, 63);
    store_u16_le(boot + 26, 16);
    store_u32_le(boot + 28, PARTITION_START_LBA);
    store_u32_le(boot + 32, FS_TOTAL_SECTORS);
    store_u32_le(boot + 36, FAT_SIZE_SECTORS);
    store_u16_le(boot + 40, 0);
    store_u16_le(boot + 42, 0);
    store_u32_le(boot + 44, ROOT_CLUSTER);
    store_u16_le(boot + 48, 1);
    store_u16_le(boot + 50, 6);
    boot[64] = 0x80u;
    boot[66] = 0x29u;
    store_u32_le(boot + 67, 0x12345678u);
    memcpy(boot + 71, "NUMOS DISK ", 11);
    memcpy(boot + 82, "FAT32   ", 8);
    boot[510] = 0x55u;
    boot[511] = 0xAAu;
}

static void create_fsinfo(uint8_t *fsinfo, uint32_t free_clusters, uint32_t next_free_cluster) {
    memset(fsinfo, 0, BYTES_PER_SECTOR);
    store_u32_le(fsinfo + 0, 0x41615252u);
    store_u32_le(fsinfo + 484, 0x61417272u);
    store_u32_le(fsinfo + 488, free_clusters);
    store_u32_le(fsinfo + 492, next_free_cluster);
    store_u32_le(fsinfo + 508, 0xAA550000u);
}

static int disk_write_repeat(uint64_t lba, const uint8_t *buf, uint32_t total_sectors) {
    uint64_t current = lba;
    uint32_t remaining = total_sectors;

    while (remaining > 0) {
        uint32_t chunk = remaining > SECTORS_PER_CLUSTER ? SECTORS_PER_CLUSTER : remaining;
        if (sys_disk_write(current, buf, chunk) < 0) return -1;
        current += chunk;
        remaining -= chunk;
    }
    return 0;
}

static int write_cluster(uint64_t partition_lba, uint32_t cluster, const uint8_t *data) {
    uint64_t lba = partition_lba + DATA_START_SECTOR +
                   ((uint64_t)(cluster - 2u) * SECTORS_PER_CLUSTER);
    return sys_disk_write(lba, data, SECTORS_PER_CLUSTER) < 0 ? -1 : 0;
}

static int write_file_clusters(uint64_t partition_lba, const struct staged_file *file) {
    uint32_t written = 0;

    if (file->clusters == 0) return 0;

    for (uint32_t c = 0; c < file->clusters; c++) {
        uint32_t copy = file->size - written;
        if (copy > BYTES_PER_CLUSTER) copy = BYTES_PER_CLUSTER;
        memset(cluster_buffer, 0, sizeof(cluster_buffer));
        if (copy > 0) memcpy(cluster_buffer, file->data + written, copy);
        if (write_cluster(partition_lba, file->cluster + c, cluster_buffer) != 0) return -1;
        written += copy;
    }
    return 0;
}

static int write_file_sectors(uint64_t lba, const struct staged_file *file) {
    uint32_t written = 0;
    uint64_t current_lba = lba;

    if (!file) return -1;
    if (file->size == 0) return 0;

    while (written < file->size) {
        uint32_t copy = file->size - written;
        uint32_t sectors;
        uint32_t bytes;

        if (copy > sizeof(cluster_buffer)) copy = sizeof(cluster_buffer);
        sectors = sectors_for_bytes(copy);
        bytes = sectors * BYTES_PER_SECTOR;

        memset(cluster_buffer, 0, bytes);
        memcpy(cluster_buffer, file->data + written, copy);
        if (sys_disk_write(current_lba, cluster_buffer, sectors) < 0) return -1;

        current_lba += sectors;
        written += copy;
    }

    return 0;
}

static int write_all_files(uint64_t partition_lba,
                           struct staged_file *files, int count) {
    for (int i = 0; i < count; i++) {
        if (write_file_clusters(partition_lba, &files[i]) != 0) return -1;
    }
    return 0;
}

static void create_mbr(uint8_t *sector, const struct staged_file *grub_boot) {
    memset(sector, 0, BYTES_PER_SECTOR);
    if (grub_boot && grub_boot->size >= BYTES_PER_SECTOR) {
        memcpy(sector, grub_boot->data, 446);
    }
    sector[446 + 0] = 0x00u;
    sector[446 + 1] = 0xFEu;
    sector[446 + 2] = 0xFFu;
    sector[446 + 3] = 0xFFu;
    sector[446 + 4] = PARTITION_TYPE_FAT32_LBA;
    sector[446 + 5] = 0xFEu;
    sector[446 + 6] = 0xFFu;
    sector[446 + 7] = 0xFFu;
    store_u32_le(sector + 446 + 8, PARTITION_START_LBA);
    store_u32_le(sector + 446 + 12, FS_TOTAL_SECTORS);
    sector[510] = 0x55u;
    sector[511] = 0xAAu;
}

static int install_to_primary_disk(void) {
    struct numos_disk_info info;
    uint8_t sector[BYTES_PER_SECTOR];
    uint32_t next_cluster = FIRST_FILE_CLUSTER;
    uint64_t partition_lba = PARTITION_START_LBA;
    struct fat32_dirent include_entries[16];
    int64_t include_dir_count = 0;
    int boot_idx = -1;
    int core_idx = -1;
    int kern_idx = -1;
    uint32_t grub_core_sectors = 0;

    if (sys_disk_info(&info) < 0 || !info.present) {
        write_str("install: no primary ATA disk\n");
        return 1;
    }

    if (info.sector_count < (uint64_t)PARTITION_START_LBA + FS_TOTAL_SECTORS) {
        write_str("install: disk is too small, need at least ");
        write_size_pretty(((uint64_t)PARTITION_START_LBA + FS_TOTAL_SECTORS) *
                          BYTES_PER_SECTOR);
        write_str("\n");
        return 1;
    }

    write_str("install: target disk ");
    write_size_pretty(info.sector_count * (uint64_t)info.sector_size);
    write_str("\n");

    include_dir_count = sys_listdir("/include", include_entries, 16);
    if (include_dir_count < 0) {
        write_str("install: failed to scan /include\n");
        return 1;
    }
    for (int i = 0; i < include_dir_count; i++) {
        if (ascii_casecmp(include_entries[i].name, "SYSCALLS.H") != 0) continue;
        if (stage_named_file("/include", include_entries[i].name, include_entries[i].size,
                             include_files, &include_count) != 0) {
            write_str("install: failed to stage /include/SYSCALLS.H\n");
            return 1;
        }
        break;
    }

    if (include_count == 0) {
        write_str("install: missing /include/SYSCALLS.H\n");
        return 1;
    }

    if (stage_directory("/bin", bin_files, &bin_count) != 0 ||
        stage_directory("/run", run_files, &run_count) != 0 ||
        stage_directory("/home", home_files, &home_count) != 0) {
        write_str("install: failed to stage files from the current system\n");
        return 1;
    }

    if (find_staged_file(bin_files, bin_count, "SHELL.ELF") < 0) {
        write_str("install: missing /bin/SHELL.ELF\n");
        return 1;
    }

    boot_idx = find_staged_file(run_files, run_count, "GRUBBOOT.BIN");
    if (boot_idx < 0) {
        write_str("install: missing /run/GRUBBOOT.BIN\n");
        return 1;
    }

    core_idx = find_staged_file(run_files, run_count, "GRUBCORE.BIN");
    if (core_idx < 0) {
        write_str("install: missing /run/GRUBCORE.BIN\n");
        return 1;
    }

    kern_idx = find_staged_file(run_files, run_count, "KERN.BIN");
    if (kern_idx < 0) {
        write_str("install: missing /run/KERN.BIN\n");
        return 1;
    }

    grub_core_sectors = sectors_for_bytes(run_files[core_idx].size);
    if (grub_core_sectors >= PARTITION_START_LBA) {
        write_str("install: GRUB core image does not fit before partition 1\n");
        return 1;
    }

    assign_clusters(include_files, include_count, &next_cluster);
    assign_clusters(bin_files, bin_count, &next_cluster);
    assign_clusters(run_files, run_count, &next_cluster);
    assign_clusters(home_files, home_count, &next_cluster);

    write_str("install: staged files into memory, writing disk\n");

    create_mbr(sector, &run_files[boot_idx]);
    if (sys_disk_write(0, sector, 1) < 0) {
        write_str("install: failed to write MBR\n");
        return 1;
    }

    if (write_file_sectors(1, &run_files[core_idx]) != 0) {
        write_str("install: failed to write embedded GRUB core\n");
        return 1;
    }

    if (PARTITION_START_LBA - 1u > grub_core_sectors &&
        disk_write_repeat(1u + grub_core_sectors, zero_buffer,
                          (PARTITION_START_LBA - 1u) - grub_core_sectors) != 0) {
        write_str("install: failed to clear disk gap\n");
        return 1;
    }

    create_boot_sector(sector);
    if (sys_disk_write(partition_lba + 0, sector, 1) < 0) {
        write_str("install: failed to write boot sector\n");
        return 1;
    }

    create_fsinfo(sector,
                  TOTAL_CLUSTERS - (next_cluster - ROOT_CLUSTER),
                  next_cluster);
    if (sys_disk_write(partition_lba + 1, sector, 1) < 0) {
        write_str("install: failed to write fsinfo\n");
        return 1;
    }

    memset(sector, 0, sizeof(sector));
    for (uint32_t i = 2; i < RESERVED_SECTORS; i++) {
        if (i == 6u) {
            create_boot_sector(sector);
        } else {
            memset(sector, 0, sizeof(sector));
        }
        if (sys_disk_write(partition_lba + i, sector, 1) < 0) {
            write_str("install: failed during reserved sector write\n");
            return 1;
        }
    }

    build_fat();
    if (sys_disk_write(partition_lba + RESERVED_SECTORS, fat_buffer, FAT_SIZE_SECTORS) < 0 ||
        sys_disk_write(partition_lba + RESERVED_SECTORS + FAT_SIZE_SECTORS,
                       fat_buffer, FAT_SIZE_SECTORS) < 0) {
        write_str("install: failed to write FAT tables\n");
        return 1;
    }

    build_root_directory();
    if (write_cluster(partition_lba, ROOT_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write root directory\n");
        return 1;
    }

    memset(cluster_buffer, 0, sizeof(cluster_buffer));
    if (write_cluster(partition_lba, INIT_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /init\n");
        return 1;
    }

    if (build_child_directory(BIN_CLUSTER, ROOT_CLUSTER, bin_files, bin_count) != 0 ||
        write_cluster(partition_lba, BIN_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /bin\n");
        return 1;
    }

    if (build_child_directory(RUN_CLUSTER, ROOT_CLUSTER, run_files, run_count) != 0 ||
        write_cluster(partition_lba, RUN_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /run\n");
        return 1;
    }

    if (build_child_directory(HOME_CLUSTER, ROOT_CLUSTER, home_files, home_count) != 0 ||
        write_cluster(partition_lba, HOME_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /home\n");
        return 1;
    }

    if (build_child_directory(INCLUDE_CLUSTER, ROOT_CLUSTER, include_files, include_count) != 0 ||
        write_cluster(partition_lba, INCLUDE_CLUSTER, cluster_buffer) != 0) {
        write_str("install: failed to write /include\n");
        return 1;
    }

    if (write_all_files(partition_lba, include_files, include_count) != 0 ||
        write_all_files(partition_lba, bin_files, bin_count) != 0 ||
        write_all_files(partition_lba, run_files, run_count) != 0 ||
        write_all_files(partition_lba, home_files, home_count) != 0) {
        write_str("install: failed to write file data\n");
        return 1;
    }

    if (next_cluster > ROOT_CLUSTER + ((FS_TOTAL_SECTORS - DATA_START_SECTOR) / SECTORS_PER_CLUSTER)) {
        write_str("install: filesystem layout overflow\n");
        return 1;
    }

    {
        uint64_t data_end_lba = partition_lba + DATA_START_SECTOR +
                                ((uint64_t)(next_cluster - 2u) * SECTORS_PER_CLUSTER);
        uint64_t partition_end_lba = partition_lba + FS_TOTAL_SECTORS;
        while (data_end_lba < partition_end_lba) {
            uint32_t chunk = (partition_end_lba - data_end_lba > SECTORS_PER_CLUSTER)
                           ? SECTORS_PER_CLUSTER
                           : (uint32_t)(partition_end_lba - data_end_lba);
            if (sys_disk_write(data_end_lba, zero_buffer, chunk) < 0) {
                write_str("install: failed to clear trailing sectors\n");
                return 1;
            }
            data_end_lba += chunk;
        }
    }

    write_str("install: done\n");
    write_str("install: reboot from the hard disk\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || (strcmp(argv[1], "ata") != 0 && strcmp(argv[1], "disk") != 0)) {
        write_str("usage: install ata\n");
        write_str("writes a bootable 30 MB NumOS system partition to the primary ATA disk\n");
        return 1;
    }

    return install_to_primary_disk();
}
