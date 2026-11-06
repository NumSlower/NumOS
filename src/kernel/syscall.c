/* NumOS — syscall.c — System-call dispatcher (Phase 10)
 *
 * int 0x80 entries land here via syscall_entry (asm) with the full
 * registers_t frame on the process kernel stack. Every pointer argument is
 * validated against the user region before use; VFS paths are copied into
 * kernel memory first. Negative errno returns follow the documented table
 * in include/kernel/syscall.h.
 */
#include "kernel/syscall.h"
#include "process/process.h"
#include "process/scheduler.h"
#include "process/userprog.h"
#include "fs/vfs.h"
#include "fs/fat32.h"
#include "drivers/serial.h"
#include "tty/tty.h"
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "cpu/idt.h"
#include "cpu/heap.h"
#include "libs/string.h"
#include "libs/kprintf.h"

/* Phase 13 syscalls (defined below the dispatcher). */
static long sys_exec(const char* upath, const char* const* uargv, const char* const* uenvp);
static long sys_wait(int pid);
static long sys_sleep(uint32_t ms);
static long sys_ps(void* ubuf, uint32_t max);
static long sys_list(const char* upath, void* ubuf, uint32_t max);
static long sys_stat_line(const char* upath, void* ubuf, uint32_t max);
static long sys_unlink(const char* upath);

#define SYSCALL_GATE_ATTR 0xEE

static bool user_range_ok(uint64_t ptr, uint64_t len) {
    if (ptr < USER_VA_BASE) return false;
    if (len > USER_VA_LIMIT - ptr) return false;
    return true;
}

static bool user_string_ok(const char* user_path) {
    if (!user_range_ok((uint64_t)user_path, 1)) return false;
    for (uint32_t i = 0; i < VFS_PATH_MAX; i++) {
        if (!user_range_ok((uint64_t)user_path + i, 1)) return false;
        if (user_path[i] == '\0') return true;
    }
    return false;
}

static long sys_write(int fd, const void* ubuf, uint32_t len) {
    struct process* proc = process_get_current();
    if (!proc || !user_range_ok((uint64_t)ubuf, len)) return -SYS_EINVAL;

    if (fd == 1 || fd == 2) {
        /* stdout/stderr are the process's controlling terminal. */
        struct tty* con = tty_get(proc->tty_index);
        tty_write(con, (const char*)ubuf, len);
        return (long)len;
    }

    struct vfs_file* f = user_fd_get(proc, fd);
    if (!f || fd < 3) return -SYS_EBADF;
    size_t wrote = 0;
    if (vfs_write(f, ubuf, len, &wrote) != VFS_OK) return -SYS_EIO;
    return (long)wrote;
}

static long sys_read(int fd, void* ubuf, uint32_t len) {
    struct process* proc = process_get_current();
    if (!proc || !user_range_ok((uint64_t)ubuf, len)) return -SYS_EINVAL;

    if (fd == 0) {
        /* stdin is the process's controlling terminal: canonical line
         * discipline, 5s EOF timeout */
        struct tty* con = tty_get(proc->tty_index);
        tty_set_fg_pid(con, proc->pid);
        char kbuf[TTY_LINE_MAX];
        long got = tty_read(con, kbuf, len < TTY_LINE_MAX ? len : TTY_LINE_MAX - 1, 5000);
        if (got <= 0) return 0;
        if (!user_range_ok((uint64_t)ubuf, (uint32_t)got)) return -SYS_EINVAL;
        memcpy((void*)ubuf, kbuf, (uint32_t)got);
        return got;
    }

    struct vfs_file* f = user_fd_get(proc, fd);
    if (!f || fd < 3) return -SYS_EBADF;
    size_t got = 0;
    if (vfs_read(f, ubuf, len, &got) != VFS_OK) return -SYS_EIO;
    return (long)got;
}

