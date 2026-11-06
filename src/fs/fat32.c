/* NumOS — fat32.c — FAT32 filesystem driver implementation
 *
 * Read/write FAT32 over the block layer: BPB parsing and validation,
 * FAT chain access, 8.3 plus LFN directory entries, path walking, file
 * create/read/write/close, directory listing, unlink, and cluster
 * allocation/free. Only 512-byte sectors are supported. All metadata
 * updates end with a device flush so state persists across reboots.
 * Mounts a superfloppy geometry (BPB at LBA 0, no MBR partitions yet).
 */
#include "fs/fat32.h"
#include "libs/io.h"
#include "libs/string.h"
#include "libs/kprintf.h"
#include "cpu/heap.h"

#define FAT32_ATTR_RO 0x01
#define FAT32_ATTR_HIDDEN 0x02
#define FAT32_ATTR_SYSTEM 0x04
#define FAT32_ATTR_VOLUME_ID 0x08
#define FAT32_ATTR_DIR 0x10
#define FAT32_ATTR_ARCHIVE 0x20
#define FAT32_ATTR_LFN 0x0F

#define FAT32_DIRENT_FREE 0xE5
#define FAT32_DIRENT_END 0x00
#define FAT32_NTRES_LOWER_BASE 0x08
#define FAT32_NTRES_LOWER_EXT 0x10

#define FAT32_LFN_SEQ_LAST 0x40

#define FAT32_SEC_SIZE 512
#define FAT32_NAME_MAX 255

struct fat32_bpb {
    uint8_t jmp[3];
    char oem[8];
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t media;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t reserved[12];
    uint8_t drive_number;
    uint8_t boot_sig;
    uint32_t volume_id;
    char volume_label[11];
    char fs_type[8];
} __attribute__((packed));

struct fat32_dirent {
    char name[11];
    uint8_t attr;
    uint8_t ntres;
    uint8_t crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t last_acc_date;
    uint16_t first_cluster_high;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t first_cluster_low;
    uint32_t file_size;
} __attribute__((packed));

struct fat32_lfn_entry {
    uint8_t seq;
    uint16_t name1[5];
    uint8_t attr;
    uint8_t type;
    uint8_t checksum;
    uint16_t name2[6];
    uint16_t first_cluster;
    uint16_t name3[2];
} __attribute__((packed));

struct fat32_fs {
    struct block_device* dev;
    struct fat32_bpb bpb;
    uint32_t fat_start_lba;
    uint32_t data_start_lba;
    uint32_t cluster_count;
    uint32_t bytes_per_cluster;
};

struct fat32_file {
    struct fat32_fs* fs;
    uint32_t first_cluster;
    uint32_t size;
    bool is_dir;
    uint32_t dir_offset;
    uint64_t dir_lba;
    int flags;
    uint64_t pos;
};

static uint64_t fat32_cluster_lba(struct fat32_fs* fs, uint32_t cluster) {
    return (uint64_t)fs->data_start_lba + ((uint64_t)cluster - 2) * fs->bpb.sectors_per_cluster;
}

static int fat32_read_sector(struct fat32_fs* fs, uint64_t lba, uint8_t* buf) {
    return block_read_blocks(fs->dev, lba, 1, buf);
}

static int fat32_write_sector(struct fat32_fs* fs, uint64_t lba, const uint8_t* buf) {
    return block_write_blocks(fs->dev, lba, 1, buf);
}

static uint32_t fat32_chain_next(struct fat32_fs* fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + fat_offset / FAT32_SEC_SIZE;
    uint32_t entry_offset = fat_offset % FAT32_SEC_SIZE;

    uint8_t buf[FAT32_SEC_SIZE];
    if (fat32_read_sector(fs, fat_sector, buf) != BLOCK_OK) return FAT32_BAD_CLUSTER;

    uint32_t value;
    memcpy(&value, buf + entry_offset, 4);
    return value & 0x0FFFFFFFu;
}

static int fat32_fat_set(struct fat32_fs* fs, uint32_t cluster, uint32_t value) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + fat_offset / FAT32_SEC_SIZE;
    uint32_t entry_offset = fat_offset % FAT32_SEC_SIZE;

    uint8_t buf[FAT32_SEC_SIZE];
    if (fat32_read_sector(fs, fat_sector, buf) != BLOCK_OK) return VFS_EIO;
    memcpy(buf + entry_offset, &value, 4);
    if (fat32_write_sector(fs, fat_sector, buf) != BLOCK_OK) return VFS_EIO;

    for (uint8_t i = 1; i < fs->bpb.num_fats; i++) {
        uint32_t mirror = fs->fat_start_lba + i * fs->bpb.fat_size_32 + fat_offset / FAT32_SEC_SIZE;
        if (fat32_write_sector(fs, mirror, buf) != BLOCK_OK) return VFS_EIO;
    }
    return VFS_OK;
}

