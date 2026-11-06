/* NumOS — syscall.h — Documented system-call table (Phase 10)
 *
 * Entry: int 0x80 (gate DPL 3). Arguments follow the System V kernel
 * convention: nr in RAX, then RDI, RSI, RDX, R10, R8, R9. Return value in
 * RAX; negative errno on failure. All pointers passed from Ring 3 must lie
 * inside the user region [USER_VA_BASE, USER_VA_LIMIT); the dispatcher
 * validates and copies before touching them.
 *
 *  NR  Name      Prototype                              Notes
 *  --  --------  ------------------------------------  ------------------------------
 *  0   read      int read(int fd, void* buf, u32 len)  fd 0: returns 0 (EOF);
 *                                                          fds >= 3: VFS read
 *  1   write     int write(int fd, const void* buf,    fd 1/2: serial console;
 *                u32 len)                             fds >= 3: VFS write
 *  2   open      int open(const char* path, int flags) VFS_O_* flags; returns fd
 *  3   close     int close(int fd)
 *  4   exit      noreturn exit(int code)               marks zombie, logs, yields
 *  5   getpid    int getpid(void)
 *  6   sbrk      void* sbrk(int64_t delta)             grows user heap; returns
 *                old break, or (void*)-1 on failure
 *  7   yield     int yield(void)                       scheduler round-robin step
 *  8   uptime    u64 uptime(void)                      PIT ticks since boot
 *  9   exec      int exec(const char* path,            loads an ELF64 from VFS as
 *                  const char** argv,                 a child of the caller (ppid
 *                  const char** envp)                 = caller pid); argv/envp are
 *                                                     copied into the child's user
 *                                                     stack (SysV layout at entry:
 *                                                     rsp -> argc, argv[], NULL,
 *                                                     envp[], NULL); returns child
 *                                                     pid or negative errno
 * 10   wait      int wait(int pid)                     blocks (yield loop) until the
 *                                                     child is a zombie; reaps it
 *                                                     and returns its exit code
 * 11   sleep     int sleep(u32 ms)                     yields in a loop until the
 *                                                     deadline passes
 * 12   ps        int ps(void* ubuf, u32 max)           fills user buffer with
 *                                                     "pid ppid state name" lines;
 *                                                     returns byte count
 * 13   list      int list(const char* path,            fills user buffer with
 *                  void* ubuf, u32 max)               "name DIR|FILE size" lines
 * 14   stat      int stat(const char* path,            fills user buffer with
 *                  void* ubuf, u32 max)               "name DIR|FILE size\n"
 * 15   unlink    int unlink(const char* path)          VFS unlink
 */
#ifndef NUMOS_KERNEL_SYSCALL_H
#define NUMOS_KERNEL_SYSCALL_H

#include "libs/types.h"
#include "cpu/isr.h"

#define SYSCALL_NR 0x80

#define SYS_READ   0
#define SYS_WRITE  1
#define SYS_OPEN   2
#define SYS_CLOSE  3
#define SYS_EXIT   4
#define SYS_GETPID 5
#define SYS_SBRK   6
#define SYS_YIELD  7
#define SYS_UPTIME 8
#define SYS_EXEC   9
#define SYS_WAIT   10
#define SYS_SLEEP  11
#define SYS_PS     12
#define SYS_LIST   13
#define SYS_STAT   14
#define SYS_UNLINK 15

#define SYS_EINVAL 1
#define SYS_ENOMEM 2
#define SYS_EBADF  3
#define SYS_ENOENT 4
#define SYS_EMFILE 5
#define SYS_EIO    6

extern void syscall_entry(void);

/* Syscall execution depth (defined in kernel.c): incremented by the
 * int-0x80 stub on entry, decremented on return. While nonzero the PIT
 * defers preemption — the interrupted frame's CS:RIP is the user syscall
 * site, so privilege alone cannot tell "user code" from "kernel code
 * running on behalf of a syscall". */
extern volatile uint64_t kernel_exec_depth;

void syscall_init(void);
void syscall_handler(registers_t* regs);

#endif
