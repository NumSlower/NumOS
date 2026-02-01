/*
 * syscall.h — NumOS user-space system call interface
 *
 * This header is the user-space mirror of cpu/syscall.h in the kernel tree.
 * It defines the same syscall numbers and provides inline C wrappers that
 * translate C calling convention into the register layout the kernel's
 * syscall trampoline expects.
 *
 * Register convention (x86_64, identical to Linux):
 *   RAX  ← syscall number (input) / return value (output)
 *   RDI  ← argument 1
 *   RSI  ← argument 2
 *   RDX  ← argument 3
 *   RCX  — clobbered by the syscall instruction (saves user RIP)
 *   R11  — clobbered by the syscall instruction (saves user RFLAGS)
 *
 * Constraints on each wrapper:
 *   - "=a" (ret)       : RAX is written by the kernel before sysret.
 *   - "0" (number)     : ties the input RAX to output constraint 0.
 *   - "D" (arg1)       : RDI
 *   - "S" (arg2)       : RSI
 *   - "d" (arg3)       : RDX
 *   - clobber "rcx","r11","memory"
 *       rcx and r11 are destroyed by the hardware.
 *       "memory" tells the compiler that the kernel may read or write
 *       arbitrary memory (e.g. SYS_WRITE reads the buffer), so it must
 *       flush any pending stores before the syscall and must not cache
 *       loads across it.
 *
 * Why inline and not extern?
 *   Each syscall is a single instruction.  Wrapping it in an extern
 *   function adds a CALL + RET pair plus a stack frame.  Inlining
 *   eliminates all of that overhead.  The compiler can also see through
 *   the wrapper and optimise register allocation around it.
 */

#ifndef USER_SYSCALL_H
#define USER_SYSCALL_H

#include <stdint.h>

/* ─── Syscall numbers ────────────────────────────────────────────
 * Must match the kernel's cpu/syscall.h exactly.  We use Linux
 * x86_64 numbers for familiarity and future compatibility.
 * ─────────────────────────────────────────────────────────────── */
#define SYS_WRITE   1     /* write(fd, buf, count) → bytes written  */
#define SYS_EXIT    60    /* exit(status)          → does not return */

/* ─── write(fd, buf, count) ──────────────────────────────────────
 * fd    : file descriptor.  Only 1 (stdout → VGA) is supported.
 * buf   : pointer to data to write.  Must be within the kernel's
 *         identity-mapped window (< 128 MB).
 * count : number of bytes to write.  Must be ≥ 0.
 *
 * Returns: number of bytes written, or -1 on error.
 *
 * The kernel validates fd == 1, count ≥ 0, and that [buf, buf+count)
 * is entirely within the first 128 MB before touching the buffer.
 * ─────────────────────────────────────────────────────────────── */
static inline long sys_write(long fd, const char *buf, long count)
{
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)                          /* output: RAX */
        : "0"  (SYS_WRITE),                   /* input:  RAX = syscall number */
          "D"  (fd),                           /*         RDI = arg1           */
          "S"  (buf),                          /*         RSI = arg2           */
          "d"  (count)                         /*         RDX = arg3           */
        : "rcx", "r11", "memory"              /* clobbers                     */
    );
    return ret;
}

/* ─── exit(status) ───────────────────────────────────────────────
 * Terminates the current process.  The kernel reloads its own stack
 * and spins in hlt; this function does not return.
 *
 * status : exit code printed by the kernel before halting.
 *
 * __attribute__((noreturn)) tells the compiler that code after a call
 * to sys_exit() is unreachable.  This lets it omit dead stores and
 * elide the return path in callers.
 * ─────────────────────────────────────────────────────────────── */
static inline void __attribute__((noreturn)) sys_exit(long status)
{
    __asm__ volatile (
        "syscall"
        :                                     /* no outputs */
        : "a" (SYS_EXIT),                     /* RAX = syscall number */
          "D" (status)                        /* RDI = arg1           */
        : "rcx", "r11", "memory"
    );

    /* Unreachable.  The compiler needs this to satisfy noreturn. */
    while (1) {
        __asm__ volatile ("ud2");
    }
}

#endif /* USER_SYSCALL_H */