static int fat32_alloc_cluster(struct fat32_fs* fs, uint32_t* out) {
    uint8_t buf[FAT32_SEC_SIZE];
    for (uint32_t cluster = 2; cluster < fs->cluster_count + 2; cluster++) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs->fat_start_lba + fat_offset / FAT32_SEC_SIZE;
        uint32_t entry_offset = fat_offset % FAT32_SEC_SIZE;

        if (fat32_read_sector(fs, fat_sector, buf) != BLOCK_OK) return VFS_EIO;
        uint32_t value;
        memcpy(&value, buf + entry_offset, 4);
        value &= 0x0FFFFFFFu;
        if (value == FAT32_FREE_CLUSTER) {
            uint32_t eoc = FAT32_EOC_MIN;
            memcpy(buf + entry_offset, &eoc, 4);
            if (fat32_write_sector(fs, fat_sector, buf) != BLOCK_OK) return VFS_EIO;
            for (uint8_t i = 1; i < fs->bpb.num_fats; i++) {
                uint32_t mirror = fs->fat_start_lba + i * fs->bpb.fat_size_32 + fat_offset / FAT32_SEC_SIZE;
                fat32_write_sector(fs, mirror, buf);
            }
            *out = cluster;
            return VFS_OK;
        }
    }
    return VFS_ENOSPC;
}

static int fat32_zero_cluster(struct fat32_fs* fs, uint32_t cluster) {
    uint8_t zero[FAT32_SEC_SIZE];
    memset(zero, 0, FAT32_SEC_SIZE);
    uint64_t lba = fat32_cluster_lba(fs, cluster);
    for (uint32_t s = 0; s < fs->bpb.sectors_per_cluster; s++) {
        if (fat32_write_sector(fs, lba + s, zero) != BLOCK_OK) return VFS_EIO;
    }
    return VFS_OK;
}

static int fat32_free_chain(struct fat32_fs* fs, uint32_t start) {
    uint32_t cluster = start;
    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint32_t next = fat32_chain_next(fs, cluster);
        if (fat32_fat_set(fs, cluster, FAT32_FREE_CLUSTER) != VFS_OK) return VFS_EIO;
        if (next < 2) break;
        cluster = next;
    }
    return VFS_OK;
}

