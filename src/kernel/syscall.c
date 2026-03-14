/*
 * syscall.c - NumOS System Call Implementation
 *
 * Wires up the x86-64 SYSCALL/SYSRET mechanism via three MSRs:
 *
 *   STAR   (0xC0000081) - kernel/user segment selectors
 *   LSTAR  (0xC0000082) - 64-bit entry point (syscall_entry stub)
 *   SFMASK (0xC0000084) - RFLAGS bits cleared on SYSCALL entry (we clear IF)
 *
 * Also sets the SCE bit in EFER so the processor honours the SYSCALL
 * instruction.
 *
 * Syscall ABI (Linux x86-64 compatible):
 *   rax = syscall number (in) / return value (out)
 *   rdi = arg1, rsi = arg2, rdx = arg3, r10 = arg4
 *   Negative errno-style values indicate errors.
 */

#include "kernel/syscall.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "fs/fat32.h"
#include "cpu/gdt.h"
#include "kernel/scheduler.h"

/* =========================================================================
 * MSR addresses and bit definitions
 * ======================================================================= */

#define MSR_EFER    0xC0000080   /* Extended Feature Enable Register */
#define MSR_STAR    0xC0000081   /* Segment selectors for SYSCALL/SYSRET */
#define MSR_LSTAR   0xC0000082   /* 64-bit SYSCALL entry point */
#define MSR_SFMASK  0xC0000084   /* RFLAGS mask applied on SYSCALL */

#define EFER_SCE    (1UL << 0)   /* SYSCALL Enable bit in EFER */
#define SFMASK_IF   (1UL << 9)   /* Clear IF (bit 9) on syscall entry */

/* =========================================================================
 * External declarations
 * ======================================================================= */

/*
 * Defined in keyboard.c; consumes characters from the IRQ-filled ring buffer.
 * Declared here at file scope so sys_read() does not need an inline extern.
 */
extern char keyboard_getchar_buffered(void);

/* Defined in syscall_entry.asm */
extern void syscall_entry(void);

/* =========================================================================
 * Module state
 * ======================================================================= */

static struct syscall_stats stats;
static int syscall_initialised = 0;

/* =========================================================================
 * Low-level MSR helpers
 * ======================================================================= */

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

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * syscall_init - enable the SYSCALL instruction and configure the three MSRs.
 *
 * GDT layout (from gdt.c) that determines STAR values:
 *   0x00 - NULL
 *   0x08 - Kernel Code  (Ring 0)
 *   0x10 - Kernel Data  (Ring 0)
 *   0x18 - User Data    (Ring 3)   <- position 3 for SYSRET SS
 *   0x20 - User Code    (Ring 3)   <- position 4 for SYSRET CS
 *
 * SYSCALL:  CS = STAR[47:32]       = 0x08  (kernel code)
 *           SS = STAR[47:32] + 8   = 0x10  (kernel data)
 * SYSRETQ:  CS = STAR[63:48] + 16  = 0x20 | 3  (user code)
 *           SS = STAR[63:48] + 8   = 0x18 | 3  (user data)
 *
 * Therefore STAR[63:48] = GDT_KERNEL_DATA = 0x10.
 */
void syscall_init(void) {
    if (syscall_initialised) {
        vga_writestring("SYSCALL: Already initialised\n");
        return;
    }

    vga_writestring("SYSCALL: Initialising syscall subsystem...\n");
    memset(&stats, 0, sizeof(stats));

    /* 1. Enable SYSCALL in EFER */
    uint64_t efer = rdmsr(MSR_EFER);
    efer |= EFER_SCE;
    wrmsr(MSR_EFER, efer);

    /* 2. STAR: set kernel CS (bits 47:32) and user segment base (bits 63:48) */
    uint64_t star = 0;
    star |= ((uint64_t)GDT_KERNEL_CODE << 32);  /* bits 47:32 = 0x08 */
    star |= ((uint64_t)GDT_KERNEL_DATA << 48);  /* bits 63:48 = 0x10 */
    wrmsr(MSR_STAR, star);

    /* 3. LSTAR: 64-bit syscall entry point (assembly stub) */
    wrmsr(MSR_LSTAR, (uint64_t)syscall_entry);

    /* 4. SFMASK: clear IF on syscall entry so we run with interrupts disabled */
    wrmsr(MSR_SFMASK, SFMASK_IF);

    syscall_initialised = 1;

    vga_writestring("SYSCALL: EFER.SCE enabled\n");
    vga_writestring("SYSCALL: LSTAR  = 0x"); print_hex((uint64_t)syscall_entry); vga_writestring("\n");
    vga_writestring("SYSCALL: STAR   = 0x"); print_hex(star);                    vga_writestring("\n");
    vga_writestring("SYSCALL: SFMASK = IF cleared\n");
    vga_writestring("SYSCALL: Syscall subsystem ready\n");
}

/* =========================================================================
 * Individual syscall implementations
 * ======================================================================= */

/*
 * sys_write - write count bytes from buf to file descriptor fd.
 * Only FD_STDOUT (1) and FD_STDERR (2) are supported; both write to VGA.
 */
