#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/base.h"

/* =========================================================================
 * NumOS Syscall ABI  (x86-64, Linux-compatible numbering for easy porting)
 *
 * Calling convention (same as Linux x86-64 System V):
 *   rax  = syscall number (in) / return value (out)
 *   rdi  = arg1
 *   rsi  = arg2
 *   rdx  = arg3
 *   r10  = arg4
 *   r8   = arg5
 *   r9   = arg6
 *
 * Preserved by kernel across syscall: rbx, rbp, r12-r15, rsp
 * Clobbered (caller-saved):           rcx (holds RIP on return), r11 (RFLAGS)
 * ========================================================================= */

/* ---- Syscall numbers ---------------------------------------------------- */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_GETPID      39
#define SYS_EXIT        60
#define SYS_SLEEP_MS    35   /* NumOS extension: sleep N milliseconds        */
#define SYS_UPTIME_MS   96   /* NumOS extension: return uptime in ms         */
#define SYS_PUTS        200  /* NumOS extension: write null-terminated string */

/* Maximum syscall number tracked in dispatch table */
#define SYSCALL_MAX     256

/* ---- Well-known file descriptors ---------------------------------------- */
#define FD_STDIN    0
#define FD_STDOUT   1
#define FD_STDERR   2

/* ---- Return-value conventions ------------------------------------------- */
#define SYSCALL_SUCCESS   0
#define SYSCALL_EBADF   (-9)
#define SYSCALL_ENOMEM  (-12)
#define SYSCALL_EFAULT  (-14)
#define SYSCALL_EINVAL  (-22)
#define SYSCALL_ENOSYS  (-38)  /* Function not implemented */

/* ---- Saved CPU state at syscall entry ------------------------------------ */
struct syscall_regs {
    uint64_t rax;   /* syscall number / return value */
    uint64_t rdi;   /* arg1 */
    uint64_t rsi;   /* arg2 */
    uint64_t rdx;   /* arg3 */
    uint64_t r10;   /* arg4 */
    uint64_t r8;    /* arg5 */
    uint64_t r9;    /* arg6 */
    uint64_t rcx;   /* user RIP (saved by syscall instruction) */
    uint64_t r11;   /* user RFLAGS (saved by syscall instruction) */
    uint64_t rbx;   /* callee-saved; preserved unchanged */
    uint64_t rbp;   /* callee-saved; preserved unchanged */
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;   /* user stack pointer */
};

/* ---- Syscall statistics -------------------------------------------------- */
struct syscall_stats {
    uint64_t total_calls;
    uint64_t calls_per_number[SYSCALL_MAX];
    uint64_t unknown_calls;
    uint64_t errors;
};

/* ---- Function prototypes ------------------------------------------------- */

/* Initialise the SYSCALL/SYSRET mechanism via MSR writes */
void syscall_init(void);

/* C-level dispatcher – called from the assembly syscall entry stub */
int64_t syscall_dispatch(struct syscall_regs *regs);

/* Individual syscall implementations */
int64_t sys_read   (int fd, void *buf, size_t count);
int64_t sys_write  (int fd, const void *buf, size_t count);
int64_t sys_open   (const char *path, int flags, int mode);
int64_t sys_close  (int fd);
int64_t sys_exit   (int status);
int64_t sys_getpid (void);
int64_t sys_sleep_ms(uint64_t ms);
int64_t sys_uptime_ms(void);
int64_t sys_puts   (const char *str);

/* Debug / diagnostics */
void syscall_print_stats(void);
struct syscall_stats syscall_get_stats(void);

/* Assembly entry point declared here so the linker can find it */
extern void syscall_entry(void);

#endif /* SYSCALL_H */