static bool ci_equal(const char* a, const char* b) {
    while (*a && *b) {
        char ca = *a, cb = *b;
        if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 32);
        if (ca != cb) return false;
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static void dirent_display_name(const struct fat32_dirent* de, char* out, uint32_t out_size) {
    char base[9];
    char ext[4];
    memcpy(base, de->name, 8);
    base[8] = '\0';
    memcpy(ext, de->name + 8, 3);
    ext[3] = '\0';
    for (uint8_t i = 0; i < 8 && base[i] != ' '; i++) {
        if (base[i] >= 'A' && base[i] <= 'Z' && (de->ntres & FAT32_NTRES_LOWER_BASE)) {
            base[i] = (char)(base[i] + 32);
        }
    }
    for (uint8_t i = 0; i < 3 && ext[i] != ' '; i++) {
        if (ext[i] >= 'A' && ext[i] <= 'Z' && (de->ntres & FAT32_NTRES_LOWER_EXT)) {
            ext[i] = (char)(ext[i] + 32);
        }
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (base[i] == '\0') { for (uint8_t j = i; j < 8; j++) base[j] = ' '; }
    }
    for (int8_t i = 7; i >= 0 && base[i] == ' '; i--) base[i] = '\0';
    for (int8_t i = 2; i >= 0 && ext[i] == ' '; i--) ext[i] = '\0';
    base[8] = '\0';
    ext[3] = '\0';

    if (ext[0] == '\0') {
        strncpy(out, base, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        strncpy(out, base, out_size - 5);
        out[out_size - 5] = '\0';
        strcat(out, ".");
        size_t blen = strlen(out);
        size_t room = out_size - blen - 1;
        size_t elen = strlen(ext);
        if (elen > room) elen = room;
        memcpy(out + blen, ext, elen);
        out[blen + elen] = '\0';
    }
    out[out_size - 1] = '\0';
}

struct fat32_dir_iter {
    struct fat32_fs* fs;
    uint32_t cluster;
    uint32_t sector_in_cluster;
    uint32_t index_in_sector;
    uint64_t lba;
    struct fat32_dirent entry;
    char long_name[FAT32_NAME_MAX + 1];
    bool has_long_name;
};

static int dir_iter_advance(struct fat32_dir_iter* it) {
    for (;;) {
        if (it->index_in_sector >= FAT32_SEC_SIZE / sizeof(struct fat32_dirent)) {
            it->index_in_sector = 0;
            it->sector_in_cluster++;
            if (it->sector_in_cluster >= it->fs->bpb.sectors_per_cluster) {
                it->sector_in_cluster = 0;
                uint32_t next = fat32_chain_next(it->fs, it->cluster);
                if (next < 2 || next >= FAT32_EOC_MIN) {
                    it->cluster = 0;
                    return VFS_OK;
                }
                it->cluster = next;
            }
            it->lba = fat32_cluster_lba(it->fs, it->cluster) + it->sector_in_cluster;
        }

        uint8_t buf[FAT32_SEC_SIZE];
        if (fat32_read_sector(it->fs, it->lba, buf) != BLOCK_OK) return VFS_EIO;

        struct fat32_dirent* de = (struct fat32_dirent*)(buf + it->index_in_sector * sizeof(*de));
        uint8_t first = (uint8_t)de->name[0];
        it->index_in_sector++;

        if (first == FAT32_DIRENT_END) {
            it->cluster = 0;
            return VFS_OK;
        }
        if (first == FAT32_DIRENT_FREE) {
            it->has_long_name = false;
            it->long_name[0] = '\0';
            continue;
        }
        if (de->attr == FAT32_ATTR_LFN) {
            struct fat32_lfn_entry* lfn = (struct fat32_lfn_entry*)de;
            uint8_t seq = lfn->seq & 0x1F;
            if (seq >= 1 && seq <= 20) {
                char* dst = it->long_name + (uint32_t)(seq - 1) * 13;
                uint16_t chars[13];
                memcpy(chars, lfn->name1, 10);
                memcpy(chars + 5, lfn->name2, 12);
                memcpy(chars + 11, lfn->name3, 4);
                for (uint8_t i = 0; i < 13; i++) {
                    dst[i] = (chars[i] != 0x0000 && chars[i] != 0xFFFF && chars[i] < 128)
                                 ? (char)chars[i] : '\0';
                }
                dst[13] = '\0';
                it->has_long_name = true;
            }
            continue;
        }
        if (de->attr & FAT32_ATTR_VOLUME_ID) continue;

        it->entry = *de;
        if (!it->has_long_name) it->long_name[0] = '\0';
        return VFS_OK;
    }
}

static void dir_iter_start(struct fat32_fs* fs, uint32_t first_cluster, struct fat32_dir_iter* it) {
    it->fs = fs;
    it->cluster = first_cluster;
    it->sector_in_cluster = 0;
    it->index_in_sector = 0;
    it->lba = fat32_cluster_lba(fs, first_cluster);
    it->has_long_name = false;
}

static void dir_iter_name(const struct fat32_dir_iter* it, char* out, uint32_t out_size) {
    if (it->has_long_name && it->long_name[0] != '\0') {
        strncpy(out, it->long_name, out_size - 1);
        out[out_size - 1] = '\0';
    } else {
        dirent_display_name(&it->entry, out, out_size);
    }
}

/* ---- path walking ------------------------------------------------------ */

static bool dirent_is_dir(const struct fat32_dirent* de) {
    return (de->attr & FAT32_ATTR_DIR) != 0;
}

static uint32_t dirent_first_cluster(const struct fat32_dirent* de) {
    return ((uint32_t)de->first_cluster_high << 16) | de->first_cluster_low;
}

/* Finds `component` inside the directory at `dir_cluster`. On success fills
 * `out` with the directory entry plus its on-disk location. */
static int dir_find_entry(struct fat32_fs* fs, uint32_t dir_cluster, const char* component,
                          struct fat32_dirent* out, uint64_t* out_lba, uint32_t* out_offset) {
    struct fat32_dir_iter it;
    char name[FAT32_NAME_MAX + 1];
    dir_iter_start(fs, dir_cluster, &it);

    while (it.cluster != 0) {
        int rc = dir_iter_advance(&it);
        if (rc != VFS_OK) return rc;
        if (it.cluster == 0) break;

        dir_iter_name(&it, name, sizeof(name));
        if (name[0] == '\0') continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
        if (!ci_equal(name, component)) continue;

        *out = it.entry;
        *out_lba = it.lba;
        *out_offset = (it.index_in_sector - 1) * sizeof(struct fat32_dirent);
        return VFS_OK;
    }
    return VFS_ENOENT;
}

/* Resolves `path` (absolute, mount-relative) to a directory entry. The
 * lookup of the final component can be suppressed for create operations. */
static int fat32_walk(struct fat32_fs* fs, const char* path, bool resolve_last,
                      struct fat32_dirent* out, uint64_t* out_lba, uint32_t* out_offset,
                      uint32_t* parent_cluster, char* last_name) {
    if (path[0] == '\0' || path[0] != '/') return VFS_EINVAL;

    uint32_t cur_cluster = fs->bpb.root_cluster;
    char component[FAT32_NAME_MAX + 1];
    uint32_t i = 0;

    for (;;) {
        while (path[i] == '/') i++;
        if (path[i] == '\0') break;

        uint32_t start = i;
        while (path[i] != '\0' && path[i] != '/') i++;
        uint32_t len = i - start;
        if (len == 0 || len > FAT32_NAME_MAX) return VFS_EINVAL;
        memcpy(component, path + start, len);
        component[len] = '\0';

        bool is_last = true;
        {
            uint32_t j = i;
            while (path[j] == '/') j++;
            if (path[j] != '\0') is_last = false;
        }

        if (is_last && !resolve_last) {
            if (last_name) strncpy(last_name, component, FAT32_NAME_MAX);
            if (parent_cluster) *parent_cluster = cur_cluster;
            return VFS_OK;
        }

        struct fat32_dirent de;
        uint64_t lba; uint32_t offset;
        int rc = dir_find_entry(fs, cur_cluster, component, &de, &lba, &offset);
        if (rc != VFS_OK) return rc;

        if (is_last) {
            if (out) *out = de;
            if (out_lba) *out_lba = lba;
            if (out_offset) *out_offset = offset;
            if (parent_cluster) *parent_cluster = cur_cluster;
            if (last_name) strncpy(last_name, component, FAT32_NAME_MAX);
            return VFS_OK;
        }

        if (!dirent_is_dir(&de)) return VFS_EINVAL;
        cur_cluster = dirent_first_cluster(&de);
        if (cur_cluster < 2) return VFS_EIO;
    }

    /* Path itself names the root directory (or ends in slashes). */
    if (out) {
        memset(out, 0, sizeof(*out));
        out->attr = FAT32_ATTR_DIR;
        out->first_cluster_high = (uint16_t)(fs->bpb.root_cluster >> 16);
        out->first_cluster_low = (uint16_t)fs->bpb.root_cluster;
    }
    if (parent_cluster) *parent_cluster = fs->bpb.root_cluster;
    return VFS_OK;
}

/* ---- file operations ---------------------------------------------------- */

static int dir_find_free_slots(struct fat32_fs* fs, uint32_t dir_cluster, uint32_t needed,
                               uint64_t* out_lba, uint32_t* out_offset);
static int dir_append_cluster(struct fat32_fs* fs, uint32_t dir_cluster);
static int fat32_extend_file(struct fat32_file* f, uint64_t new_size);

static int fat32_open(struct vfs_filesystem* vfs_fs, const char* path, int flags, struct vfs_file** out) {
    struct fat32_fs* fs = (struct fat32_fs*)vfs_fs->fs_data;
    struct fat32_dirent de;
    uint64_t lba; uint32_t offset; uint32_t parent_cluster;
    char last_name[FAT32_NAME_MAX + 1];
    memset(last_name, 0, sizeof(last_name));

    bool want_create = (flags & VFS_O_CREAT) != 0;
    bool exists;

    if (!want_create) {
        int wrc = fat32_walk(fs, path, true, &de, &lba, &offset, NULL, last_name);
        if (wrc != VFS_OK) return wrc;
        exists = (last_name[0] != '\0');
        if (!exists) return VFS_EINVAL;
    } else {
        int wrc = fat32_walk(fs, path, false, &de, &lba, &offset, &parent_cluster, last_name);
        if (wrc != VFS_OK) return wrc;
        if (last_name[0] == '\0') return VFS_EINVAL;
        exists = (dir_find_entry(fs, parent_cluster, last_name, &de, &lba, &offset) == VFS_OK);
    }

    struct fat32_file* f = kcalloc(1, sizeof(*f));
    if (!f) return VFS_EIO;
    f->fs = fs;
    f->flags = flags;
    f->pos = 0;

    if (exists) {
        if (want_create && (flags & VFS_O_TRUNC)) {
            uint32_t first = dirent_first_cluster(&de);
            if (first >= 2 && first < FAT32_EOC_MIN) fat32_free_chain(fs, first);
            de.first_cluster_high = 0;
            de.first_cluster_low = 0;
            de.file_size = 0;
            uint8_t buf[FAT32_SEC_SIZE];
            if (fat32_read_sector(fs, lba, buf) != BLOCK_OK) { kfree(f); return VFS_EIO; }
            memcpy(buf + offset, &de, sizeof(de));
            if (fat32_write_sector(fs, lba, buf) != BLOCK_OK) { kfree(f); return VFS_EIO; }
        }
        f->first_cluster = dirent_first_cluster(&de);
        f->size = de.file_size;
        f->is_dir = dirent_is_dir(&de);
        f->dir_lba = lba;
        f->dir_offset = offset;
    } else {
        if (!want_create) { kfree(f); return VFS_ENOENT; }

        char short_name[11];
        memset(short_name, ' ', 11);
        uint32_t n = 0;
        for (; n < 8 && last_name[n] != '\0' && last_name[n] != '.'; n++) {
            char c = last_name[n];
            if (c >= 'a' && c <= 'z') c = (char)(c - 32);
            short_name[n] = c;
        }
        bool base_done = (last_name[n] == '\0' || last_name[n] == '.');
        const char* dot = strchr(last_name, '.');
        if (!base_done || (n == 0) || (dot && dot[1] != '\0' && dot - last_name > 8) ||
            (dot && strchr(dot + 1, '.') != NULL)) {
            kfree(f);
            return VFS_EINVAL;
        }
        if (dot && dot[1] != '\0') {
            for (uint32_t e = 0; e < 3 && dot[1 + e] != '\0'; e++) {
                char c = dot[1 + e];
                if (c >= 'a' && c <= 'z') c = (char)(c - 32);
                short_name[8 + e] = c;
            }
        }

        int rc = fat32_extend_file(f, fs->bytes_per_cluster);
        if (rc != VFS_OK) { kfree(f); return rc; }
        if (fat32_zero_cluster(fs, f->first_cluster) != VFS_OK) { kfree(f); return VFS_EIO; }

        memset(&de, 0, sizeof(de));
        de.attr = FAT32_ATTR_ARCHIVE;
        de.ntres = 0;
        de.crt_time = 0x6000;
        de.crt_date = 0x5B21;
        de.wrt_time = 0x6000;
        de.wrt_date = 0x5B21;
        de.first_cluster_high = (uint16_t)(f->first_cluster >> 16);
        de.first_cluster_low = (uint16_t)f->first_cluster;
        de.file_size = 0;
        memcpy(de.name, short_name, 11);

        uint8_t buf[FAT32_SEC_SIZE];
        uint64_t free_lba; uint32_t free_offset;
        rc = dir_find_free_slots(fs, parent_cluster, 1, &free_lba, &free_offset);
        if (rc == VFS_ENOSPC) {
            rc = dir_append_cluster(fs, parent_cluster);
            if (rc != VFS_OK) { kfree(f); return rc; }
            free_lba = fat32_cluster_lba(fs, parent_cluster);
            free_offset = 0;
        }
        if (rc != VFS_OK) { kfree(f); return rc; }

        if (fat32_read_sector(fs, free_lba, buf) != BLOCK_OK) { kfree(f); return VFS_EIO; }
        memcpy(buf + free_offset, &de, sizeof(de));
        if (fat32_write_sector(fs, free_lba, buf) != BLOCK_OK) { kfree(f); return VFS_EIO; }

        f->size = 0;
        f->is_dir = false;
        f->dir_lba = free_lba;
        f->dir_offset = free_offset;
    }

    static struct vfs_file handles[VFS_MAX_OPEN];
    static uint32_t handle_count = 0;
    if (handle_count >= VFS_MAX_OPEN) { kfree(f); return VFS_EMFILE; }
    struct vfs_file* vf = &handles[handle_count++];
    vf->fs = vfs_fs;
    vf->pos = 0;
    vf->fdata = f;
    *out = vf;
    return VFS_OK;
}

/* ---- directory growth helpers ------------------------------------------- */

static int dir_find_free_slots(struct fat32_fs* fs, uint32_t dir_cluster, uint32_t needed,
                               uint64_t* out_lba, uint32_t* out_offset);
static int dir_append_cluster(struct fat32_fs* fs, uint32_t dir_cluster);
static int fat32_extend_file(struct fat32_file* f, uint64_t new_size);

/* ---- directory growth helpers (implementation) -------------------------- */

/* Scans the directory cluster chain for a run of `needed` consecutive free
 * slots (deleted or never-used). Returns the on-disk position of the run. */
static int dir_find_free_slots(struct fat32_fs* fs, uint32_t dir_cluster, uint32_t needed,
                               uint64_t* out_lba, uint32_t* out_offset) {
    uint32_t cluster = dir_cluster;
    uint32_t run_len = 0;
    uint64_t run_lba = 0; uint32_t run_offset = 0;

    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint64_t lba = fat32_cluster_lba(fs, cluster);
        for (uint32_t s = 0; s < fs->bpb.sectors_per_cluster; s++) {
            uint8_t buf[FAT32_SEC_SIZE];
            if (fat32_read_sector(fs, lba + s, buf) != BLOCK_OK) return VFS_EIO;
            uint32_t per_sector = FAT32_SEC_SIZE / (uint32_t)sizeof(struct fat32_dirent);
            for (uint32_t e = 0; e < per_sector; e++) {
                struct fat32_dirent* de = (struct fat32_dirent*)(buf + e * sizeof(*de));
                uint8_t first = (uint8_t)de->name[0];
                bool free_slot = (first == FAT32_DIRENT_END || first == FAT32_DIRENT_FREE);
                if (free_slot) {
                    if (run_len == 0) {
                        run_lba = lba + s;
                        run_offset = e * (uint32_t)sizeof(*de);
                    }
                    run_len++;
                    if (run_len >= needed) {
                        *out_lba = run_lba;
                        *out_offset = run_offset;
                        return VFS_OK;
                    }
                } else {
                    run_len = 0;
                }
            }
        }
        cluster = fat32_chain_next(fs, cluster);
    }
    return VFS_ENOSPC;
}

/* Appends one zeroed cluster to the end of a directory chain. */
static int dir_append_cluster(struct fat32_fs* fs, uint32_t dir_cluster) {
    uint32_t cluster = dir_cluster;
    while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint32_t next = fat32_chain_next(fs, cluster);
        if (next >= 2 && next < FAT32_EOC_MIN) {
            cluster = next;
            continue;
        }
        uint32_t new_cluster;
        int rc = fat32_alloc_cluster(fs, &new_cluster);
        if (rc != VFS_OK) return rc;
        rc = fat32_zero_cluster(fs, new_cluster);
        if (rc != VFS_OK) return rc;
        rc = fat32_fat_set(fs, cluster, new_cluster);
        if (rc != VFS_OK) return rc;
        return VFS_OK;
    }
    return VFS_EIO;
}

