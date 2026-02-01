/*
 * syscall.c — NumOS system call entry point and dispatcher
 * UPDATED: MSR_STAR now uses correct base selector for swapped GDT layout
 */

#include "cpu/syscall.h"
#include "drivers/vga.h"
#include "kernel/kernel.h"

#define KS_SIZE  (16 * 1024)

static char g_ks[KS_SIZE] __attribute__((aligned(16)));
static volatile uint64_t g_ks_top = (uint64_t)((uint8_t *)g_ks + KS_SIZE);
static volatile uint64_t g_user_rsp = 0;

__attribute__((naked))
void syscall_trampoline(void)
{
    __asm__ volatile (
        "movq %rsp, g_user_rsp(%rip)\n\t"
        "leaq g_ks_top(%rip), %rsp\n\t"
        "movq (%rsp), %rsp\n\t"
        "pushq %rcx\n\t"
        "pushq %r11\n\t"
        "pushq %rbx\n\t"
        "pushq %rbp\n\t"
        "pushq %r12\n\t"
        "pushq %r13\n\t"
        "pushq %r14\n\t"
        "pushq %r15\n\t"
        "movq %rdi, %r8\n\t"
        "movq %rsi, %r9\n\t"
        "movq %rdx, %r10\n\t"
        "movq %rax, %rdi\n\t"
        "movq %r8,  %rsi\n\t"
        "movq %r9,  %rdx\n\t"
        "movq %r10, %rcx\n\t"
        "call syscall_handler\n\t"
        "popq %r15\n\t"
        "popq %r14\n\t"
        "popq %r13\n\t"
        "popq %r12\n\t"
        "popq %rbp\n\t"
        "popq %rbx\n\t"
        "popq %r11\n\t"
        "popq %rcx\n\t"
        "movq g_user_rsp(%rip), %rsp\n\t"
        "sysretq\n\t"
    );
}

long syscall_handler(long number, long arg1, long arg2, long arg3)
{
    switch (number) {

    case SYS_WRITE: {
        long        fd    = arg1;
        const char *buf   = (const char *)arg2;
        long        count = arg3;

        if (fd != 1) return -1;
        if (count < 0) return -1;
        if ((uint64_t)buf + (uint64_t)count > 128ULL * 1024 * 1024) return -1;

        for (long i = 0; i < count; i++) {
            vga_putchar((unsigned char)buf[i]);
        }
        return count;
    }

    case SYS_EXIT: {
        long status = arg1;

        vga_writestring("\n[kernel] User process exited with code ");
        print_dec((uint64_t)status);
        vga_writestring("\n");

        __asm__ volatile (
            "leaq g_ks_top(%rip), %rsp\n\t"
            "movq (%rsp), %rsp\n\t"
            "1:\n\t"
            "hlt\n\t"
            "jmp 1b\n\t"
        );
        return 0;
    }

    default:
        vga_writestring("[kernel] Unknown syscall ");
        print_dec((uint64_t)number);
        vga_writestring("\n");
        return -1;
    }
}

/* MSR_STAR setup - CRITICAL FIX!
 * 
 * With swapped GDT layout:
 *   GDT[3] = User Data (selector 0x18, or 0x1B with RPL=3)
 *   GDT[4] = User Code (selector 0x20, or 0x23 with RPL=3)
 *
 * sysret loads:
 *   SS = (STAR[63:48] + 8) | 3
 *   CS = (STAR[63:48] + 16) | 3
 *
 * We want SS=0x1B, CS=0x23, so:
 *   STAR[63:48] + 8 = 0x18  → STAR[63:48] = 0x10
 *   STAR[63:48] + 16 = 0x20 → STAR[63:48] = 0x10 ✓
 *
 * Therefore: STAR[63:48] = 0x10
 */

#define MSR_STAR     0x174
#define MSR_LSTAR    0x176
#define MSR_CSTAR    0x177
#define MSR_SFMASK   0x178

#define KERNEL_CS  0x08   /* GDT index 1 */
#define USER_BASE  0x10   /* Base for user segments (see calculation above) */

static inline void wrmsr(uint32_t msr, uint64_t value)
{
    uint32_t lo = (uint32_t)(value & 0xFFFFFFFFU);
    uint32_t hi = (uint32_t)(value >> 32);
    __asm__ volatile ("wrmsr"
        : : "c" (msr), "a" (lo), "d" (hi) : "memory");
}

void syscall_init(void)
{
    vga_writestring("Syscall: programming MSRs...\n");

    /* CRITICAL: USER_BASE = 0x10 for swapped GDT layout */
    uint64_t star = ((uint64_t)KERNEL_CS << 32) |
                    ((uint64_t)USER_BASE  << 48);
                    
    vga_writestring("Syscall:   STAR = ");
    print_hex(star);
    vga_writestring("\n");
    vga_writestring("Syscall:   sysret will load CS=(0x");
    print_hex(USER_BASE + 16);
    vga_writestring("|3)=0x");
    print_hex((USER_BASE + 16) | 3);
    vga_writestring("\n");
    vga_writestring("Syscall:   sysret will load SS=(0x");
    print_hex(USER_BASE + 8);
    vga_writestring("|3)=0x");
    print_hex((USER_BASE + 8) | 3);
    vga_writestring("\n");
    
    wrmsr(MSR_STAR,   star);
    wrmsr(MSR_LSTAR,  (uint64_t)(uintptr_t)syscall_trampoline);
    wrmsr(MSR_CSTAR,  0);
    wrmsr(MSR_SFMASK, 0x200);

    vga_writestring("Syscall: MSRs programmed\n");
}