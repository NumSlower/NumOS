#include "fs/vfs.h"

#include "fs/fat32.h"
#include "cpu/heap.h"
#include "lib/string.h"

struct vfs_mount {
    const char *name;
    char        mount_point[VFS_PATH_MAX];
    struct vfs_ops ops;
    int         active;
};

struct vfs_file {
    struct vfs_mount *mount;
    int               backend_handle;
    int               in_use;
};

static struct vfs_mount mounts[VFS_MAX_MOUNTS];
static struct vfs_file  open_files[VFS_MAX_OPEN_FILES];

static int path_prefix_match(const char *mount_point, const char *path) {
    size_t mount_len;

    if (!mount_point || !path) return 0;
    if (mount_point[0] == '/' && mount_point[1] == '\0') return 1;

    mount_len = strlen(mount_point);
    if (memcmp(path, mount_point, mount_len) != 0) return 0;

    if (path[mount_len] == '\0') return 1;
    return path[mount_len] == '/';
}

static int translate_path(const struct vfs_mount *mount,
                          const char             *path,
                          char                   *out,
                          size_t                  out_cap) {
    size_t prefix_len;
    const char *suffix;
    size_t suffix_len;

    if (!mount || !path || !out || out_cap == 0) return -1;

    if (path[0] != '/') {
        size_t len = strlen(path);
        if (len + 1 > out_cap) return -1;
        memcpy(out, path, len + 1);
        return 0;
    }

    prefix_len = strlen(mount->mount_point);
    suffix = path + prefix_len;

    if (mount->mount_point[0] == '/' && mount->mount_point[1] == '\0') {
        suffix = path;
    } else if (*suffix == '\0') {
        suffix = "/";
    }

    if (*suffix == '\0') suffix = "/";

    suffix_len = strlen(suffix);
    if (suffix_len + 1 > out_cap) return -1;
    memcpy(out, suffix, suffix_len + 1);
    return 0;
}

static struct vfs_mount *find_mount_for_path(const char *path) {
    struct vfs_mount *best = NULL;
    size_t best_len = 0;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) continue;
        if (!path_prefix_match(mounts[i].mount_point, path)) continue;

        size_t mount_len = strlen(mounts[i].mount_point);
        if (!best || mount_len > best_len) {
            best = &mounts[i];
            best_len = mount_len;
        }
    }

    return best;
}

static int alloc_open_slot(void) {
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!open_files[i].in_use) return i;
    }
    return -1;
}

static int fat32_vfs_stat(const char *path, struct vfs_stat *st) {
    struct fat32_dirent dent;

    if (fat32_stat(path, &dent) != 0) return -1;
    if (!st) return 0;

    memset(st, 0, sizeof(*st));
    strncpy(st->name, dent.name, sizeof(st->name) - 1);
    st->size = dent.size;
    st->attr = dent.attr;
    st->type = (dent.attr & FAT32_ATTR_DIRECTORY) ? VFS_NODE_DIRECTORY
                                                  : VFS_NODE_FILE;
    st->fs_data = dent.cluster;
    return 0;
}

static int fat32_vfs_listdir(const char *path,
                             struct vfs_dirent *entries,
                             int max_entries) {
    struct fat32_dirent *tmp;
    uint32_t saved_dir;
    int count;

    if (!entries || max_entries <= 0) return -1;

    tmp = (struct fat32_dirent *)kmalloc(sizeof(*tmp) * (size_t)max_entries);
    if (!tmp) return -1;

    saved_dir = fat32_get_current_directory();
    if (path && path[0] != '\0' &&
        !(path[0] == '/' && path[1] == '\0')) {
        if (fat32_chdir(path) != 0) {
            fat32_set_current_directory(saved_dir);
            kfree(tmp);
            return -1;
        }
    } else if (path && path[0] == '/' && path[1] == '\0') {
        if (fat32_chdir("/") != 0) {
            fat32_set_current_directory(saved_dir);
            kfree(tmp);
            return -1;
        }
    }

    count = fat32_readdir(tmp, max_entries);
    fat32_set_current_directory(saved_dir);
    if (count < 0) {
        kfree(tmp);
        return -1;
    }

    for (int i = 0; i < count; i++) {
        memset(&entries[i], 0, sizeof(entries[i]));
        strncpy(entries[i].name, tmp[i].name, sizeof(entries[i].name) - 1);
        entries[i].size = tmp[i].size;
        entries[i].attr = tmp[i].attr;
        entries[i].type = (tmp[i].attr & FAT32_ATTR_DIRECTORY)
                        ? VFS_NODE_DIRECTORY : VFS_NODE_FILE;
        entries[i].fs_data = tmp[i].cluster;
    }

    kfree(tmp);
    return count;
}