/* Grows a file's cluster chain so it covers `new_size` bytes. Allocates and
 * zero-fills clusters as needed, linking them into the chain. Updates the
 * on-disk directory entry's first cluster when a fresh chain was created. */
static int fat32_extend_file(struct fat32_file* f, uint64_t new_size) {
    struct fat32_fs* fs = f->fs;
    uint32_t needed = (uint32_t)((new_size + fs->bytes_per_cluster - 1) / fs->bytes_per_cluster);
    if (needed == 0) needed = 1;

    uint32_t count = 0;
    uint32_t cluster = f->first_cluster;
    uint32_t last = 0;

    if (f->first_cluster >= 2 && f->first_cluster < FAT32_EOC_MIN) {
        while (cluster >= 2 && cluster < FAT32_EOC_MIN) {
            count++;
            last = cluster;
            cluster = fat32_chain_next(fs, cluster);
            if (count >= 65525) return VFS_EIO;
        }
    }

    while (count < needed) {
        uint32_t new_cluster;
        int rc = fat32_alloc_cluster(fs, &new_cluster);
        if (rc != VFS_OK) return rc;
        rc = fat32_zero_cluster(fs, new_cluster);
        if (rc != VFS_OK) return rc;

        if (last == 0) {
            f->first_cluster = new_cluster;
            if (f->dir_lba != 0) {
                uint8_t buf[FAT32_SEC_SIZE];
                if (fat32_read_sector(fs, f->dir_lba, buf) != BLOCK_OK) return VFS_EIO;
                struct fat32_dirent* de = (struct fat32_dirent*)(buf + f->dir_offset);
                de->first_cluster_high = (uint16_t)(new_cluster >> 16);
                de->first_cluster_low = (uint16_t)new_cluster;
                if (fat32_write_sector(fs, f->dir_lba, buf) != BLOCK_OK) return VFS_EIO;
            }
        } else {
            if (fat32_fat_set(fs, last, new_cluster) != VFS_OK) return VFS_EIO;
        }
        last = new_cluster;
        count++;
    }
    return VFS_OK;
}

