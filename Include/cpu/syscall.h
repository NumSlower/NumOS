/*
 * syscall.h — NumOS system call interface (kernel side)
 *
 * Syscall convention (x86_64, Linux-compatible numbering):
 *   RAX  — syscall number on entry; return value on exit
 *   RDI  — argument 1
 *   RSI  — argument 2
 *   RDX  — argument 3
 *   RCX  — clobbered by hardware (saves user RIP)
 *   R11  — clobbered by hardware (saves user RFLAGS)
 *
 * Mechanism:
 *   User executes `syscall`.  The CPU loads the target from MSR IA32_LSTAR
 *   and jumps there at CPL 0.  Our naked trampoline switches to a static
 *   kernel stack, saves context, calls syscall_handler, restores context,
 *   and returns via sysretq.
 *
 * IMPORTANT — SYS_EXIT:
 *   syscall_handler() for SYS_EXIT calls hang() and never returns.
 *   It does NOT return to the trampoline; returning would cause sysretq to
 *   resume userspace at the `ud2` instruction that follows `syscall` in
 *   crt0.S, triggering an invalid-opcode exception.
 */

#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/base.h"

/* ─── Syscall number table ─────────────────────────────────────── */
#define SYS_WRITE    1    /* write(fd, buf, count)                 */
#define SYS_MMAP     9    /* mmap(addr, len, prot, flags, fd, off) */
#define SYS_MPROTECT 10   /* mprotect(addr, len, prot)             */
#define SYS_MUNMAP   11   /* munmap(addr, len)                     */
#define SYS_EXIT     60   /* exit(status) — does not return        */

/* ─── Kernel API ───────────────────────────────────────────────── */

/**
 * syscall_init — program IA32_LSTAR / STAR / SFMASK MSRs.
 *   Must be called after GDT and IDT are fully set up.
 */
void syscall_init(void);

/**
 * syscall_handler — C dispatcher invoked by the naked trampoline.
 *   Parameters map directly to the register convention.
 *   Returns the syscall return value (in RAX on sysretq).
 *   For SYS_EXIT this function calls hang() and never returns.
 */
long syscall_handler(long number, long arg1, long arg2, long arg3);

#endif /* SYSCALL_H */