static long sys_open(const char* upath, int flags) {
    struct process* proc = process_get_current();
    if (!proc || !user_string_ok(upath)) return -SYS_EINVAL;

    char path[VFS_PATH_MAX];
    strncpy(path, upath, VFS_PATH_MAX - 1);
    path[VFS_PATH_MAX - 1] = '\0';

    struct vfs_file* f = NULL;
    if (vfs_open(path, flags, &f) != VFS_OK) return -SYS_ENOENT;

    int fd = user_fd_alloc(proc, f);
    if (fd < 0) {
        vfs_close(f);
        return -SYS_EMFILE;
    }
    return fd;
}

static long sys_close(int fd) {
    struct process* proc = process_get_current();
    if (!proc) return -SYS_EINVAL;
    struct vfs_file* f = user_fd_get(proc, fd);
    if (!f || fd < 3) return -SYS_EBADF;
    int rc = vfs_close(f);
    user_fd_free(proc, fd);
    return (rc == VFS_OK) ? 0 : -SYS_EIO;
}

static long sys_sbrk(int64_t delta) {
    struct process* proc = process_get_current();
    if (!proc || !proc->is_user) return -(long)SYS_EINVAL;

    uint64_t old = proc->user_heap_end;
    if (delta == 0) return (long)old;

    uint64_t new_end = old + (uint64_t)delta;
    if (new_end < USER_HEAP_BASE || new_end >= USER_HEAP_LIMIT) {
        return -(long)SYS_ENOMEM;
    }

    if (delta > 0) {
        uint64_t start_page = old & ~0xFFFULL;
        for (uint64_t page = start_page; page < new_end; page += 4096) {
            if (!vmm_is_user_mapped((uint64_t*)proc->cr3, page)) {
                uint64_t frame = pmm_alloc_frame();
                if (!frame) return -(long)SYS_ENOMEM;
                memset((void*)frame, 0, 4096);
                vmm_map_in((uint64_t*)proc->cr3, page, frame, PAGE_USER | PAGE_WRITE);
            }
        }
    }

    proc->user_heap_end = new_end;
    return (long)old;
}