/* Updates size and cluster fields of a file's directory entry on disk. */
static int fat32_sync_dirent(struct fat32_file* f) {
    struct fat32_fs* fs = f->fs;
    if (f->dir_lba == 0) return VFS_OK;

    uint8_t buf[FAT32_SEC_SIZE];
    if (fat32_read_sector(fs, f->dir_lba, buf) != BLOCK_OK) return VFS_EIO;
    struct fat32_dirent* de = (struct fat32_dirent*)(buf + f->dir_offset);
    de->first_cluster_high = (uint16_t)(f->first_cluster >> 16);
    de->first_cluster_low = (uint16_t)f->first_cluster;
    de->file_size = (uint32_t)f->size;
    if (fat32_write_sector(fs, f->dir_lba, buf) != BLOCK_OK) return VFS_EIO;
    return VFS_OK;
}

static int fat32_read(struct vfs_file* vf, void* buffer, size_t len, size_t* out_len) {
    struct fat32_file* f = (struct fat32_file*)vf->fdata;
    struct fat32_fs* fs = f->fs;
    uint8_t* dst = (uint8_t*)buffer;

    if (f->is_dir) return VFS_EINVAL;
    if (out_len) *out_len = 0;

    uint64_t remaining = 0;
    if (f->size > vf->pos) remaining = f->size - vf->pos;
    uint64_t to_read = len;
    if (to_read > remaining) to_read = remaining;
    if (to_read == 0) return VFS_OK;

    uint32_t cluster = f->first_cluster;
    uint64_t cluster_index = vf->pos / fs->bytes_per_cluster;
    uint32_t chain_pos = 0;
    while (chain_pos < cluster_index && cluster >= 2 && cluster < FAT32_EOC_MIN) {
        cluster = fat32_chain_next(fs, cluster);
        chain_pos++;
    }
    if (cluster < 2 || cluster >= FAT32_EOC_MIN) return VFS_EIO;

    uint64_t done = 0;
    uint32_t offset_in_cluster = (uint32_t)(vf->pos % fs->bytes_per_cluster);
    uint8_t sec_buf[FAT32_SEC_SIZE];

    while (done < to_read && cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint64_t lba = fat32_cluster_lba(fs, cluster);
        while (offset_in_cluster < fs->bytes_per_cluster && done < to_read) {
            uint32_t sector_in_cluster = offset_in_cluster / FAT32_SEC_SIZE;
            uint32_t offset_in_sector = offset_in_cluster % FAT32_SEC_SIZE;
            uint64_t chunk = FAT32_SEC_SIZE - offset_in_sector;
            if (chunk > to_read - done) chunk = to_read - done;
            if (chunk > fs->bytes_per_cluster - offset_in_cluster) {
                chunk = fs->bytes_per_cluster - offset_in_cluster;
            }

            if (chunk == FAT32_SEC_SIZE && (offset_in_cluster % FAT32_SEC_SIZE) == 0) {
                if (block_read_blocks(fs->dev, lba + sector_in_cluster, 1, dst + done) != BLOCK_OK) {
                    return VFS_EIO;
                }
            } else {
                if (fat32_read_sector(fs, lba + sector_in_cluster, sec_buf) != VFS_OK) return VFS_EIO;
                memcpy(dst + done, sec_buf + offset_in_sector, chunk);
            }
            done += chunk;
            offset_in_cluster += (uint32_t)chunk;
        }
        offset_in_cluster = 0;
        cluster = fat32_chain_next(fs, cluster);
    }

    vf->pos += done;
    if (out_len) *out_len = done;
    return VFS_OK;
}

