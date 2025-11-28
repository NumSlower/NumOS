#include "kernel/syscall.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "cpu/heap.h"
#include "cpu/paging.h"
#include "fs/fat32.h"
#include "cpu/idt.h"
#include "cpu/gdt.h"

/* NULL definition if not in kernel.h */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* External assembly function for syscall entry */
extern void syscall_entry_asm(void);

/* System call statistics */
struct syscall_stats {
    uint64_t total_calls;
    uint64_t calls_per_syscall[SYSCALL_MAX];
    uint64_t errors;
};

static struct syscall_stats syscall_stats = {0};

/* File descriptor table (simplified) */
#define MAX_OPEN_FILES 16
struct file_descriptor {
    int in_use;
    struct fat32_file *file;
    int flags;
    off_t position;
};

static struct file_descriptor fd_table[MAX_OPEN_FILES];

/* Forward declarations for syscall handlers */
static int64_t sys_read_impl(int fd, void *buf, size_t count);
static int64_t sys_write_impl(int fd, const void *buf, size_t count);
static int64_t sys_open_impl(const char *pathname, int flags);
static int64_t sys_close_impl(int fd);
static int64_t sys_exit_impl(int status);
static int64_t sys_sleep_impl(uint32_t ms);
static int64_t sys_uptime_impl(void);
static int64_t sys_sysinfo_impl(struct sysinfo *info);
static int64_t sys_reboot_impl(void);
static int64_t sys_stat_impl(const char *pathname, struct stat *statbuf);
static int64_t sys_unlink_impl(const char *pathname);
static int64_t sys_lseek_impl(int fd, off_t offset, int whence);

/* Helper functions */
static int allocate_fd(void);
static void free_fd(int fd);
static int validate_fd(int fd);
static int validate_user_pointer(const void *ptr, size_t size);

void syscall_init(void) {
    vga_writestring("Initializing system call interface...\n");
    
    /* Clear file descriptor table */
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        fd_table[i].in_use = 0;
        fd_table[i].file = NULL;
        fd_table[i].flags = 0;
        fd_table[i].position = 0;
    }
    
    /* Reserve stdin, stdout, stderr */
    fd_table[STDIN_FILENO].in_use = 1;
    fd_table[STDOUT_FILENO].in_use = 1;
    fd_table[STDERR_FILENO].in_use = 1;
    
    /* Clear statistics */
    memset(&syscall_stats, 0, sizeof(syscall_stats));
    
    /* Register syscall interrupt handler (INT 0x80) */
    idt_set_gate(0x80, (uint64_t)syscall_entry_asm, GDT_KERNEL_CODE, 
                IDT_ATTR_PRESENT | IDT_ATTR_DPL3 | IDT_TYPE_INTERRUPT);
    
    vga_writestring("System call interface initialized (INT 0x80)\n");
}

int64_t syscall_handler(uint64_t syscall_num, uint64_t arg1, uint64_t arg2, 
                       uint64_t arg3, uint64_t arg4, uint64_t arg5) {
    /* Suppress unused parameter warnings for now */
    (void)arg4;
    (void)arg5;
    
    /* Update statistics */
    syscall_stats.total_calls++;
    
    if (syscall_num >= SYSCALL_MAX) {
        syscall_stats.errors++;
        return SYSCALL_EINVAL;
    }
    
    syscall_stats.calls_per_syscall[syscall_num]++;
    
    /* Dispatch to appropriate handler */
    int64_t result = SYSCALL_ERROR;
    
    switch (syscall_num) {
        case SYSCALL_READ:
            result = sys_read_impl((int)arg1, (void*)arg2, (size_t)arg3);
            break;
            
        case SYSCALL_WRITE:
            result = sys_write_impl((int)arg1, (const void*)arg2, (size_t)arg3);
            break;
            
        case SYSCALL_OPEN:
            result = sys_open_impl((const char*)arg1, (int)arg2);
            break;
            
        case SYSCALL_CLOSE:
            result = sys_close_impl((int)arg1);
            break;
            
        case SYSCALL_EXIT:
            result = sys_exit_impl((int)arg1);
            break;
            
        case SYSCALL_SLEEP:
            result = sys_sleep_impl((uint32_t)arg1);
            break;
            
        case SYSCALL_UPTIME:
            result = sys_uptime_impl();
            break;
            
        case SYSCALL_SYSINFO:
            result = sys_sysinfo_impl((struct sysinfo*)arg1);
            break;
            
        case SYSCALL_REBOOT:
            result = sys_reboot_impl();
            break;
            
        case SYSCALL_STAT:
            result = sys_stat_impl((const char*)arg1, (struct stat*)arg2);
            break;
            
        case SYSCALL_UNLINK:
            result = sys_unlink_impl((const char*)arg1);
            break;
            
        case SYSCALL_LSEEK:
            result = sys_lseek_impl((int)arg1, (off_t)arg2, (int)arg3);
            break;
            
        default:
            /* Unimplemented system call */
            syscall_stats.errors++;
            result = SYSCALL_EINVAL;
            break;
    }
    
    /* Track errors */
    if (result < 0) {
        syscall_stats.errors++;
    }
    
    return result;
}