int64_t sys_write(int fd, const void *buf, size_t count) {
    if (!buf)    return SYSCALL_EFAULT;
    if (!count)  return 0;
    if (fd != FD_STDOUT && fd != FD_STDERR) return SYSCALL_EBADF;

    const char *p = (const char *)buf;
    for (size_t i = 0; i < count; i++) {
        vga_putchar(p[i]);
    }
    return (int64_t)count;
}

/*
 * sys_read - read up to count bytes from fd into buf.
 * Only FD_STDIN (0) is supported; reads from the keyboard IRQ buffer.
 * Blocks until a newline or count bytes are received.
 */
int64_t sys_read(int fd, void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;
    if (fd != FD_STDIN) return SYSCALL_EBADF;

    char  *p   = (char *)buf;
    size_t got = 0;

    while (got < count) {
        char c = keyboard_getchar_buffered();
        p[got++] = c;
        if (c == '\n') break;
    }

    return (int64_t)got;
}

/*
 * sys_open - open a file on the FAT32 volume.
 */
int64_t sys_open(const char *path, int flags, int mode) {
    (void)mode;  /* mode is unused; FAT32 does not support POSIX permissions */
    if (!path) return SYSCALL_EFAULT;
    return fat32_open(path, flags);
}

/*
 * sys_close - close an open file descriptor.
 */
int64_t sys_close(int fd) {
    return fat32_close(fd);
}

/*
 * sys_exit - terminate the calling process with exit_code.
 * Calls into the scheduler; never returns.
 */
int64_t sys_exit(int status) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n[SYSCALL] sys_exit(");
    print_dec((uint64_t)(uint32_t)status);
    vga_writestring(") called\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    process_exit(status);

    while (1) __asm__ volatile("hlt");  /* unreachable */
    return 0;
}

/*
 * sys_getpid - return the PID of the calling process.
 */
int64_t sys_getpid(void) {
    struct process *proc = scheduler_current();
    return proc ? (int64_t)proc->pid : 1;
}

/*
 * sys_sleep_ms - block the calling process for at least ms milliseconds.
 */
int64_t sys_sleep_ms(uint64_t ms) {
    uint64_t wake = timer_get_uptime_ms() + ms;
    process_sleep_until(wake);
    return 0;
}

/*
 * sys_uptime_ms - return the number of milliseconds since boot.
 */
int64_t sys_uptime_ms(void) {
    return (int64_t)timer_get_uptime_ms();
}

/*
 * sys_puts - write a null-terminated string to VGA followed by a newline.
 */
int64_t sys_puts(const char *str) {
    if (!str) return SYSCALL_EFAULT;
    vga_writestring(str);
    vga_putchar('\n');
    return 0;
}

/* =========================================================================
 * Central dispatcher
 * ======================================================================= */

/*
 * syscall_dispatch - C-level dispatcher called from syscall_entry.asm.
 *
 * Interrupts are re-enabled for the duration of the call (SFMASK cleared
 * them on entry; the assembly stub re-disables them before SYSRETQ).
 * The return value is written back into regs->rax so the stub can pop it.
 */
int64_t syscall_dispatch(struct syscall_regs *regs) {
    uint64_t nr  = regs->rax;
    int64_t  ret = SYSCALL_ENOSYS;

    stats.total_calls++;
    if (nr < SYSCALL_MAX) {
        stats.calls_per_number[nr]++;
    } else {
        stats.unknown_calls++;
    }

    /* Re-enable interrupts while processing the syscall */
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
            vga_writestring("[SYSCALL] Unknown syscall: ");
            print_dec(nr);
            vga_writestring("\n");
            ret = SYSCALL_ENOSYS;
            break;
    }

    /* Disable interrupts again before SYSRETQ */
    __asm__ volatile("cli");

    regs->rax = (uint64_t)ret;
    return ret;
}

/* =========================================================================
 * Diagnostics
 * ======================================================================= */

/*
 * syscall_print_stats - write a formatted syscall usage summary to VGA.
 */
void syscall_print_stats(void) {
    /* Names for known syscall numbers; others are printed as "?" */
    static const char *names[SYSCALL_MAX];
    /* Zero the array before assigning known entries */
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

    vga_writestring("\nSyscall Statistics:\n");
    vga_writestring("  Total calls:   "); print_dec(stats.total_calls);   vga_writestring("\n");
    vga_writestring("  Unknown calls: "); print_dec(stats.unknown_calls);  vga_writestring("\n");
    vga_writestring("  Errors:        "); print_dec(stats.errors);         vga_writestring("\n");
    vga_writestring("\nPer-syscall breakdown:\n");

    for (int i = 0; i < SYSCALL_MAX; i++) {
        if (stats.calls_per_number[i] == 0) continue;
        vga_writestring("  [");
        print_dec((uint64_t)i);
        vga_writestring("] ");
        vga_writestring(names[i] ? names[i] : "?");
        vga_writestring(": ");
        print_dec(stats.calls_per_number[i]);
        vga_writestring(" calls\n");
    }
}

/*
 * syscall_get_stats - return a snapshot of the current statistics.
 */
struct syscall_stats syscall_get_stats(void) {
    return stats;
}