static int fat32_write(struct vfs_file* vf, const void* buffer, size_t len, size_t* out_len) {
    struct fat32_file* f = (struct fat32_file*)vf->fdata;
    struct fat32_fs* fs = f->fs;
    const uint8_t* src = (const uint8_t*)buffer;

    if (f->is_dir) return VFS_EINVAL;
    if (out_len) *out_len = 0;
    if (len == 0) return VFS_OK;

    uint64_t end = vf->pos + len;
    if (end > f->size) {
        int rc = fat32_extend_file(f, end);
        if (rc != VFS_OK) return rc;
        f->size = (uint32_t)end;
        if (fat32_sync_dirent(f) != VFS_OK) return VFS_EIO;
    }

    uint32_t cluster = f->first_cluster;
    uint64_t cluster_index = vf->pos / fs->bytes_per_cluster;
    uint32_t chain_pos = 0;
    while (chain_pos < cluster_index && cluster >= 2 && cluster < FAT32_EOC_MIN) {
        cluster = fat32_chain_next(fs, cluster);
        chain_pos++;
    }
    if (cluster < 2 || cluster >= FAT32_EOC_MIN) return VFS_EIO;

    uint64_t done = 0;
    uint32_t offset_in_cluster = (uint32_t)(vf->pos % fs->bytes_per_cluster);
    uint8_t sec_buf[FAT32_SEC_SIZE];

    while (done < len && cluster >= 2 && cluster < FAT32_EOC_MIN) {
        uint64_t lba = fat32_cluster_lba(fs, cluster);
        while (offset_in_cluster < fs->bytes_per_cluster && done < len) {
            uint32_t sector_in_cluster = offset_in_cluster / FAT32_SEC_SIZE;
            uint32_t offset_in_sector = offset_in_cluster % FAT32_SEC_SIZE;
            uint64_t chunk = FAT32_SEC_SIZE - offset_in_sector;
            if (chunk > len - done) chunk = len - done;
            if (chunk > fs->bytes_per_cluster - offset_in_cluster) {
                chunk = fs->bytes_per_cluster - offset_in_cluster;
            }

            if (chunk == FAT32_SEC_SIZE && (offset_in_cluster % FAT32_SEC_SIZE) == 0) {
                if (block_write_blocks(fs->dev, lba + sector_in_cluster, 1, src + done) != BLOCK_OK) {
                    return VFS_EIO;
                }
            } else {
                if (fat32_read_sector(fs, lba + sector_in_cluster, sec_buf) != VFS_OK) return VFS_EIO;
                memcpy(sec_buf + offset_in_sector, src + done, chunk);
                if (fat32_write_sector(fs, lba + sector_in_cluster, sec_buf) != BLOCK_OK) {
                    return VFS_EIO;
                }
            }
            done += chunk;
            offset_in_cluster += (uint32_t)chunk;
        }
        offset_in_cluster = 0;
        cluster = fat32_chain_next(fs, cluster);
    }

    vf->pos += done;
    if (out_len) *out_len = done;
    return VFS_OK;
}

