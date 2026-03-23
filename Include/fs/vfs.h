#ifndef VFS_H
#define VFS_H

#include "lib/base.h"

#define VFS_MAX_MOUNTS       4
#define VFS_MAX_OPEN_FILES   16
#define VFS_PATH_MAX         260
#define VFS_NAME_MAX         255

#define VFS_NODE_FILE        1
#define VFS_NODE_DIRECTORY   2

struct vfs_stat {
    char     name[VFS_NAME_MAX];
    uint32_t size;
    uint8_t  type;
    uint8_t  attr;
    uint32_t fs_data;
};

struct vfs_dirent {
    char     name[VFS_NAME_MAX];
    uint32_t size;
    uint8_t  type;
    uint8_t  attr;
    uint32_t fs_data;
};

struct vfs_ops {
    int     (*open)(const char *path, int flags);
    int     (*close)(int handle);
    ssize_t (*read)(int handle, void *buf, size_t count);
    ssize_t (*write)(int handle, const void *buf, size_t count);
    int     (*stat)(const char *path, struct vfs_stat *st);
    int     (*listdir)(const char *path, struct vfs_dirent *entries, int max_entries);
};

int     vfs_init(void);
int     vfs_register_fat32_root(void);
int     vfs_open(const char *path, int flags);
int     vfs_close(int fd);
ssize_t vfs_read(int fd, void *buf, size_t count);
ssize_t vfs_write(int fd, const void *buf, size_t count);
int     vfs_stat(const char *path, struct vfs_stat *st);
int     vfs_listdir(const char *path, struct vfs_dirent *entries, int max_entries);

#endif /* VFS_H */
