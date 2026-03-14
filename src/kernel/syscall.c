/*
 * syscall.c - NumOS System Call Implementation
 *
 * Changes from previous version:
 *   • sys_write(fd=1/2) now routes to the framebuffer console when the BGA
 *     framebuffer is active.  Existing user-space programs (elftest.asm)
 *     that write to fd=1 therefore see their output in the FB terminal with
 *     zero source changes.
 *   • Six new framebuffer syscalls: SYS_FB_INFO … SYS_FB_FILLRECT (201-206).
 */

#include "kernel/syscall.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/framebuffer.h"
#include "fs/fat32.h"
#include "cpu/gdt.h"
#include "kernel/scheduler.h"

/* =========================================================================
 * MSR helpers
 * ======================================================================= */

#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084
#define EFER_SCE    (1UL << 0)
#define SFMASK_IF   (1UL << 9)

extern char keyboard_getchar_buffered(void);
extern void syscall_entry(void);

static struct syscall_stats stats;
static int syscall_initialised = 0;

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
    return ((uint64_t)hi<<32)|lo;
}
static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"::"a"((uint32_t)value),"d"((uint32_t)(value>>32)),"c"(msr):"memory");
}

/* =========================================================================
 * Init
 * ======================================================================= */

void syscall_init(void) {
    if (syscall_initialised) return;
    vga_writestring("SYSCALL: Initializing SYSCALL/SYSRET...\n");
    memset(&stats, 0, sizeof(stats));

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    uint64_t star = 0;
    star |= ((uint64_t)GDT_KERNEL_CODE << 32);
    star |= ((uint64_t)GDT_KERNEL_DATA << 48);
    wrmsr(MSR_STAR,   star);
    wrmsr(MSR_LSTAR,  (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, SFMASK_IF);

    syscall_initialised = 1;
    vga_writestring("SYSCALL: Ready\n");
}

/* =========================================================================
 * Standard syscall implementations
 * ======================================================================= */

/*
 * sys_write - write to a file descriptor.
 *
 * fd=1 (stdout) and fd=2 (stderr):
 *   When the framebuffer is active, output goes to the FB console.
 *   Otherwise falls through to VGA text mode.
 *   This makes existing user-space programs work transparently in both
 *   display modes without any source changes.
 */
int64_t sys_write(int fd, const void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;
    if (fd != FD_STDOUT && fd != FD_STDERR) return SYSCALL_EBADF;

    const char *p = (const char *)buf;
    if (fb_is_available()) {
        /* Framebuffer active — write to FB console */
        for (size_t i = 0; i < count; i++) fb_con_putchar(p[i]);
    } else {
        /* VGA text mode fallback */
        for (size_t i = 0; i < count; i++) vga_putchar(p[i]);
    }
    return (int64_t)count;
}

int64_t sys_read(int fd, void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;

    /* fd=0: stdin, backed by the PS/2 keyboard ring buffer. */
    if (fd == FD_STDIN) {
        return sys_input(buf, count);
    }

    /* Reserve 1,2 for stdout/stderr. FAT32 file descriptors start at 3. */
    if (fd < 3) return SYSCALL_EBADF;

    int fat_fd = fd - 3;
    ssize_t n  = fat32_read(fat_fd, buf, count);
    if (n < 0) return SYSCALL_EBADF;
    return (int64_t)n;
}

int64_t sys_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path) return SYSCALL_EFAULT;

    int fat_fd = fat32_open(path, flags);
    if (fat_fd < 0) return SYSCALL_EINVAL;
    return (int64_t)(fat_fd + 3);
}

int64_t sys_close(int fd) {
    if (fd < 3) return SYSCALL_EBADF;
    int fat_fd = fd - 3;
    return (fat32_close(fat_fd) == 0) ? 0 : SYSCALL_EBADF;
}

int64_t sys_exit(int status) {
    process_exit(status);
    while (1) __asm__ volatile("hlt");
    return 0;
}

int64_t sys_getpid(void) {
    struct process *p = scheduler_current();
    return p ? (int64_t)p->pid : 1;
}

int64_t sys_sleep_ms(uint64_t ms) {
    process_sleep_until(timer_get_uptime_ms() + ms);
    return 0;
}

int64_t sys_uptime_ms(void) {
    return (int64_t)timer_get_uptime_ms();
}

int64_t sys_puts(const char *str) {
    if (!str) return SYSCALL_EFAULT;
    if (fb_is_available()) {
        fb_con_print(str);
        fb_con_putchar('\n');
    } else {
        vga_writestring(str);
        vga_putchar('\n');
    }
    return 0;
}

int64_t sys_input(void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;

    char  *p   = (char *)buf;
    size_t got = 0;
    while (got < count) {
        char c = keyboard_getchar_buffered();
        p[got++] = c;
        if (c == '\n') break;
    }
    return (int64_t)got;
}

/* =========================================================================
 * Framebuffer syscall implementations
 * ======================================================================= */

/*
 * sys_fb_info — query a single property of the framebuffer.
 * field: 0=width, 1=height, 2=bpp, 3=available
 */
int64_t sys_fb_info(uint64_t field) {
    switch (field) {
        case 0: return fb_get_width();
        case 1: return fb_get_height();
        case 2: return FB_BPP;
        case 3: return fb_is_available() ? 1 : 0;
        default: return SYSCALL_EINVAL;
    }
}

/*
 * sys_fb_write — write a buffer of chars to the framebuffer console.
 * When FB is not active, falls through to VGA.
 */