void syscall_handler(registers_t* regs) {
    struct process* proc = process_get_current();
    if (!proc) {
        kprintf("syscall: no current process\n");
        return;
    }

    long ret = 0;
    switch (regs->rax) {
        case SYS_READ:
            ret = sys_read((int)regs->rdi, (void*)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_WRITE:
            ret = sys_write((int)regs->rdi, (const void*)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_OPEN:
            ret = sys_open((const char*)regs->rdi, (int)regs->rsi);
            break;
        case SYS_CLOSE:
            ret = sys_close((int)regs->rdi);
            break;
        case SYS_EXIT:
            kernel_exec_depth--; /* this syscall never returns through the stub */
            proc->exit_code = (int)regs->rdi;
            proc->state = PROC_STATE_ZOMBIE;
            kprintf("process: %s (pid %u) exited with code %d\n",
                    proc->name, proc->pid, proc->exit_code);
            scheduler_schedule();
            ret = 0;
            break;
        case SYS_GETPID:
            ret = (long)proc->pid;
            break;
        case SYS_SBRK:
            ret = sys_sbrk((int64_t)regs->rdi);
            break;
        case SYS_YIELD:
            scheduler_yield();
            ret = 0;
            break;
        case SYS_UPTIME:
            ret = (long)scheduler_get_uptime_ticks();
            break;
        case SYS_EXEC:
            ret = sys_exec((const char*)regs->rdi,
                           (const char* const*)regs->rsi,
                           (const char* const*)regs->rdx);
            break;
        case SYS_WAIT:
            ret = sys_wait((int)regs->rdi);
            break;
        case SYS_SLEEP:
            ret = sys_sleep((uint32_t)regs->rdi);
            break;
        case SYS_PS:
            ret = sys_ps((void*)regs->rdi, (uint32_t)regs->rsi);
            break;
        case SYS_LIST:
            ret = sys_list((const char*)regs->rdi, (void*)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_STAT:
            ret = sys_stat_line((const char*)regs->rdi, (void*)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_UNLINK:
            ret = sys_unlink((const char*)regs->rdi);
            break;
        default:
            kprintf("syscall: pid %u unknown nr %lu\n", proc->pid, regs->rax);
            ret = -(long)SYS_EINVAL;
            break;
    }

    regs->rax = (uint64_t)ret;

    /* Ctrl-C job-control equivalent: a termination request on the running
     * user process takes effect at this kernel boundary. */
    if (proc->is_user && proc->terminate_requested) {
        proc->exit_code = 130;
        proc->state = PROC_STATE_ZOMBIE;
        kprintf("process: %s (pid %u) terminated (Ctrl-C)\n", proc->name, proc->pid);
        scheduler_schedule();
    }
}

/* ---- Phase 13: exec/wait/sleep/ps --------------------------------------- */

struct list_sink { char* buf; uint32_t off; uint32_t max; };

static int list_sink_cb(const struct vfs_stat* st, void* ctx) {
    struct list_sink* sink = (struct list_sink*)ctx;
    char line[128];
    uint32_t l = (uint32_t)ksprintf(line, "%s %s %u\n", st->name,
                                    st->is_dir ? "DIR" : "FILE", (unsigned)st->size);
    if (sink->off + l > sink->max) return -1; /* stop listing */
    memcpy(sink->buf + sink->off, line, l);
    sink->off += l;
    return 0;
}

#define EXEC_ARGV_MAX 16
#define EXEC_ENVP_MAX 32

static long sys_exec(const char* upath, const char* const* uargv, const char* const* uenvp) {
    struct process* proc = process_get_current();
    if (!proc || !user_string_ok(upath)) return -SYS_EINVAL;

    char path[VFS_PATH_MAX];
    strncpy(path, upath, VFS_PATH_MAX - 1);
    path[VFS_PATH_MAX - 1] = '\0';

    static const char* argv[EXEC_ARGV_MAX];
    static char argv_str[EXEC_ARGV_MAX][64];
    uint32_t argc = 0;
    if (uargv) {
        for (uint32_t i = 0; i < EXEC_ARGV_MAX; i++) {
            const char* up = NULL;
            if (!user_range_ok((uint64_t)(uargv + i), 8)) return -SYS_EINVAL;
            up = *(const char* const*)((uint64_t)uargv + i * 8);
            if (!up) break;
            if (!user_string_ok(up)) return -SYS_EINVAL;
            strncpy(argv_str[argc], up, 63);
            argv_str[argc][63] = '\0';
            argv[argc] = argv_str[argc];
            argc++;
        }
    }
    argv[argc] = NULL;

    static const char* envp[EXEC_ENVP_MAX];
    static char envp_str[EXEC_ENVP_MAX][96];
    uint32_t envc = 0;
    if (uenvp) {
        for (uint32_t i = 0; i < EXEC_ENVP_MAX; i++) {
            const char* up = NULL;
            if (!user_range_ok((uint64_t)(uenvp + i), 8)) return -SYS_EINVAL;
            up = *(const char* const*)((uint64_t)uenvp + i * 8);
            if (!up) break;
            if (!user_string_ok(up)) return -SYS_EINVAL;
            strncpy(envp_str[envc], up, 95);
            envp_str[envc][95] = '\0';
            envp[envc] = envp_str[envc];
            envc++;
        }
    }
    envp[envc] = NULL;

    struct vfs_stat st;
    if (vfs_stat(path, &st) != VFS_OK) return -SYS_ENOENT;
    if (st.size == 0 || st.size > 256 * 1024) return -SYS_EINVAL;

    uint8_t* image = kmalloc(st.size);
    if (!image) return -SYS_ENOMEM;

    struct vfs_file* f = NULL;
    long rc = -SYS_EIO;
        if (vfs_open(path, VFS_O_RDONLY, &f) == VFS_OK) {
            size_t got = 0;
            if (vfs_read(f, image, st.size, &got) == VFS_OK && got == st.size) {
                struct process* child = user_process_exec(path, image, got,
                                                          argv, envp, proc->pid,
                                                          proc->tty_index);
                kprintf("exec: spawn %s -> %s pid %d\n", path,
                        child ? "ok" : "FAILED", child ? (int)child->pid : -1);
                rc = child ? (long)child->pid : -SYS_ENOMEM;
            }
            vfs_close(f);
        }
    kfree(image);
    return rc;
}

static long sys_wait(int pid) {
    struct process* proc = process_get_current();
    if (!proc) return -SYS_EINVAL;

    for (;;) {
        struct process* z = process_find_child_zombie(proc->pid);
        if (z) {
            long code = z->exit_code;
            process_reap(z);
            return code;
        }
        /* Child still running? */
        bool alive = false;
        for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
            struct process* p = process_get_by_index(i);
            if (p && p->pid == (uint32_t)pid && p->ppid == proc->pid &&
                p->state != PROC_STATE_UNUSED && p->state != PROC_STATE_ZOMBIE) {
                alive = true;
                break;
            }
        }
        if (!alive) return -SYS_EINVAL; /* not our child / already reaped */
        scheduler_yield();
    }
}

static long sys_sleep(uint32_t ms) {
    uint64_t deadline = scheduler_get_uptime_ticks() + ms;
    while (scheduler_get_uptime_ticks() < deadline) {
        scheduler_yield();
    }
    return 0;
}

static long sys_ps(void* ubuf, uint32_t max) {    struct process* proc = process_get_current();
    if (!proc || !user_range_ok((uint64_t)ubuf, max)) return -SYS_EINVAL;

    static char line[96];
    uint32_t off = 0;
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        struct process* p = process_get_by_index(i);
        if (!p) break;
        static const char* state_names[] = {"unused", "ready", "running", "blocked", "zombie"};
        uint32_t l = (uint32_t)ksprintf(line, "%u %u %s %s\n",
                                        (unsigned)p->pid, (unsigned)p->ppid,
                                        state_names[p->state], p->name);
        if (off + l > max) break;
        memcpy((void*)((uint64_t)ubuf + off), line, l);
        off += l;
    }
    return (long)off;
}

static long sys_list(const char* upath, void* ubuf, uint32_t max) {
    struct process* proc = process_get_current();
    if (!proc || !user_string_ok(upath) || !user_range_ok((uint64_t)ubuf, max)) {
        return -SYS_EINVAL;
    }
    char path[VFS_PATH_MAX];
    strncpy(path, upath, VFS_PATH_MAX - 1);
    path[VFS_PATH_MAX - 1] = '\0';

    struct list_sink sink;
    sink.buf = (char*)ubuf;
    sink.off = 0;
    sink.max = max;

    int rc = vfs_list(path, list_sink_cb, &sink);
    if (rc != VFS_OK) return -SYS_ENOENT;
    return (long)sink.off;
}

static long sys_stat_line(const char* upath, void* ubuf, uint32_t max) {
    struct process* proc = process_get_current();
    if (!proc || !user_string_ok(upath) || !user_range_ok((uint64_t)ubuf, max)) {
        return -SYS_EINVAL;
    }
    char path[VFS_PATH_MAX];
    strncpy(path, upath, VFS_PATH_MAX - 1);
    path[VFS_PATH_MAX - 1] = '\0';

    struct vfs_stat st;
    if (vfs_stat(path, &st) != VFS_OK) return -SYS_ENOENT;

    char line[128];
    uint32_t l = (uint32_t)ksprintf(line, "%s %s %u\n", st.name,
                                    st.is_dir ? "DIR" : "FILE", (unsigned)st.size);
    if (l > max) return -SYS_EINVAL;
    memcpy(ubuf, line, l);
    return (long)l;
}

static long sys_unlink(const char* upath) {
    struct process* proc = process_get_current();
    if (!proc || !user_string_ok(upath)) return -SYS_EINVAL;
    char path[VFS_PATH_MAX];
    strncpy(path, upath, VFS_PATH_MAX - 1);
    path[VFS_PATH_MAX - 1] = '\0';
    return vfs_unlink(path) == VFS_OK ? 0 : -SYS_EIO;
}

void syscall_init(void) {
    idt_set_gate(SYSCALL_NR, (uint64_t)syscall_entry, 0x08, SYSCALL_GATE_ATTR, 0);
}
