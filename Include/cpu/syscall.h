/*
 * syscall.h — NumOS system call interface (kernel side)
 *
 * Syscall convention (x86_64, Linux-compatible numbering):
 *   RAX  — syscall number on entry; return value on exit
 *   RDI  — argument 1
 *   RSI  — argument 2
 *   RDX  — argument 3
 *   RCX  — clobbered (hardware saves user RIP here)
 *   R11  — clobbered (hardware saves user RFLAGS here)
 *
 * Mechanism:
 *   User executes `syscall`.  The processor loads the target from
 *   MSR IA32_LSTAR and jumps there at CPL 0.  Our assembly trampoline
 *   swaps to the kernel stack, saves context, calls syscall_handler,
 *   restores context, and returns via sysret.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/base.h"

/* ─── Syscall number table ───────────────────────────────────────
 * User space and kernel must agree on every number here.
 * We use Linux x86_64 numbers for familiarity.
 * ─────────────────────────────────────────────────────────────── */
#define SYS_WRITE   1     /* write(fd, buf, count) → bytes written  */
#define SYS_MMAP    9     /* mmap(addr, length, prot, flags, fd, offset) */
#define SYS_MUNMAP  11    /* munmap(addr, length) → 0 on success */
#define SYS_MPROTECT 10   /* mprotect(addr, length, prot) → 0 on success */
#define SYS_EXIT    60    /* exit(status)          → does not return */

/* ─── Kernel API ─────────────────────────────────────────────────
 * syscall_init   — program IA32_LSTAR/STAR/SFMASK MSRs.
 *                  Must be called after GDT + IDT are set up.
 * syscall_handler — C dispatcher invoked by the assembly trampoline.
 *                  Parameters map directly to the register convention.
 * ─────────────────────────────────────────────────────────────── */
void syscall_init(void);

long syscall_handler(long number, long arg1, long arg2, long arg3);

#endif /* SYSCALL_H */