int64_t sys_fb_write(const char *buf, size_t len) {
    if (!buf) return SYSCALL_EFAULT;
    if (fb_is_available()) {
        for (size_t i = 0; i < len; i++) fb_con_putchar(buf[i]);
    } else {
        for (size_t i = 0; i < len; i++) vga_putchar(buf[i]);
    }
    return (int64_t)len;
}

/* sys_fb_clear — clear the framebuffer console. */
int64_t sys_fb_clear(void) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_con_clear();
    return 0;
}

/* sys_fb_setcolor — change the console text colour. */
int64_t sys_fb_setcolor(uint32_t fg, uint32_t bg) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_con_set_color(fg, bg);
    return 0;
}

/* sys_fb_setpixel — set a single pixel in the framebuffer. */
int64_t sys_fb_setpixel(int x, int y, uint32_t color) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_set_pixel(x, y, color);
    return 0;
}

/* sys_fb_fillrect — fill a rectangle in the framebuffer. */
int64_t sys_fb_fillrect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_fill_rect(x, y, w, h, color);
    return 0;
}

/* =========================================================================
 * Dispatcher
 * ======================================================================= */

int64_t syscall_dispatch(struct syscall_regs *regs) {
    uint64_t nr  = regs->rax;
    int64_t  ret = SYSCALL_ENOSYS;

    stats.total_calls++;
    if (nr < SYSCALL_MAX) stats.calls_per_number[nr]++;
    else                  stats.unknown_calls++;

    __asm__ volatile("sti");

    switch ((int)nr) {
        case SYS_READ:
            ret = sys_read((int)regs->rdi, (void*)regs->rsi, (size_t)regs->rdx);
            break;
        case SYS_INPUT:
            ret = sys_input((void*)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_WRITE:
            ret = sys_write((int)regs->rdi, (const void*)regs->rsi, (size_t)regs->rdx);
            break;
        case SYS_OPEN:
            ret = sys_open((const char*)regs->rdi, (int)regs->rsi, (int)regs->rdx);
            break;
        case SYS_CLOSE:
            ret = sys_close((int)regs->rdi);
            break;
        case SYS_EXIT:
            ret = sys_exit((int)regs->rdi);
            break;
        case SYS_GETPID:
            ret = sys_getpid();
            break;
        case SYS_SLEEP_MS:
            ret = sys_sleep_ms(regs->rdi);
            break;
        case SYS_UPTIME_MS:
            ret = sys_uptime_ms();
            break;
        case SYS_PUTS:
            ret = sys_puts((const char*)regs->rdi);
            break;

        /* Framebuffer syscalls */
        case SYS_FB_INFO:
            ret = sys_fb_info(regs->rdi);
            break;
        case SYS_FB_WRITE:
            ret = sys_fb_write((const char*)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_FB_CLEAR:
            ret = sys_fb_clear();
            break;
        case SYS_FB_SETCOLOR:
            ret = sys_fb_setcolor((uint32_t)regs->rdi, (uint32_t)regs->rsi);
            break;
        case SYS_FB_SETPIXEL:
            ret = sys_fb_setpixel((int)regs->rdi, (int)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_FB_FILLRECT:
            ret = sys_fb_fillrect((int)regs->rdi, (int)regs->rsi,
                                  (int)regs->rdx, (int)regs->r10,
                                  (uint32_t)regs->r8);
            break;

        default:
            stats.errors++;
            ret = SYSCALL_ENOSYS;
            break;
    }

    __asm__ volatile("cli");
    regs->rax = (uint64_t)ret;
    return ret;
}

/* =========================================================================
 * Diagnostics
 * ======================================================================= */

void syscall_print_stats(void) {
    static const char *names[SYSCALL_MAX];
    for (int i = 0; i < SYSCALL_MAX; i++) names[i] = NULL;
    names[SYS_READ]      = "read";
    names[SYS_INPUT]     = "input";
    names[SYS_WRITE]     = "write";
    names[SYS_OPEN]      = "open";
    names[SYS_CLOSE]     = "close";
    names[SYS_EXIT]      = "exit";
    names[SYS_GETPID]    = "getpid";
    names[SYS_SLEEP_MS]  = "sleep_ms";
    names[SYS_UPTIME_MS] = "uptime_ms";
    names[SYS_PUTS]      = "puts";
    names[SYS_FB_INFO]   = "fb_info";
    names[SYS_FB_WRITE]  = "fb_write";
    names[SYS_FB_CLEAR]  = "fb_clear";
    names[SYS_FB_SETCOLOR]= "fb_setcolor";
    names[SYS_FB_SETPIXEL]= "fb_setpixel";
    names[SYS_FB_FILLRECT]= "fb_fillrect";

    vga_writestring("\nSyscall Statistics:\n");
    vga_writestring("  Total: "); print_dec(stats.total_calls);
    vga_writestring("  Errors: "); print_dec(stats.errors); vga_writestring("\n");
    for (int i = 0; i < SYSCALL_MAX; i++) {
        if (!stats.calls_per_number[i]) continue;
        vga_writestring("  ["); print_dec((uint64_t)i); vga_writestring("] ");
        vga_writestring(names[i] ? names[i] : "?");
        vga_writestring(": "); print_dec(stats.calls_per_number[i]);
        vga_writestring("\n");
    }
}

struct syscall_stats syscall_get_stats(void) { return stats; }