/* System call implementations */
static int64_t sys_read_impl(int fd, void *buf, size_t count) {
    if (!validate_fd(fd) || !validate_user_pointer(buf, count)) {
        return SYSCALL_EINVAL;
    }
    
    /* Handle stdin specially */
    if (fd == STDIN_FILENO) {
        /* Would read from keyboard - not implemented yet */
        return SYSCALL_EINVAL;
    }
    
    struct file_descriptor *file_desc = &fd_table[fd];
    if (!file_desc->file) {
        return SYSCALL_EBADF;
    }
    
    size_t bytes_read = fat32_fread(buf, 1, count, file_desc->file);
    return bytes_read;
}

static int64_t sys_write_impl(int fd, const void *buf, size_t count) {
    if (!validate_user_pointer(buf, count)) {
        return SYSCALL_EINVAL;
    }
    
    /* Handle stdout/stderr specially */
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO) {
        const char *str = (const char*)buf;
        for (size_t i = 0; i < count; i++) {
            vga_putchar(str[i]);
        }
        return count;
    }
    
    if (!validate_fd(fd)) {
        return SYSCALL_EINVAL;
    }
    
    struct file_descriptor *file_desc = &fd_table[fd];
    if (!file_desc->file) {
        return SYSCALL_EBADF;
    }
    
    size_t bytes_written = fat32_fwrite(buf, 1, count, file_desc->file);
    return bytes_written;
}

static int64_t sys_open_impl(const char *pathname, int flags) {
    if (!validate_user_pointer(pathname, 1)) {
        return SYSCALL_EINVAL;
    }
    
    /* Allocate file descriptor */
    int fd = allocate_fd();
    if (fd < 0) {
        return SYSCALL_ENOMEM;
    }
    
    /* Convert flags to FAT32 mode string */
    const char *mode;
    if (flags & O_CREAT) {
        if (flags & O_TRUNC) {
            mode = "w";
        } else if (flags & O_APPEND) {
            mode = "a";
        } else {
            mode = "w+";
        }
    } else {
        if (flags & O_WRONLY) {
            mode = "w";
        } else if (flags & O_RDWR) {
            mode = "r+";
        } else {
            mode = "r";
        }
    }
    
    /* Open the file */
    struct fat32_file *file = fat32_fopen(pathname, mode);
    if (!file) {
        free_fd(fd);
        return SYSCALL_ENOENT;
    }
    
    /* Set up file descriptor */
    fd_table[fd].file = file;
    fd_table[fd].flags = flags;
    fd_table[fd].position = 0;
    
    return fd;
}

static int64_t sys_close_impl(int fd) {
    if (!validate_fd(fd)) {
        return SYSCALL_EINVAL;
    }
    
    /* Don't allow closing stdin/stdout/stderr */
    if (fd <= STDERR_FILENO) {
        return SYSCALL_EPERM;
    }
    
    struct file_descriptor *file_desc = &fd_table[fd];
    if (!file_desc->file) {
        return SYSCALL_EBADF;
    }
    
    /* Close the file */
    int result = fat32_fclose(file_desc->file);
    
    /* Free the file descriptor */
    free_fd(fd);
    
    return (result == FAT32_SUCCESS) ? SYSCALL_SUCCESS : SYSCALL_ERROR;
}

static int64_t sys_exit_impl(int status) {
    vga_writestring("Process exiting with status: ");
    print_dec(status);
    vga_putchar('\n');
    
    /* In a real OS, this would terminate the process */
    /* For now, we just return to the shell */
    return SYSCALL_SUCCESS;
}

static int64_t sys_sleep_impl(uint32_t ms) {
    timer_sleep(ms);
    return SYSCALL_SUCCESS;
}

static int64_t sys_uptime_impl(void) {
    return timer_get_uptime_ms();
}

static int64_t sys_sysinfo_impl(struct sysinfo *info) {
    if (!validate_user_pointer(info, sizeof(struct sysinfo))) {
        return SYSCALL_EINVAL;
    }
    
    /* Fill in system information */
    info->uptime = timer_get_uptime_ms() / 1000;
    
    struct heap_stats heap = heap_get_stats();
    info->total_memory = heap.total_size;
    info->free_memory = heap.free_size;
    info->used_memory = heap.used_size;
    
    info->process_count = 1; /* Only kernel for now */
    info->cpu_freq = 0; /* Unknown */
    
    strcpy(info->version, "NumOS v2.2");
    
    return SYSCALL_SUCCESS;
}

static int64_t sys_reboot_impl(void) {
    vga_writestring("System reboot requested via syscall...\n");
    timer_sleep(1000); /* Give user time to see message */
    
    /* Reboot via keyboard controller */
    outb(0x64, 0xFE);
    
    /* Should not reach here */
    hang();
    return SYSCALL_SUCCESS;
}

