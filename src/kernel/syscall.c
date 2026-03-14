/*
 * syscall.c - NumOS System Call Implementation
 *
 * Wires up the x86-64 SYSCALL/SYSRET mechanism via three MSRs:
 *
 *   STAR   (0xC0000081) – segment selectors for kernel/user transitions
 *   LSTAR  (0xC0000082) – 64-bit entry point (our syscall_entry stub)
 *   SFMASK (0xC0000084) – RFLAGS bits to clear on SYSCALL (we clear IF)
 *
 * Also sets the SCE bit in EFER so the processor honours SYSCALL at all.
 *
 * Syscall ABI: rax=number, rdi=arg1, rsi=arg2, rdx=arg3, r10=arg4.
 * Return value in rax; negative errno-style values indicate errors.
 */

#include "kernel/syscall.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "fs/fat32.h"
#include "cpu/gdt.h"
#include "kernel/scheduler.h"

/* -------------------------------------------------------------------------
 * MSR addresses
 * ---------------------------------------------------------------------- */
#define MSR_EFER        0xC0000080
#define MSR_STAR        0xC0000081
#define MSR_LSTAR       0xC0000082
#define MSR_SFMASK      0xC0000084

/* EFER bits */
#define EFER_SCE        (1UL << 0)   /* SYSCALL Enable */

/* SFMASK: clear IF (bit 9) on syscall entry so we run with interrupts off */
#define SFMASK_IF       (1UL << 9)

/* -------------------------------------------------------------------------
 * Global state
 * ---------------------------------------------------------------------- */
static struct syscall_stats stats;
static int syscall_initialised = 0;

/* -------------------------------------------------------------------------
 * Low-level MSR helpers
 * ---------------------------------------------------------------------- */
static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}

static inline void wrmsr(uint32_t msr, uint64_t value) {
    uint32_t lo = (uint32_t)value;
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile("wrmsr" : : "a"(lo), "d"(hi), "c"(msr) : "memory");
}

/* -------------------------------------------------------------------------
 * syscall_init – enable SYSCALL and point LSTAR at our entry stub
 * ---------------------------------------------------------------------- */
void syscall_init(void) {
    if (syscall_initialised) {
        vga_writestring("SYSCALL: Already initialised\n");
        return;
    }

    vga_writestring("SYSCALL: Initialising syscall subsystem...\n");

    /* Zero statistics */
    memset(&stats, 0, sizeof(stats));

    /* 1. Enable SYSCALL in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    /* 2. Set up STAR:
     *    Bits [47:32] = kernel CS selector (used for SYSCALL)
     *    Bits [63:48] = user CS - 16 selector base (used for SYSRET)
     *
     *    Our GDT layout (from gdt.c):
     *      0x00 – NULL
     *      0x08 – Kernel Code   (Ring 0)
     *      0x10 – Kernel Data   (Ring 0)
     *      0x18 – User Data     (Ring 3)   ← SWAPPED for sysret compat
     *      0x20 – User Code     (Ring 3)
     *
     *    For SYSCALL:  CS = STAR[47:32]        = 0x08 (kernel code)
     *                  SS = STAR[47:32] + 8    = 0x10 (kernel data)
     *    For SYSRETQ:  CS = STAR[63:48] + 16   = 0x20 | 3 (user code)
     *                  SS = STAR[63:48] + 8    = 0x18 | 3 (user data)
     *
     *    So STAR[63:48] must be 0x10 (user data selector - 8):
     *      0x10 + 8  = 0x18 → SS (user data, matches entry 3)
     *      0x10 + 16 = 0x20 → CS (user code, matches entry 4)
     */
    uint64_t star = 0;
    star |= ((uint64_t)GDT_KERNEL_CODE << 32);   /* bits 47:32 = 0x08 */
    star |= ((uint64_t)GDT_KERNEL_DATA << 48);   /* bits 63:48 = 0x10 */
    wrmsr(MSR_STAR, star);

    /* 3. LSTAR – 64-bit syscall entry point */
    extern void syscall_entry(void);
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* 4. SFMASK – clear IF on syscall entry (run with interrupts disabled) */
    wrmsr(MSR_SFMASK, SFMASK_IF);

    syscall_initialised = 1;

    vga_writestring("SYSCALL: EFER.SCE enabled\n");
    vga_writestring("SYSCALL: LSTAR  = 0x");
    print_hex((uint64_t)syscall_entry);
    vga_writestring("\n");
    vga_writestring("SYSCALL: STAR   = 0x");
    print_hex(star);
    vga_writestring("\n");
    vga_writestring("SYSCALL: SFMASK = IF cleared\n");
    vga_writestring("SYSCALL: Syscall subsystem ready\n");
}

/* =========================================================================
 * Individual syscall handlers
 * ======================================================================= */

/* ---- sys_write ---------------------------------------------------------- */
int64_t sys_write(int fd, const void *buf, size_t count) {
    if (!buf) return SYSCALL_EFAULT;
    if (count == 0) return 0;

    /* Only stdout/stderr are supported for now; both go to VGA */
    if (fd != FD_STDOUT && fd != FD_STDERR) return SYSCALL_EBADF;

    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        vga_putchar(p[i]);
    }
    return (int64_t)count;
}

