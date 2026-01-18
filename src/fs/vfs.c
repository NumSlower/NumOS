/*
 * vfs.c - Virtual Filesystem Implementation
 */

#include "fs/vfs.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/heap.h"

/* Global VFS state */
static struct {
    struct vfs_filesystem filesystems[VFS_MAX_FILESYSTEMS];
    struct vfs_mount mounts[VFS_MAX_MOUNTS];
    struct vfs_file files[VFS_MAX_OPEN_FILES];
    int initialized;
    uint64_t total_reads;
    uint64_t total_writes;
    uint64_t total_seeks;
} g_vfs = {0};

/* Initialize VFS layer */
int vfs_init(void) {
    if (g_vfs.initialized) {
        return VFS_SUCCESS;
    }
    
    vga_writestring("VFS: Initializing virtual filesystem layer...\n");
    
    memset(&g_vfs, 0, sizeof(g_vfs));
    
    g_vfs.initialized = 1;
    g_vfs.total_reads = 0;
    g_vfs.total_writes = 0;
    g_vfs.total_seeks = 0;
    
    vga_writestring("VFS: Virtual filesystem layer initialized\n");
    return VFS_SUCCESS;
}

/* Shutdown VFS layer */
void vfs_shutdown(void) {
    if (!g_vfs.initialized) {
        return;
    }
    
    vga_writestring("VFS: Shutting down...\n");
    
    /* Close all open files */
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (g_vfs.files[i].in_use) {
            vfs_close(i);
        }
    }
    
    /* Unmount all filesystems */
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_vfs.mounts[i].mounted) {
            vfs_unmount(g_vfs.mounts[i].mountpoint);
        }
    }
    
    g_vfs.initialized = 0;
    vga_writestring("VFS: Shutdown complete\n");
}

/* Register filesystem type */
int vfs_register_filesystem(const char *name, struct vfs_operations *ops) {
    if (!name || !ops) {
        return VFS_ERROR_INVALID;
    }
    
    /* Find free slot */
    for (int i = 0; i < VFS_MAX_FILESYSTEMS; i++) {
        if (!g_vfs.filesystems[i].registered) {
            strncpy(g_vfs.filesystems[i].name, name, 31);
            g_vfs.filesystems[i].name[31] = '\0';
            g_vfs.filesystems[i].ops = ops;
            g_vfs.filesystems[i].registered = 1;
            
            vga_writestring("VFS: Registered filesystem: ");
            vga_writestring(name);
            vga_putchar('\n');
            return VFS_SUCCESS;
        }
    }
    
    return VFS_ERROR_NO_SPACE;
}

/* Get filesystem by name */
struct vfs_filesystem* vfs_get_filesystem(const char *name) {
    if (!name) {
        return NULL;
    }
    
    for (int i = 0; i < VFS_MAX_FILESYSTEMS; i++) {
        if (g_vfs.filesystems[i].registered && 
            strcmp(g_vfs.filesystems[i].name, name) == 0) {
            return &g_vfs.filesystems[i];
        }
    }
    
    return NULL;
}

/* Mount filesystem */
int vfs_mount(const char *source __attribute__((unused)), const char *target, const char *fstype, uint32_t flags) {
    if (!target || !fstype) {
        return VFS_ERROR_INVALID;
    }
    
    struct vfs_filesystem *fs = vfs_get_filesystem(fstype);
    if (!fs) {
        vga_writestring("VFS: Unknown filesystem type: ");
        vga_writestring(fstype);
        vga_putchar('\n');
        return VFS_ERROR_INVALID;
    }
    
    /* Find free mount slot */
    struct vfs_mount *mount = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_vfs.mounts[i].mounted) {
            mount = &g_vfs.mounts[i];
            break;
        }
    }
    
    if (!mount) {
        return VFS_ERROR_NO_SPACE;
    }
    
    /* Initialize mount */
    strncpy(mount->mountpoint, target, VFS_MAX_PATH - 1);
    mount->mountpoint[VFS_MAX_PATH - 1] = '\0';
    mount->fs = fs;
    mount->flags = flags;
    mount->private_data = NULL;
    
    /* Call filesystem mount operation */
    if (fs->ops->mount) {
        int result = fs->ops->mount(mount);
        if (result != VFS_SUCCESS) {
            return result;
        }
    }
    
    mount->mounted = 1;
    
    vga_writestring("VFS: Mounted ");
    vga_writestring(fstype);
    vga_writestring(" at ");
    vga_writestring(target);
    vga_putchar('\n');
    
    return VFS_SUCCESS;
}

/* Unmount filesystem */
int vfs_unmount(const char *target) {
    if (!target) {
        return VFS_ERROR_INVALID;
    }
    
    /* Find mount point */
    struct vfs_mount *mount = NULL;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_vfs.mounts[i].mounted && 
            strcmp(g_vfs.mounts[i].mountpoint, target) == 0) {
            mount = &g_vfs.mounts[i];
            break;
        }
    }
    
    if (!mount) {
        return VFS_ERROR_NOT_FOUND;
    }
    
    /* Call filesystem unmount operation */
    if (mount->fs && mount->fs->ops && mount->fs->ops->unmount) {
        int result = mount->fs->ops->unmount(mount);
        if (result != VFS_SUCCESS) {
            return result;
        }
    }
    
    mount->mounted = 0;
    
    vga_writestring("VFS: Unmounted ");
    vga_writestring(target);
    vga_putchar('\n');
    
    return VFS_SUCCESS;
}

