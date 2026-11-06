/* NumOS — vfs.h — Virtual Filesystem layer
 *
 * A small POSIX-style file API over mountable filesystems. Mounts map a
 * path prefix to a filesystem instance backed by a block device; open,
 * read, write, stat, list and unlink calls resolve the owning mount and
 * delegate. Filesystem implementations live below this interface (FAT32).
 */
#ifndef NUMOS_FS_VFS_H
#define NUMOS_FS_VFS_H

#include "libs/types.h"
#include "drivers/block.h"

#define VFS_PATH_MAX 128
#define VFS_NAME_MAX 64
#define VFS_MAX_MOUNTS 4
#define VFS_MAX_OPEN 16

#define VFS_OK        0
#define VFS_ENOENT    1
#define VFS_EINVAL    2
#define VFS_EIO       3
#define VFS_ENOSPC    4
#define VFS_EEXIST    5
#define VFS_EMFILE    6
#define VFS_ENODEV    7

#define VFS_O_RDONLY  0x0000
#define VFS_O_WRONLY  0x0001
#define VFS_O_RDWR    0x0002
#define VFS_O_CREAT   0x0100
#define VFS_O_TRUNC   0x0200

struct vfs_filesystem;

struct vfs_stat {
    char name[VFS_NAME_MAX];
    bool is_dir;
    uint64_t size;
};

typedef int (*vfs_list_cb)(const struct vfs_stat* st, void* ctx);

struct vfs_file {
    struct vfs_filesystem* fs;
    uint64_t pos;
    void* fdata;
};

struct vfs_ops {
    int (*open)(struct vfs_filesystem* fs, const char* path, int flags, struct vfs_file** out);
    int (*read)(struct vfs_file* f, void* buffer, size_t len, size_t* out_len);
    int (*write)(struct vfs_file* f, const void* buffer, size_t len, size_t* out_len);
    int (*close)(struct vfs_file* f);
    int (*stat)(struct vfs_filesystem* fs, const char* path, struct vfs_stat* out);
    int (*list)(struct vfs_filesystem* fs, const char* path, vfs_list_cb cb, void* ctx);
    int (*unlink)(struct vfs_filesystem* fs, const char* path);
    int (*sync)(struct vfs_filesystem* fs);
};

struct vfs_filesystem {
    char type[16];
    struct block_device* dev;
    const struct vfs_ops* ops;
    void* fs_data;
};

bool vfs_init(void);
int vfs_mount(const char* path, struct vfs_filesystem* fs);
int vfs_unmount(const char* path);
uint32_t vfs_get_mount_count(void);
const char* vfs_get_mount_path(uint32_t index);
struct vfs_filesystem* vfs_get_mount_fs(uint32_t index);

int vfs_open(const char* path, int flags, struct vfs_file** out);
int vfs_read(struct vfs_file* f, void* buffer, size_t len, size_t* out_len);
int vfs_write(struct vfs_file* f, const void* buffer, size_t len, size_t* out_len);
int vfs_close(struct vfs_file* f);
int vfs_stat(const char* path, struct vfs_stat* out);
int vfs_list(const char* path, vfs_list_cb cb, void* ctx);
int vfs_unlink(const char* path);
int vfs_sync(void);

#endif
