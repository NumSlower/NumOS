#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/base.h"

/* System Call Numbers */
#define SYSCALL_READ       0
#define SYSCALL_WRITE      1
#define SYSCALL_OPEN       2
#define SYSCALL_CLOSE      3
#define SYSCALL_EXIT       4
#define SYSCALL_GETPID     5
#define SYSCALL_FORK       6
#define SYSCALL_EXEC       7
#define SYSCALL_SLEEP      8
#define SYSCALL_YIELD      9
#define SYSCALL_KILL       10
#define SYSCALL_BRK        11
#define SYSCALL_MMAP       12
#define SYSCALL_MUNMAP     13
#define SYSCALL_GETTIME    14
#define SYSCALL_UPTIME     15
#define SYSCALL_SYSINFO    16
#define SYSCALL_REBOOT     17
#define SYSCALL_SHUTDOWN   18
#define SYSCALL_CHDIR      19
#define SYSCALL_GETCWD     20
#define SYSCALL_MKDIR      21
#define SYSCALL_RMDIR      22
#define SYSCALL_UNLINK     23
#define SYSCALL_STAT       24
#define SYSCALL_IOCTL      25
#define SYSCALL_DUP        26
#define SYSCALL_PIPE       27
#define SYSCALL_GETDENTS   28
#define SYSCALL_LSEEK      29
#define SYSCALL_MAX        30

/* System Call Return Codes */
#define SYSCALL_SUCCESS    0
#define SYSCALL_ERROR     -1
#define SYSCALL_EINVAL    -2
#define SYSCALL_ENOMEM    -3
#define SYSCALL_ENOENT    -4
#define SYSCALL_EPERM     -5
#define SYSCALL_EIO       -6
#define SYSCALL_EBADF     -7

/* File descriptor constants */
#define STDIN_FILENO   0
#define STDOUT_FILENO  1
#define STDERR_FILENO  2

/* Open flags */
#define O_RDONLY    0x0000
#define O_WRONLY    0x0001
#define O_RDWR      0x0002
#define O_CREAT     0x0100
#define O_TRUNC     0x0200
#define O_APPEND    0x0400

/* Seek whence */
#define SEEK_SET    0
#define SEEK_CUR    1
#define SEEK_END    2

/* System information structure */
struct sysinfo {
    uint64_t uptime;           /* Seconds since boot */
    uint64_t total_memory;     /* Total usable memory */
    uint64_t free_memory;      /* Available memory */
    uint64_t used_memory;      /* Used memory */
    uint32_t process_count;    /* Number of processes */
    uint32_t cpu_freq;         /* CPU frequency in Hz */
    char version[32];          /* OS version string */
};

/* File stat structure */
struct stat {
    uint32_t st_mode;      /* File mode */
    uint32_t st_size;      /* File size in bytes */
    uint32_t st_blocks;    /* Number of blocks allocated */
    uint64_t st_atime;     /* Time of last access */
    uint64_t st_mtime;     /* Time of last modification */
    uint64_t st_ctime;     /* Time of last status change */
};

/* Initialize system call handler */
void syscall_init(void);

/* System call dispatcher */
int64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, 
                       uint64_t arg3, uint64_t arg4, uint64_t arg5);

/* User-space system call wrapper functions */
int64_t syscall0(uint64_t num);
int64_t syscall1(uint64_t num, uint64_t arg1);
int64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2);
int64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3);
int64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4);
int64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, 
                uint64_t arg4, uint64_t arg5);

/* Convenience wrapper functions */
static inline ssize_t sys_read(int fd, void *buf, size_t count) {
    return syscall3(SYSCALL_READ, fd, (uint64_t)buf, count);
}

static inline ssize_t sys_write(int fd, const void *buf, size_t count) {
    return syscall3(SYSCALL_WRITE, fd, (uint64_t)buf, count);
}

static inline int sys_open(const char *pathname, int flags) {
    return syscall2(SYSCALL_OPEN, (uint64_t)pathname, flags);
}

static inline int sys_close(int fd) {
    return syscall1(SYSCALL_CLOSE, fd);
}

static inline void sys_exit(int status) {
    syscall1(SYSCALL_EXIT, status);
}

static inline int sys_sleep(uint32_t ms) {
    return syscall1(SYSCALL_SLEEP, ms);
}

static inline uint64_t sys_uptime(void) {
    return syscall0(SYSCALL_UPTIME);
}

static inline int sys_sysinfo(struct sysinfo *info) {
    return syscall1(SYSCALL_SYSINFO, (uint64_t)info);
}

static inline void sys_reboot(void) {
    syscall0(SYSCALL_REBOOT);
}

static inline int sys_stat(const char *pathname, struct stat *statbuf) {
    return syscall2(SYSCALL_STAT, (uint64_t)pathname, (uint64_t)statbuf);
}

static inline int sys_unlink(const char *pathname) {
    return syscall1(SYSCALL_UNLINK, (uint64_t)pathname);
}

static inline off_t sys_lseek(int fd, off_t offset, int whence) {
    return syscall3(SYSCALL_LSEEK, fd, offset, whence);
}

/* Print system call statistics */
void syscall_print_stats(void);

#endif /* SYSCALL_H */