/* Find mount point for path */
struct vfs_mount* vfs_find_mount(const char *path) {
    if (!path) {
        return NULL;
    }
    
    struct vfs_mount *best_match = NULL;
    size_t best_len = 0;
    
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (!g_vfs.mounts[i].mounted) {
            continue;
        }
        
        size_t mount_len = strlen(g_vfs.mounts[i].mountpoint);
        
        if (strncmp(path, g_vfs.mounts[i].mountpoint, mount_len) == 0) {
            if (mount_len > best_len) {
                best_match = &g_vfs.mounts[i];
                best_len = mount_len;
            }
        }
    }
    
    return best_match;
}

/* Open file */
int vfs_open(const char *path, int flags) {
    if (!path) {
        return -VFS_ERROR_INVALID;
    }
    
    struct vfs_mount *mount = vfs_find_mount(path);
    if (!mount) {
        return -VFS_ERROR_NOT_FOUND;
    }
    
    /* Find free file descriptor */
    int fd = -1;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (!g_vfs.files[i].in_use) {
            fd = i;
            break;
        }
    }
    
    if (fd < 0) {
        return -VFS_ERROR_NO_SPACE;
    }
    
    /* Initialize file handle */
    struct vfs_file *file = &g_vfs.files[fd];
    file->mount = mount;
    strncpy(file->path, path, VFS_MAX_PATH - 1);
    file->path[VFS_MAX_PATH - 1] = '\0';
    file->flags = flags;
    file->position = 0;
    file->private_data = NULL;
    
    /* Call filesystem open operation */
    if (mount->fs->ops->open) {
        int result = mount->fs->ops->open(file, path, flags);
        if (result != VFS_SUCCESS) {
            return -result;
        }
    }
    
    file->in_use = 1;
    return fd;
}

/* Close file */
int vfs_close(int fd) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_vfs.files[fd].in_use) {
        return VFS_ERROR_INVALID;
    }
    
    struct vfs_file *file = &g_vfs.files[fd];
    
    /* Call filesystem close operation */
    if (file->mount && file->mount->fs->ops->close) {
        file->mount->fs->ops->close(file);
    }
    
    file->in_use = 0;
    return VFS_SUCCESS;
}

/* Read from file */
ssize_t vfs_read(int fd, void *buffer, size_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_vfs.files[fd].in_use) {
        return -VFS_ERROR_INVALID;
    }
    
    struct vfs_file *file = &g_vfs.files[fd];
    
    if (!file->mount || !file->mount->fs->ops->read) {
        return -VFS_ERROR_INVALID;
    }
    
    ssize_t result = file->mount->fs->ops->read(file, buffer, size);
    
    if (result > 0) {
        g_vfs.total_reads++;
    }
    
    return result;
}

/* Write to file */
ssize_t vfs_write(int fd, const void *buffer, size_t size) {
    if (fd < 0 || fd >= VFS_MAX_OPEN_FILES || !g_vfs.files[fd].in_use) {
        return -VFS_ERROR_INVALID;
    }
    
    struct vfs_file *file = &g_vfs.files[fd];
    
    if (!file->mount || !file->mount->fs->ops->write) {
        return -VFS_ERROR_INVALID;
    }
    
    ssize_t result = file->mount->fs->ops->write(file, buffer, size);
    
    if (result > 0) {
        g_vfs.total_writes++;
    }
    
    return result;
}

/* Print VFS statistics */
void vfs_print_stats(void) {
    vga_writestring("VFS Statistics:\n");
    vga_writestring("  Total reads: ");
    print_dec(g_vfs.total_reads);
    vga_writestring("\n  Total writes: ");
    print_dec(g_vfs.total_writes);
    vga_writestring("\n  Total seeks: ");
    print_dec(g_vfs.total_seeks);
    
    int mounted = 0;
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_vfs.mounts[i].mounted) mounted++;
    }
    
    int open_files = 0;
    for (int i = 0; i < VFS_MAX_OPEN_FILES; i++) {
        if (g_vfs.files[i].in_use) open_files++;
    }
    
    vga_writestring("\n  Mounted filesystems: ");
    print_dec(mounted);
    vga_writestring("\n  Open files: ");
    print_dec(open_files);
    vga_putchar('\n');
}

/* Print mount points */
void vfs_print_mounts(void) {
    vga_writestring("Mounted Filesystems:\n");
    vga_writestring("Mount Point          Filesystem Type\n");
    vga_writestring("-------------------- ---------------\n");
    
    for (int i = 0; i < VFS_MAX_MOUNTS; i++) {
        if (g_vfs.mounts[i].mounted) {
            vga_writestring(g_vfs.mounts[i].mountpoint);
            
            int len = strlen(g_vfs.mounts[i].mountpoint);
            for (int j = len; j < 21; j++) {
                vga_putchar(' ');
            }
            
            vga_writestring(g_vfs.mounts[i].fs->name);
            vga_putchar('\n');
        }
    }
}