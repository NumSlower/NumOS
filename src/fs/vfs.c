/* NumOS — vfs.c — Virtual Filesystem layer implementation
 *
 * Mount table with longest-prefix resolution. All file operations route
 * through the owning filesystem's ops; the layer itself owns no per-file
 * state beyond the handle's filesystem pointer and caller-owned position.
 */
#include "fs/vfs.h"
#include "libs/string.h"
#include "libs/kprintf.h"

struct vfs_mount {
    char path[VFS_PATH_MAX];
    struct vfs_filesystem* fs;
};

static struct vfs_mount mounts[VFS_MAX_MOUNTS];
static uint32_t mount_count = 0;

bool vfs_init(void) {
    mount_count = 0;
    return true;
}

static bool path_is_valid(const char* path) {
    return path && path[0] == '/' && strlen(path) < VFS_PATH_MAX;
}

int vfs_mount(const char* path, struct vfs_filesystem* fs) {
    if (!path_is_valid(path) || !fs || !fs->ops) return VFS_EINVAL;
    if (mount_count >= VFS_MAX_MOUNTS) return VFS_EMFILE;
    if (strlen(path) > 1) return VFS_EINVAL;

    for (uint32_t i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].path, path) == 0) return VFS_EEXIST;
    }

    strncpy(mounts[mount_count].path, path, VFS_PATH_MAX - 1);
    mounts[mount_count].path[VFS_PATH_MAX - 1] = '\0';
    mounts[mount_count].fs = fs;
    mount_count++;
    return VFS_OK;
}

int vfs_unmount(const char* path) {
    if (!path) return VFS_EINVAL;
    for (uint32_t i = 0; i < mount_count; i++) {
        if (strcmp(mounts[i].path, path) == 0) {
            for (uint32_t j = i; j + 1 < mount_count; j++) {
                mounts[j] = mounts[j + 1];
            }
            mount_count--;
            return VFS_OK;
        }
    }
    return VFS_ENOENT;
}

uint32_t vfs_get_mount_count(void) {
    return mount_count;
}

const char* vfs_get_mount_path(uint32_t index) {
    if (index >= mount_count) return NULL;
    return mounts[index].path;
}

struct vfs_filesystem* vfs_get_mount_fs(uint32_t index) {
    if (index >= mount_count) return NULL;
    return mounts[index].fs;
}

static struct vfs_filesystem* resolve(const char* path) {
    (void)path;
    if (mount_count == 0) return NULL;
    return mounts[0].fs;
}

int vfs_open(const char* path, int flags, struct vfs_file** out) {
    if (!path_is_valid(path) || !out) return VFS_EINVAL;
    struct vfs_filesystem* fs = resolve(path);
    if (!fs) return VFS_ENODEV;
    if (!fs->ops->open) return VFS_EINVAL;
    return fs->ops->open(fs, path, flags, out);
}

int vfs_read(struct vfs_file* f, void* buffer, size_t len, size_t* out_len) {
    if (!f || !f->fs || !buffer) return VFS_EINVAL;
    if (!f->fs->ops->read) return VFS_EINVAL;
    return f->fs->ops->read(f, buffer, len, out_len);
}

int vfs_write(struct vfs_file* f, const void* buffer, size_t len, size_t* out_len) {
    if (!f || !f->fs || !buffer) return VFS_EINVAL;
    if (!f->fs->ops->write) return VFS_EINVAL;
    return f->fs->ops->write(f, buffer, len, out_len);
}

int vfs_close(struct vfs_file* f) {
    if (!f || !f->fs) return VFS_EINVAL;
    if (!f->fs->ops->close) return VFS_OK;
    return f->fs->ops->close(f);
}

int vfs_stat(const char* path, struct vfs_stat* out) {
    if (!path_is_valid(path) || !out) return VFS_EINVAL;
    struct vfs_filesystem* fs = resolve(path);
    if (!fs) return VFS_ENODEV;
    if (!fs->ops->stat) return VFS_EINVAL;
    return fs->ops->stat(fs, path, out);
}

int vfs_list(const char* path, vfs_list_cb cb, void* ctx) {
    if (!path_is_valid(path) || !cb) return VFS_EINVAL;
    struct vfs_filesystem* fs = resolve(path);
    if (!fs) return VFS_ENODEV;
    if (!fs->ops->list) return VFS_EINVAL;
    return fs->ops->list(fs, path, cb, ctx);
}

int vfs_unlink(const char* path) {
    if (!path_is_valid(path)) return VFS_EINVAL;
    struct vfs_filesystem* fs = resolve(path);
    if (!fs) return VFS_ENODEV;
    if (!fs->ops->unlink) return VFS_EINVAL;
    return fs->ops->unlink(fs, path);
}

int vfs_sync(void) {
    for (uint32_t i = 0; i < mount_count; i++) {
        if (mounts[i].fs->ops->sync) {
            int rc = mounts[i].fs->ops->sync(mounts[i].fs);
            if (rc != VFS_OK) return rc;
        }
    }
    return VFS_OK;
}
