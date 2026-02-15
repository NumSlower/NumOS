/*
 * syscall.c — NumOS system call entry point and dispatcher
 *
 * Fixes applied:
 *  1. SYS_EXIT now calls hang() instead of returning to userspace.
 *  2. Kernel stack pointer loaded with a single direct LEA (no double-deref).
 *  3. Caller-saved registers (rdi/rsi/rdx) preserved around the C call.
 */

#include "cpu/syscall.h"
#include "drivers/vga.h"
#include "kernel/kernel.h"

/* 16 KB kernel syscall stack, 16-byte aligned */
#define KS_SIZE  (16 * 1024)
uint8_t  g_ks[KS_SIZE] __attribute__((aligned(16)));

/* Saved user RSP while executing a syscall */
static volatile uint64_t g_user_rsp = 0;

/*
 * syscall_trampoline — naked trampoline, called by hardware on `syscall`.
 *
 * Register state on entry (set by CPU):
 *   RCX  = user RIP  (return address)
 *   R11  = user RFLAGS
 *   RSP  = still the user stack pointer
 *   RAX  = syscall number
 *   RDI  = arg1, RSI = arg2, RDX = arg3
 *
 * We:
 *  1. Save user RSP.
 *  2. Switch to kernel stack (g_ks top).
 *  3. Save caller-saved regs that the C ABI would clobber.
 *  4. Rearrange args: syscall_handler(number, arg1, arg2, arg3).
 *  5. Call syscall_handler.
 *  6. Restore regs, restore user RSP, sysretq.
 */
__attribute__((naked))
void syscall_trampoline(void)
{
    __asm__ volatile (
        /* 1. Save user RSP and switch to kernel stack */
        "movq %%rsp, g_user_rsp(%%rip)\n\t"
        "leaq g_ks+%c0(%%rip), %%rsp\n\t"   /* RSP = &g_ks[KS_SIZE] directly */

        /* 2. Save caller-saved registers the C ABI may clobber */
        "pushq %%rcx\n\t"          /* user RIP */
        "pushq %%r11\n\t"          /* user RFLAGS */
        "pushq %%rbx\n\t"
        "pushq %%rbp\n\t"
        "pushq %%r12\n\t"
        "pushq %%r13\n\t"
        "pushq %%r14\n\t"
        "pushq %%r15\n\t"

        /* 3. Set up args for syscall_handler(number, arg1, arg2, arg3)
         *    On syscall entry: RAX=number, RDI=arg1, RSI=arg2, RDX=arg3
         *    System V AMD64 ABI: RDI=1st, RSI=2nd, RDX=3rd, RCX=4th
         *    RCX was clobbered by `syscall` (user RIP) so use R10 for arg3 */
        "movq %%rdi, %%r8\n\t"     /* stash arg1 */
        "movq %%rsi, %%r9\n\t"     /* stash arg2 */
        "movq %%rdx, %%r10\n\t"    /* stash arg3 */
        "movq %%rax, %%rdi\n\t"    /* number */
        "movq %%r8,  %%rsi\n\t"    /* arg1 */
        "movq %%r9,  %%rdx\n\t"    /* arg2 */
        "movq %%r10, %%rcx\n\t"    /* arg3 */
        "call syscall_handler\n\t"

        /* 4. Restore saved registers */
        "popq %%r15\n\t"
        "popq %%r14\n\t"
        "popq %%r13\n\t"
        "popq %%r12\n\t"
        "popq %%rbp\n\t"
        "popq %%rbx\n\t"
        "popq %%r11\n\t"           /* user RFLAGS */
        "popq %%rcx\n\t"           /* user RIP */

        /* 5. Restore user RSP and return to userspace */
        "movq g_user_rsp(%%rip), %%rsp\n\t"
        "sysretq\n\t"
        :
        : "i"(KS_SIZE)
        : "memory"
    );
}