static int register_mount(const char *name,
                          const char *mount_point,
                          const struct vfs_ops *ops) {
    if (!name || !mount_point || !ops) return -1;

    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!mounts[i].active) {
            memset(&mounts[i], 0, sizeof(mounts[i]));
            mounts[i].name = name;
            strncpy(mounts[i].mount_point, mount_point,
                    sizeof(mounts[i].mount_point) - 1);
            mounts[i].ops = *ops;
            mounts[i].active = 1;
            return 0;
        }
    }

    return -1;
}

int vfs_init(void) {
    memset(mounts, 0, sizeof(mounts));
    memset(open_files, 0, sizeof(open_files));
    return 0;
}

int vfs_register_fat32_root(void) {
    const struct vfs_ops fat32_ops = {
        .open = fat32_open,
        .close = fat32_close,
        .read = fat32_read,
        .write = fat32_write,
        .stat = fat32_vfs_stat,
        .listdir = fat32_vfs_listdir,
    };

    return register_mount("fat32", "/", &fat32_ops);
}

int vfs_open(const char *path, int flags) {
    struct vfs_mount *mount;
    char local_path[VFS_PATH_MAX];
    int backend_handle;
    int slot;

    if (!path) return -1;

    mount = find_mount_for_path(path);
    if (!mount || !mount->ops.open) return -1;
    if (translate_path(mount, path, local_path, sizeof(local_path)) != 0) {
        return -1;
    }

    backend_handle = mount->ops.open(local_path, flags);
    if (backend_handle < 0) return -1;

    slot = alloc_open_slot();
    if (slot < 0) {
        mount->ops.close(backend_handle);
        return -1;
    }

    open_files[slot].mount = mount;
    open_files[slot].backend_handle = backend_handle;
    open_files[slot].in_use = 1;
    return slot;
}

int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].in_use) return -1;
    if (!open_files[fd].mount || !open_files[fd].mount->ops.close) return -1;

    int rc = open_files[fd].mount->ops.close(open_files[fd].backend_handle);
    memset(&open_files[fd], 0, sizeof(open_files[fd]));
    return rc;
}

ssize_t vfs_read(int fd, void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].in_use) return -1;
    if (!open_files[fd].mount || !open_files[fd].mount->ops.read) return -1;

    return open_files[fd].mount->ops.read(open_files[fd].backend_handle,
                                          buf,
                                          count);
}

ssize_t vfs_write(int fd, const void *buf, size_t count) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !open_files[fd].in_use) return -1;
    if (!open_files[fd].mount || !open_files[fd].mount->ops.write) return -1;

    return open_files[fd].mount->ops.write(open_files[fd].backend_handle,
                                           buf,
                                           count);
}

int vfs_stat(const char *path, struct vfs_stat *st) {
    struct vfs_mount *mount;
    char local_path[VFS_PATH_MAX];

    if (!path) return -1;

    mount = find_mount_for_path(path);
    if (!mount || !mount->ops.stat) return -1;
    if (translate_path(mount, path, local_path, sizeof(local_path)) != 0) {
        return -1;
    }

    return mount->ops.stat(local_path, st);
}

int vfs_listdir(const char *path, struct vfs_dirent *entries, int max_entries) {
    struct vfs_mount *mount;
    char local_path[VFS_PATH_MAX];

    if (!entries || max_entries <= 0) return -1;

    if (!path || path[0] == '\0') {
        mount = find_mount_for_path("/");
        if (!mount || !mount->ops.listdir) return -1;
        return mount->ops.listdir("", entries, max_entries);
    }

    mount = find_mount_for_path(path);
    if (!mount || !mount->ops.listdir) return -1;
    if (translate_path(mount, path, local_path, sizeof(local_path)) != 0) {
        return -1;
    }

    return mount->ops.listdir(local_path, entries, max_entries);
}