/* ---- sys_read ----------------------------------------------------------- */
int64_t sys_read(int fd, void *buf, size_t count) {
    if (!buf) return SYSCALL_EFAULT;
    if (count == 0) return 0;
    if (fd != FD_STDIN) return SYSCALL_EBADF;
 
    char *p = (char *)buf;
    size_t got = 0;
    while (got < count) {
        /* Use the IRQ-buffer version - does NOT call keyboard_handler() */
        extern char keyboard_getchar_buffered(void);
        char c = keyboard_getchar_buffered();
        p[got++] = c;
        if (c == '\n') break;
    }
    return (int64_t)got;
}

/* ---- sys_open ----------------------------------------------------------- */
int64_t sys_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path) return SYSCALL_EFAULT;
    return fat32_open(path, flags);
}

/* ---- sys_close ---------------------------------------------------------- */
int64_t sys_close(int fd) {
    return fat32_close(fd);
}

/* ---- sys_exit ----------------------------------------------------------- */
int64_t sys_exit(int status) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n[SYSCALL] sys_exit(");
    print_dec((uint64_t)(uint32_t)status);
    vga_writestring(") called\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* Properly terminate the current process via the scheduler */
    process_exit(status);

    /* Never reached */
    while (1) __asm__ volatile("hlt");
    return 0;
}

/* ---- sys_getpid --------------------------------------------------------- */
int64_t sys_getpid(void) {
    /* Single-process kernel: always PID 1 */
    return 1;
}

/* ---- sys_sleep_ms ------------------------------------------------------- */
int64_t sys_sleep_ms(uint64_t ms) {
    uint64_t wake = timer_get_uptime_ms() + ms;
    process_sleep_until(wake);
    return 0;
}

/* ---- sys_uptime_ms ------------------------------------------------------ */
int64_t sys_uptime_ms(void) {
    return (int64_t)timer_get_uptime_ms();
}

/* ---- sys_puts ----------------------------------------------------------- */
int64_t sys_puts(const char *str) {
    if (!str) return SYSCALL_EFAULT;
    vga_writestring(str);
    vga_putchar('\n');
    return 0;
}

/* =========================================================================
 * syscall_dispatch – central C dispatcher called from syscall_entry.asm
 * ======================================================================= */
int64_t syscall_dispatch(struct syscall_regs *regs) {
    uint64_t nr  = regs->rax;
    int64_t  ret = SYSCALL_ENOSYS;

    /* Update statistics */
    stats.total_calls++;
    if (nr < SYSCALL_MAX) {
        stats.calls_per_number[nr]++;
    } else {
        stats.unknown_calls++;
    }

    /* Re-enable interrupts while we process the syscall.
     * (SFMASK cleared IF; we're on the kernel stack now so it's safe.) */
    __asm__ volatile("sti");

    switch ((int)nr) {
        case SYS_READ:
            ret = sys_read((int)regs->rdi,
                           (void *)regs->rsi,
                           (size_t)regs->rdx);
            break;

        case SYS_WRITE:
            ret = sys_write((int)regs->rdi,
                            (const void *)regs->rsi,
                            (size_t)regs->rdx);
            break;

        case SYS_OPEN:
            ret = sys_open((const char *)regs->rdi,
                           (int)regs->rsi,
                           (int)regs->rdx);
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
            ret = sys_puts((const char *)regs->rdi);
            break;

        default:
            stats.errors++;
            ret = SYSCALL_ENOSYS;
            vga_writestring("[SYSCALL] Unknown syscall: ");
            print_dec(nr);
            vga_writestring("\n");
            break;
    }

    /* Disable interrupts again before SYSRETQ */
    __asm__ volatile("cli");

    /* Write return value back into the saved rax slot so the
     * assembly stub can pop it into rax before SYSRETQ.       */
    regs->rax = (uint64_t)ret;
    return ret;
}

/* =========================================================================
 * Diagnostics
 * ======================================================================= */
void syscall_print_stats(void) {
    vga_writestring("\nSyscall Statistics:\n");
    vga_writestring("  Total calls:   ");
    print_dec(stats.total_calls);
    vga_writestring("\n  Unknown calls: ");
    print_dec(stats.unknown_calls);
    vga_writestring("\n  Errors:        ");
    print_dec(stats.errors);
    vga_writestring("\n\nPer-syscall breakdown:\n");

    const char *names[SYSCALL_MAX];
    /* zero all names first */
    for (int i = 0; i < SYSCALL_MAX; i++) names[i] = NULL;
    names[SYS_READ]      = "read";
    names[SYS_WRITE]     = "write";
    names[SYS_OPEN]      = "open";
    names[SYS_CLOSE]     = "close";
    names[SYS_EXIT]      = "exit";
    names[SYS_GETPID]    = "getpid";
    names[SYS_SLEEP_MS]  = "sleep_ms";
    names[SYS_UPTIME_MS] = "uptime_ms";
    names[SYS_PUTS]      = "puts";

    for (int i = 0; i < SYSCALL_MAX; i++) {
        if (stats.calls_per_number[i] > 0) {
            vga_writestring("  [");
            print_dec((uint64_t)i);
            vga_writestring("] ");
            vga_writestring(names[i] ? names[i] : "?");
            vga_writestring(": ");
            print_dec(stats.calls_per_number[i]);
            vga_writestring(" calls\n");
        }
    }
}

struct syscall_stats syscall_get_stats(void) {
    return stats;
}
