/*
 * vfs.h - Virtual Filesystem Layer
 * Provides unified interface for multiple filesystem types
 */

#ifndef VFS_H
#define VFS_H

#include "lib/base.h"

/* VFS Configuration */
#define VFS_MAX_FILESYSTEMS   8
#define VFS_MAX_MOUNTS        16
#define VFS_MAX_OPEN_FILES    64
#define VFS_MAX_PATH          512
#define VFS_MAX_NAME          256

/* File types */
#define VFS_TYPE_UNKNOWN      0
#define VFS_TYPE_REGULAR      1
#define VFS_TYPE_DIRECTORY    2
#define VFS_TYPE_SYMLINK      3
#define VFS_TYPE_DEVICE       4
#define VFS_TYPE_PIPE         5

/* File permissions */
#define VFS_PERM_READ         0x0004
#define VFS_PERM_WRITE        0x0002
#define VFS_PERM_EXECUTE      0x0001
#define VFS_PERM_USER_RWX     0x01C0
#define VFS_PERM_GROUP_RWX    0x0038
#define VFS_PERM_OTHER_RWX    0x0007

/* File open modes */
#define VFS_O_RDONLY          0x0001
#define VFS_O_WRONLY          0x0002
#define VFS_O_RDWR            0x0003
#define VFS_O_CREAT           0x0100
#define VFS_O_EXCL            0x0200
#define VFS_O_TRUNC           0x0400
#define VFS_O_APPEND          0x0800

/* Seek origins */
#define VFS_SEEK_SET          0
#define VFS_SEEK_CUR          1
#define VFS_SEEK_END          2

/* Error codes */
#define VFS_SUCCESS           0
#define VFS_ERROR_GENERIC     1
#define VFS_ERROR_NOT_FOUND   2
#define VFS_ERROR_EXISTS      3
#define VFS_ERROR_NO_SPACE    4
#define VFS_ERROR_INVALID     5
#define VFS_ERROR_NO_MEMORY   6
#define VFS_ERROR_IO          7
#define VFS_ERROR_PERM        8

/* File statistics */
struct vfs_stat {
    uint64_t inode;           /* Inode number */
    uint32_t mode;            /* File type and permissions */
    uint32_t nlink;           /* Number of hard links */
    uid_t uid;                /* User ID */
    gid_t gid;                /* Group ID */
    uint64_t size;            /* File size in bytes */
    uint64_t blocks;          /* Number of blocks allocated */
    uint32_t blksize;         /* Block size */
    uint64_t atime;           /* Last access time */
    uint64_t mtime;           /* Last modification time */
    uint64_t ctime;           /* Last status change time */
};

/* Directory entry */
struct vfs_dirent {
    uint64_t inode;           /* Inode number */
    uint32_t type;            /* File type */
    char name[VFS_MAX_NAME];  /* Filename */
};

/* Forward declarations */
struct vfs_filesystem;
struct vfs_mount;
struct vfs_file;

/* Filesystem operations */
struct vfs_operations {
    /* Mount/unmount */
    int (*mount)(struct vfs_mount *mount);
    int (*unmount)(struct vfs_mount *mount);
    
    /* File operations */
    int (*open)(struct vfs_file *file, const char *path, int flags);
    int (*close)(struct vfs_file *file);
    ssize_t (*read)(struct vfs_file *file, void *buffer, size_t size);
    ssize_t (*write)(struct vfs_file *file, const void *buffer, size_t size);
    off_t (*seek)(struct vfs_file *file, off_t offset, int whence);
    
    /* Directory operations */
    int (*readdir)(struct vfs_file *file, struct vfs_dirent *dirent);
    int (*mkdir)(const char *path, mode_t mode);
    int (*rmdir)(const char *path);
    
    /* File management */
    int (*unlink)(const char *path);
    int (*rename)(const char *oldpath, const char *newpath);
    int (*stat)(const char *path, struct vfs_stat *stat);
    
    /* Synchronization */
    int (*sync)(struct vfs_mount *mount);
};

/* Filesystem type */
struct vfs_filesystem {
    char name[32];                    /* Filesystem name */
    struct vfs_operations *ops;       /* Operations */
    void *private_data;               /* FS-specific data */
    int registered;                   /* Registration flag */
};

/* Mount point */
struct vfs_mount {
    char mountpoint[VFS_MAX_PATH];    /* Mount path */
    struct vfs_filesystem *fs;        /* Filesystem */
    uint8_t device_id;                /* Device identifier */
    uint32_t flags;                   /* Mount flags */
    void *private_data;               /* Mount-specific data */
    int mounted;                      /* Mount status */
};

/* Open file handle */
struct vfs_file {
    struct vfs_mount *mount;          /* Mount point */
    char path[VFS_MAX_PATH];          /* File path */
    int flags;                        /* Open flags */
    off_t position;                   /* Current position */
    void *private_data;               /* FS-specific data */
    int in_use;                       /* Handle in use */
};

/* Core VFS functions */
int vfs_init(void);
void vfs_shutdown(void);

/* Filesystem registration */
int vfs_register_filesystem(const char *name, struct vfs_operations *ops);
int vfs_unregister_filesystem(const char *name);
struct vfs_filesystem* vfs_get_filesystem(const char *name);

/* Mount management */
int vfs_mount(const char *source, const char *target, const char *fstype, uint32_t flags);
int vfs_unmount(const char *target);
struct vfs_mount* vfs_find_mount(const char *path);

/* File operations */
int vfs_open(const char *path, int flags);
int vfs_close(int fd);
ssize_t vfs_read(int fd, void *buffer, size_t size);
ssize_t vfs_write(int fd, const void *buffer, size_t size);
off_t vfs_seek(int fd, off_t offset, int whence);
int vfs_stat(const char *path, struct vfs_stat *stat);

/* Directory operations */
int vfs_readdir(int fd, struct vfs_dirent *dirent);
int vfs_mkdir(const char *path, mode_t mode);
int vfs_rmdir(const char *path);

/* File management */
int vfs_unlink(const char *path);
int vfs_rename(const char *oldpath, const char *newpath);

/* Utility functions */
int vfs_sync(void);
const char* vfs_normalize_path(const char *path, char *buffer);
int vfs_path_split(const char *path, char *dir, char *name);

/* Debug functions */
void vfs_print_mounts(void);
void vfs_print_open_files(void);
void vfs_print_stats(void);

#endif /* VFS_H */