static int fat32_close(struct vfs_file* vf) {
    struct fat32_file* f = (struct fat32_file*)vf->fdata;
    block_flush(f->fs->dev);
    kfree(f);
    vf->fdata = NULL;
    return VFS_OK;
}

static int fat32_stat(struct vfs_filesystem* vfs_fs, const char* path, struct vfs_stat* out) {
    struct fat32_fs* fs = (struct fat32_fs*)vfs_fs->fs_data;
    struct fat32_dirent de;
    uint64_t lba; uint32_t offset;
    char last_name[FAT32_NAME_MAX + 1];
    memset(last_name, 0, sizeof(last_name));

    int rc = fat32_walk(fs, path, true, &de, &lba, &offset, NULL, last_name);
    if (rc != VFS_OK) return rc;
    if (last_name[0] == '\0') {
        strncpy(out->name, "/", VFS_NAME_MAX - 1);
        out->is_dir = true;
        out->size = 0;
        return VFS_OK;
    }

    strncpy(out->name, last_name, VFS_NAME_MAX - 1);
    out->name[VFS_NAME_MAX - 1] = '\0';
    out->is_dir = dirent_is_dir(&de);
    out->size = de.file_size;
    return VFS_OK;
}

static int fat32_list(struct vfs_filesystem* vfs_fs, const char* path, vfs_list_cb cb, void* ctx) {
    struct fat32_fs* fs = (struct fat32_fs*)vfs_fs->fs_data;
    uint32_t dir_cluster = fs->bpb.root_cluster;

    if (path[1] != '\0') {
        struct fat32_dirent de;
        uint64_t lba; uint32_t offset;
        char last_name[FAT32_NAME_MAX + 1];
        memset(last_name, 0, sizeof(last_name));
        int rc = fat32_walk(fs, path, true, &de, &lba, &offset, NULL, last_name);
        if (rc != VFS_OK) return rc;
        if (!dirent_is_dir(&de)) return VFS_EINVAL;
        dir_cluster = dirent_first_cluster(&de);
        if (dir_cluster < 2) return VFS_OK;
    }

    struct fat32_dir_iter it;
    char name[FAT32_NAME_MAX + 1];
    dir_iter_start(fs, dir_cluster, &it);

    while (it.cluster != 0) {
        int rc = dir_iter_advance(&it);
        if (rc != VFS_OK) return rc;
        if (it.cluster == 0) break;

        dir_iter_name(&it, name, sizeof(name));
        if (name[0] == '\0') continue;
        if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;

        struct vfs_stat st;
        strncpy(st.name, name, VFS_NAME_MAX - 1);
        st.name[VFS_NAME_MAX - 1] = '\0';
        st.is_dir = dirent_is_dir(&it.entry);
        st.size = it.entry.file_size;
        int crc = cb(&st, ctx);
        if (crc != VFS_OK) return crc;
    }
    return VFS_OK;
}