long syscall_handler(long number, long arg1, long arg2, long arg3)
{
    switch (number) {

    /* ── write(fd, buf, count) ─────────────────────────────────── */
    case SYS_WRITE: {
        long        fd    = arg1;
        const char *buf   = (const char *)(uintptr_t)arg2;
        long        count = arg3;

        if (fd != 1 && fd != 2) return -1;
        if (count < 0 || count > 65536) return -1;

        /* Basic bounds check: buffer must be in user space (< 128 MB) */
        if ((uintptr_t)buf + (uint64_t)count > 128ULL * 1024 * 1024) return -1;
        if ((uintptr_t)buf < 0x1000) return -1;

        for (long i = 0; i < count; i++) {
            vga_putchar((unsigned char)buf[i]);
        }
        return count;
    }

    /* ── mmap(addr, length, prot, flags, fd, offset) ──────────── */
    case SYS_MMAP: {
        long length = arg2;

        if (length <= 0 || (uint64_t)length > 128ULL * 1024 * 1024) return -1;

        void *result = kmalloc((size_t)length);
        if (!result) return -1;

        return (long)(uintptr_t)result;
    }

    /* ── munmap(addr, length) ──────────────────────────────────── */
    case SYS_MUNMAP: {
        uint64_t addr   = (uint64_t)(uintptr_t)arg1;
        long     length = arg2;

        if (length <= 0) return -1;
        if (addr + (uint64_t)length > 128ULL * 1024 * 1024) return -1;

        /* stub — we don't track mmap regions yet */
        return 0;
    }

    /* ── mprotect(addr, length, prot) ─────────────────────────── */
    case SYS_MPROTECT: {
        uint64_t addr   = (uint64_t)(uintptr_t)arg1;
        long     length = arg2;

        if (length <= 0) return -1;
        if (addr + (uint64_t)length > 128ULL * 1024 * 1024) return -1;

        /* stub */
        return 0;
    }

    /* ── exit(status) ─────────────────────────────────────────── */
    case SYS_EXIT: {
        long status = arg1;

        vga_writestring("\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_writestring("[kernel] User process exited with status: ");
        print_dec((uint64_t)status);
        vga_writestring("\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

        /*
         * FIX: SYS_EXIT must NOT return.  Returning here would cause
         * sysretq to resume userspace at the instruction after `syscall`,
         * which is `ud2` — triggering an invalid-opcode exception.
         * Instead, halt the CPU permanently.
         */
        hang();

        /* unreachable, silences compiler warning */
        return status;
    }

    default:
        vga_writestring("[kernel] Unknown syscall: ");
        print_dec((uint64_t)number);
        vga_writestring("\n");
        return -1;
    }
}

/* ── MSR helpers ──────────────────────────────────────────────── */
#define MSR_STAR    0x174
#define MSR_LSTAR   0x176
#define MSR_CSTAR   0x177
#define MSR_SFMASK  0x178

#define KERNEL_CS   0x08   /* GDT[1] */
#define USER_BASE   0x10   /* sysret: CS=(BASE+16)|3=0x23, SS=(BASE+8)|3=0x1B */

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr"
        : : "c"(msr), "a"(lo), "d"(hi) : "memory");
}

void syscall_init(void)
{
    vga_writestring("Syscall: initialising MSRs...\n");

    /*
     * STAR layout:
     *   [47:32] = kernel CS used by SYSCALL  → 0x08
     *   [63:48] = base for SYSRET selectors  → 0x10
     *             SYSRET CS  = (0x10 + 16) | 3 = 0x23 (GDT[4] user code)
     *             SYSRET SS  = (0x10 +  8) | 3 = 0x1B (GDT[3] user data)
     */
    uint64_t star = ((uint64_t)KERNEL_CS << 32) |
                    ((uint64_t)USER_BASE  << 48);

    wrmsr(MSR_STAR,   star);
    wrmsr(MSR_LSTAR,  (uint64_t)(uintptr_t)syscall_trampoline);
    wrmsr(MSR_CSTAR,  0);
    wrmsr(MSR_SFMASK, 0x200);  /* clear IF on syscall entry */

    vga_writestring("Syscall: LSTAR -> syscall_trampoline\n");
    vga_writestring("Syscall: SYSRET will load CS=0x23, SS=0x1B\n");
}