static int64_t sys_stat_impl(const char *pathname, struct stat *statbuf) {
    if (!validate_user_pointer(pathname, 1) || !validate_user_pointer(statbuf, sizeof(struct stat))) {
        return SYSCALL_EINVAL;
    }
    
    /* Check if file exists */
    if (!fat32_exists(pathname)) {
        return SYSCALL_ENOENT;
    }
    
    /* Get file size */
    uint32_t size = fat32_get_file_size(pathname);
    
    /* Fill in stat structure */
    memset(statbuf, 0, sizeof(struct stat));
    statbuf->st_mode = 0644; /* Regular file, rw-r--r-- */
    statbuf->st_size = size;
    statbuf->st_blocks = (size + 511) / 512;
    statbuf->st_atime = 0;
    statbuf->st_mtime = 0;
    statbuf->st_ctime = 0;
    
    return SYSCALL_SUCCESS;
}

static int64_t sys_unlink_impl(const char *pathname) {
    if (!validate_user_pointer(pathname, 1)) {
        return SYSCALL_EINVAL;
    }
    
    /* Check if file exists */
    if (!fat32_exists(pathname)) {
        return SYSCALL_ENOENT;
    }
    
    /* In a real implementation, we'd delete the file */
    /* For now, FAT32 doesn't have a delete function */
    return SYSCALL_EINVAL; /* Not implemented */
}

static int64_t sys_lseek_impl(int fd, off_t offset, int whence) {
    if (!validate_fd(fd)) {
        return SYSCALL_EINVAL;
    }
    
    struct file_descriptor *file_desc = &fd_table[fd];
    if (!file_desc->file) {
        return SYSCALL_EBADF;
    }
    
    int result = fat32_fseek(file_desc->file, offset, whence);
    if (result != FAT32_SUCCESS) {
        return SYSCALL_ERROR;
    }
    
    return fat32_ftell(file_desc->file);
}

/* Helper functions */
static int allocate_fd(void) {
    for (int i = 3; i < MAX_OPEN_FILES; i++) { /* Start after stdin/stdout/stderr */
        if (!fd_table[i].in_use) {
            fd_table[i].in_use = 1;
            return i;
        }
    }
    return -1; /* No free descriptors */
}

static void free_fd(int fd) {
    if (fd >= 0 && fd < MAX_OPEN_FILES) {
        fd_table[fd].in_use = 0;
        fd_table[fd].file = NULL;
        fd_table[fd].flags = 0;
        fd_table[fd].position = 0;
    }
}

static int validate_fd(int fd) {
    return (fd >= 0 && fd < MAX_OPEN_FILES && fd_table[fd].in_use);
}

static int validate_user_pointer(const void *ptr, size_t size) {
    /* In a real OS, we'd check if the pointer is in user space */
    /* For now, just check if it's not NULL */
    if (!ptr) {
        return 0;
    }
    
    /* Could also check if the memory region is mapped */
    (void)size; /* Suppress warning */
    return 1;
}

/* User-space system call wrappers */
int64_t syscall0(uint64_t num) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num)
        : "memory"
    );
    return result;
}

int64_t syscall1(uint64_t num, uint64_t arg1) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1)
        : "memory"
    );
    return result;
}

int64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "memory"
    );
    return result;
}

int64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory"
    );
    return result;
}

int64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    int64_t result;
    register uint64_t r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "memory"
    );
    return result;
}

int64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, 
                uint64_t arg4, uint64_t arg5) {
    int64_t result;
    register uint64_t r10 __asm__("r10") = arg4;
    register uint64_t r8 __asm__("r8") = arg5;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
        : "memory"
    );
    return result;
}

void syscall_print_stats(void) {
    vga_writestring("System Call Statistics:\n");
    vga_writestring("  Total calls:  ");
    print_dec(syscall_stats.total_calls);
    vga_writestring("\n  Errors:       ");
    print_dec(syscall_stats.errors);
    vga_writestring("\n\n");
    
    vga_writestring("Calls by type:\n");
    const char *syscall_names[] = {
        "read", "write", "open", "close", "exit", "getpid", "fork", "exec",
        "sleep", "yield", "kill", "brk", "mmap", "munmap", "gettime", "uptime",
        "sysinfo", "reboot", "shutdown", "chdir", "getcwd", "mkdir", "rmdir",
        "unlink", "stat", "ioctl", "dup", "pipe", "getdents", "lseek"
    };
    
    for (int i = 0; i < SYSCALL_MAX; i++) {
        if (syscall_stats.calls_per_syscall[i] > 0) {
            vga_writestring("  ");
            vga_writestring(syscall_names[i]);
            vga_writestring(": ");
            print_dec(syscall_stats.calls_per_syscall[i]);
            vga_putchar('\n');
        }
    }
}