static int fat32_unlink(struct vfs_filesystem* vfs_fs, const char* path) {
    struct fat32_fs* fs = (struct fat32_fs*)vfs_fs->fs_data;
    struct fat32_dirent de;
    uint64_t lba; uint32_t offset; uint32_t parent_cluster;
    char last_name[FAT32_NAME_MAX + 1];
    memset(last_name, 0, sizeof(last_name));

    int rc = fat32_walk(fs, path, true, &de, &lba, &offset, &parent_cluster, last_name);
    if (rc != VFS_OK) return rc;
    if (last_name[0] == '\0') return VFS_EINVAL;
    if (dirent_is_dir(&de)) return VFS_EINVAL;

    uint32_t first = dirent_first_cluster(&de);
    if (first >= 2 && first < FAT32_EOC_MIN) {
        if (fat32_free_chain(fs, first) != VFS_OK) return VFS_EIO;
    }

    uint8_t buf[FAT32_SEC_SIZE];
    if (fat32_read_sector(fs, lba, buf) != BLOCK_OK) return VFS_EIO;
    struct fat32_dirent* dp = (struct fat32_dirent*)(buf + offset);
    dp->name[0] = (char)FAT32_DIRENT_FREE;
    if (fat32_write_sector(fs, lba, buf) != BLOCK_OK) return VFS_EIO;

    block_flush(fs->dev);
    return VFS_OK;
}

static int fat32_sync(struct vfs_filesystem* vfs_fs) {
    struct fat32_fs* fs = (struct fat32_fs*)vfs_fs->fs_data;
    block_flush(fs->dev);
    return VFS_OK;
}

static const struct vfs_ops fat32_ops = {
    .open = fat32_open,
    .read = fat32_read,
    .write = fat32_write,
    .close = fat32_close,
    .stat = fat32_stat,
    .list = fat32_list,
    .unlink = fat32_unlink,
    .sync = fat32_sync,
};

struct vfs_filesystem* fat32_mount(struct block_device* dev) {
    if (!dev) return NULL;

    struct fat32_fs* fs = kcalloc(1, sizeof(*fs));
    if (!fs) return NULL;
    fs->dev = dev;

    uint8_t boot[FAT32_SEC_SIZE];
    if (fat32_read_sector(fs, 0, boot) != VFS_OK) { kfree(fs); return NULL; }
    memcpy(&fs->bpb, boot, sizeof(fs->bpb));

    struct fat32_bpb* b = &fs->bpb;
    if (b->bytes_per_sector != FAT32_SEC_SIZE) { kfree(fs); return NULL; }
    if (b->sectors_per_cluster == 0 || (b->sectors_per_cluster & (b->sectors_per_cluster - 1)) != 0) {
        kfree(fs);
        return NULL;
    }
    if (b->fat_size_32 == 0 || b->num_fats == 0 || b->root_cluster < 2) { kfree(fs); return NULL; }
    if (boot[510] != 0x55 || boot[511] != 0xAA) { kfree(fs); return NULL; }

    fs->fat_start_lba = b->reserved_sectors;
    fs->data_start_lba = b->reserved_sectors + (uint32_t)b->num_fats * b->fat_size_32;
    uint32_t total_sectors = b->total_sectors_32 ? b->total_sectors_32 : b->total_sectors_16;
    uint32_t data_sectors = total_sectors - fs->data_start_lba;
    fs->cluster_count = data_sectors / b->sectors_per_cluster;
    fs->bytes_per_cluster = (uint32_t)b->sectors_per_cluster * FAT32_SEC_SIZE;

    if (fs->cluster_count < 4085) { kfree(fs); return NULL; }

    struct vfs_filesystem* vfs_fs = kcalloc(1, sizeof(*vfs_fs));
    if (!vfs_fs) { kfree(fs); return NULL; }
    strncpy(vfs_fs->type, "fat32", sizeof(vfs_fs->type) - 1);
    vfs_fs->dev = dev;
    vfs_fs->ops = &fat32_ops;
    vfs_fs->fs_data = fs;
    return vfs_fs;
}
