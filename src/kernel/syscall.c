/*
 * syscall.c - NumOS System Call Implementation
 *
 * Changes from previous version:
 *   • sys_write(fd=1/2) now routes to the framebuffer console when the BGA
 *     framebuffer is active.  Existing user-space programs (shell.c)
 *     that write to fd=1 therefore see their output in the FB terminal with
 *     zero source changes.
 *   • Six new framebuffer syscalls: SYS_FB_INFO … SYS_FB_FILLRECT (201-206).
 */

#include "kernel/syscall.h"
#include "kernel/kernel.h"
#include "kernel/config.h"
#include "kernel/sysinfo.h"
#include "kernel/elf_loader.h"
#include "drivers/graphices/vga.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "drivers/framebuffer.h"
#include "drivers/device.h"
#include "drivers/network.h"
#include "drivers/usb.h"
#include "drivers/ata.h"
#include "fs/fat32.h"
#include "fs/vfs.h"
#include "cpu/gdt.h"
#include "cpu/heap.h"
#include "cpu/paging.h"
#include "kernel/scheduler.h"

/* =========================================================================
 * MSR helpers
 * ======================================================================= */

#define MSR_EFER    0xC0000080
#define MSR_STAR    0xC0000081
#define MSR_LSTAR   0xC0000082
#define MSR_SFMASK  0xC0000084
#define EFER_SCE    (1UL << 0)
#define SFMASK_IF   (1UL << 9)

extern char keyboard_getchar_buffered(void);
extern void syscall_entry(void);

static struct syscall_stats stats;
static int syscall_initialised = 0;

static int is_user_range(const void *ptr, size_t len) {
    uint64_t addr = (uint64_t)(uintptr_t)ptr;
    if (addr >= KERNEL_VIRTUAL_BASE) return 0;
    if (len == 0) return 1;
    if (addr + len < addr) return 0;
    if (addr + len >= KERNEL_VIRTUAL_BASE) return 0;
    return 1;
}

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b,
                         uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf));
}

static void cpu_vendor_string(char *out, size_t cap) {
    if (!out || cap == 0) return;
    uint32_t a, b, c, d;
    cpuid(0, &a, &b, &c, &d);
    char tmp[13];
    memcpy(tmp + 0, &b, 4);
    memcpy(tmp + 4, &d, 4);
    memcpy(tmp + 8, &c, 4);
    tmp[12] = '\0';
    size_t n = strlen(tmp);
    if (n >= cap) n = cap - 1;
    memcpy(out, tmp, n);
    out[n] = '\0';
}

static uint32_t cpu_logical_count(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);
    uint32_t count = (b >> 16) & 0xFFu;
    if (count == 0) count = 1;
    return count;
}

static void copy_str(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static inline uint64_t rdmsr(uint32_t msr) {
    uint32_t lo, hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
    return ((uint64_t)hi<<32)|lo;
}
static inline void wrmsr(uint32_t msr, uint64_t value) {
    __asm__ volatile("wrmsr"::"a"((uint32_t)value),"d"((uint32_t)(value>>32)),"c"(msr):"memory");
}

/* =========================================================================
 * Init
 * ======================================================================= */

void syscall_init(void) {
    if (syscall_initialised) return;
    vga_writestring("SYSCALL: Initializing SYSCALL/SYSRET...\n");
    memset(&stats, 0, sizeof(stats));

    wrmsr(MSR_EFER, rdmsr(MSR_EFER) | EFER_SCE);

    uint64_t star = 0;
    star |= ((uint64_t)GDT_KERNEL_CODE << 32);
    star |= ((uint64_t)GDT_KERNEL_DATA << 48);
    wrmsr(MSR_STAR,   star);
    wrmsr(MSR_LSTAR,  (uint64_t)syscall_entry);
    wrmsr(MSR_SFMASK, SFMASK_IF);

    syscall_initialised = 1;
    vga_writestring("SYSCALL: Ready\n");
}

/* =========================================================================
 * Standard syscall implementations
 * ======================================================================= */

/*
 * sys_write - write to a file descriptor.
 *
 * fd=1 (stdout) and fd=2 (stderr):
 *   When the framebuffer is active, output goes to the FB console.
 *   Otherwise falls through to VGA text mode.
 *   This makes existing user-space programs work transparently in both
 *   display modes without any source changes.
 */
int64_t sys_write(int fd, const void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;
    if (fd == FD_STDOUT || fd == FD_STDERR) {
        const char *p = (const char *)buf;
        if (fb_is_available()) {
            fb_con_write(p, count);
        } else {
            vga_write(p, count);
        }
        return (int64_t)count;
    }

    /* Reserve 0,1,2 for stdin/stdout/stderr. VFS file descriptors start at 3. */
    if (fd < 3) return SYSCALL_EBADF;
    int vfs_fd = fd - 3;
    ssize_t n  = vfs_write(vfs_fd, buf, count);
    if (n < 0) return SYSCALL_EBADF;
    return (int64_t)n;
}

int64_t sys_read(int fd, void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;

    /* fd=0: stdin, backed by the PS/2 keyboard ring buffer. */
    if (fd == FD_STDIN) {
        return sys_input(buf, count);
    }

    /* Reserve 1,2 for stdout/stderr. VFS file descriptors start at 3. */
    if (fd < 3) return SYSCALL_EBADF;

    int vfs_fd = fd - 3;
    ssize_t n  = vfs_read(vfs_fd, buf, count);
    if (n < 0) return SYSCALL_EBADF;
    return (int64_t)n;
}

int64_t sys_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path) return SYSCALL_EFAULT;

    int vfs_fd = vfs_open(path, flags);
    if (vfs_fd < 0) return SYSCALL_EINVAL;
    return (int64_t)(vfs_fd + 3);
}

int64_t sys_exec(const char *path) {
    if (!path) return SYSCALL_EFAULT;

    char kpath[256];
    size_t i = 0;
    for (; i < sizeof(kpath) - 1; i++) {
        if (!is_user_range(path + i, 1)) return SYSCALL_EFAULT;
        char c = path[i];
        kpath[i] = c;
        if (c == 0) break;
    }
    if (i >= sizeof(kpath) - 1) return SYSCALL_EINVAL;
    if (kpath[0] == 0) return SYSCALL_EINVAL;

    uint64_t child_cr3 = paging_create_user_pml4();
    if (!child_cr3) return SYSCALL_ENOMEM;

    struct elf_load_result result;
    struct page_table *saved_pml4 = paging_get_active_pml4();
    uint64_t saved_cr3 = paging_get_current_cr3();
    __asm__ volatile("cli");
    paging_set_active_pml4((struct page_table *)(uintptr_t)child_cr3);
    paging_switch_to(child_cr3);
    int rc = elf_load_from_file(kpath, &result);
    paging_set_active_pml4(saved_pml4);
    paging_switch_to(saved_cr3);
    __asm__ volatile("sti");
    if (rc != ELF_OK) return SYSCALL_EINVAL;

    struct process *proc = process_spawn(kpath, result.entry,
                                         result.stack_top, result.stack_bottom);
    if (!proc) {
        uint64_t stack_top_page = paging_align_up(result.stack_top, PAGE_SIZE);
        struct page_table *saved = paging_get_active_pml4();
        uint64_t old_cr3 = paging_get_current_cr3();
        __asm__ volatile("cli");
        paging_set_active_pml4((struct page_table *)(uintptr_t)child_cr3);
        paging_switch_to(child_cr3);
        elf_unload(result.load_base, result.load_end, result.stack_bottom, stack_top_page);
        paging_set_active_pml4(saved);
        paging_switch_to(old_cr3);
        __asm__ volatile("sti");
        return SYSCALL_ENOMEM;
    }

    if (process_configure_image(proc, &result, child_cr3) != 0) {
        uint64_t stack_top_page = paging_align_up(result.stack_top, PAGE_SIZE);
        uint64_t saved_cr3 = paging_get_current_cr3();
        struct page_table *saved = paging_get_active_pml4();
        __asm__ volatile("cli");
        paging_set_active_pml4((struct page_table *)(uintptr_t)child_cr3);
        paging_switch_to(child_cr3);
        elf_unload(result.load_base, result.load_end, result.stack_bottom, stack_top_page);
        paging_set_active_pml4(saved);
        paging_switch_to(saved_cr3);
        __asm__ volatile("sti");
        process_discard(proc);
        return SYSCALL_ENOMEM;
    }

    while (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
        schedule();
    }

    int exit_code = proc->exit_code;
    process_reap(proc);
    return exit_code;
}

int64_t sys_exec_argv(const char *path, const char *cmdline) {
    if (!path || !cmdline) return SYSCALL_EFAULT;

    char kpath[256];
    size_t i = 0;
    for (; i < sizeof(kpath) - 1; i++) {
        if (!is_user_range(path + i, 1)) return SYSCALL_EFAULT;
        char c = path[i];
        kpath[i] = c;
        if (c == 0) break;
    }
    if (i >= sizeof(kpath) - 1) return SYSCALL_EINVAL;
    if (kpath[0] == 0) return SYSCALL_EINVAL;

    char kcmd[256];
    i = 0;
    for (; i < sizeof(kcmd) - 1; i++) {
        if (!is_user_range(cmdline + i, 1)) return SYSCALL_EFAULT;
        char c = cmdline[i];
        kcmd[i] = c;
        if (c == 0) break;
    }
    if (i >= sizeof(kcmd) - 1) return SYSCALL_EINVAL;

    uint64_t child_cr3 = paging_create_user_pml4();
    if (!child_cr3) return SYSCALL_ENOMEM;

    struct elf_load_result result;
    struct page_table *saved_pml4 = paging_get_active_pml4();
    uint64_t saved_cr3 = paging_get_current_cr3();
    __asm__ volatile("cli");
    paging_set_active_pml4((struct page_table *)(uintptr_t)child_cr3);
    paging_switch_to(child_cr3);
    int rc = elf_load_from_file(kpath, &result);
    paging_set_active_pml4(saved_pml4);
    paging_switch_to(saved_cr3);
    __asm__ volatile("sti");
    if (rc != ELF_OK) return SYSCALL_EINVAL;

    struct process *proc = process_spawn(kpath, result.entry,
                                         result.stack_top, result.stack_bottom);
    if (!proc) {
        uint64_t stack_top_page = paging_align_up(result.stack_top, PAGE_SIZE);
        struct page_table *saved = paging_get_active_pml4();
        uint64_t old_cr3 = paging_get_current_cr3();
        __asm__ volatile("cli");
        paging_set_active_pml4((struct page_table *)(uintptr_t)child_cr3);
        paging_switch_to(child_cr3);
        elf_unload(result.load_base, result.load_end, result.stack_bottom, stack_top_page);
        paging_set_active_pml4(saved);
        paging_switch_to(old_cr3);
        __asm__ volatile("sti");
        return SYSCALL_ENOMEM;
    }

    if (process_configure_image(proc, &result, child_cr3) != 0) {
        uint64_t stack_top_page = paging_align_up(result.stack_top, PAGE_SIZE);
        uint64_t saved_cr3 = paging_get_current_cr3();
        struct page_table *saved = paging_get_active_pml4();
        __asm__ volatile("cli");
        paging_set_active_pml4((struct page_table *)(uintptr_t)child_cr3);
        paging_switch_to(child_cr3);
        elf_unload(result.load_base, result.load_end, result.stack_bottom, stack_top_page);
        paging_set_active_pml4(saved);
        paging_switch_to(saved_cr3);
        __asm__ volatile("sti");
        process_discard(proc);
        return SYSCALL_ENOMEM;
    }

    strncpy(proc->cmdline, kcmd, PROCESS_CMDLINE_LEN);
    proc->cmdline[PROCESS_CMDLINE_LEN - 1] = '\0';

    while (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
        schedule();
    }

    int exit_code = proc->exit_code;
    process_reap(proc);
    return exit_code;
}

int64_t sys_close(int fd) {
    if (fd < 3) return SYSCALL_EBADF;
    int vfs_fd = fd - 3;
    return (vfs_close(vfs_fd) == 0) ? 0 : SYSCALL_EBADF;
}

int64_t sys_exit(int status) {
    process_exit(status);
    while (1) __asm__ volatile("hlt");
    return 0;
}

int64_t sys_getpid(void) {
    struct process *p = scheduler_current();
    return p ? (int64_t)(p->group_id ? p->group_id : p->pid) : 1;
}

int64_t sys_sleep_ms(uint64_t ms) {
    process_sleep_until(timer_get_uptime_ms() + ms);
    return 0;
}

int64_t sys_uptime_ms(void) {
    return (int64_t)timer_get_uptime_ms();
}

int64_t sys_sysinfo(struct sysinfo *info) {
    if (!info) return SYSCALL_EFAULT;
    if (!is_user_range(info, sizeof(struct sysinfo))) return SYSCALL_EFAULT;

    struct sysinfo out;
    memset(&out, 0, sizeof(out));

    out.uptime_ms = timer_get_uptime_ms();

    struct pmm_stats pmm;
    pmm_get_stats(&pmm);
    out.total_memory = pmm.total_memory;
    out.used_memory  = pmm.used_frames * PAGE_SIZE;
    out.free_memory  = pmm.free_frames * PAGE_SIZE;
    out.total_frames = pmm.total_frames;
    out.used_frames  = pmm.used_frames;
    out.free_frames  = pmm.free_frames;

    struct heap_stats hs;
    heap_get_stats(&hs);
    out.heap_total = hs.total_size;
    out.heap_used  = hs.used_size;
    out.heap_free  = hs.free_size;

    struct paging_stats ps;
    paging_get_stats(&ps);
    out.page_faults  = ps.page_faults;
    out.pages_mapped = ps.pages_mapped;
    out.pages_unmapped = ps.pages_unmapped;
    out.tlb_flushes  = ps.tlb_flushes;

    struct sched_stats ss;
    scheduler_get_stats(&ss);
    out.processes_active = ss.active_processes;
    out.processes_max    = MAX_PROCESSES;

    strncpy(out.version, NUMOS_VERSION, NUMOS_SYSINFO_VERSION_LEN - 1);

    memcpy(info, &out, sizeof(out));
    return 0;
}

int64_t sys_hwinfo(struct hwinfo *info, size_t len) {
    if (!info) return SYSCALL_EFAULT;
    if (len < sizeof(struct hwinfo)) return SYSCALL_EINVAL;
    if (!is_user_range(info, len)) return SYSCALL_EFAULT;

    struct hwinfo out;
    memset(&out, 0, sizeof(out));
    out.size = (uint32_t)sizeof(out);
    out.version = NUMOS_HWINFO_VERSION;
    out.form_factor = HWINFO_FORM_UNKNOWN;
    out.power_source = HWINFO_POWER_UNKNOWN;
    out.battery_percent = -1;
    out.battery_state = HWINFO_BATTERY_NONE;

    out.uptime_ms = timer_get_uptime_ms();
    out.flags |= HWINFO_HAS_UPTIME;

    out.cpu_count = cpu_logical_count();
    out.flags |= HWINFO_HAS_CPU;
    cpu_vendor_string(out.cpu_vendor, sizeof(out.cpu_vendor));

    struct pmm_stats pmm;
    pmm_get_stats(&pmm);
    out.mem_total = pmm.total_memory;
    out.mem_free  = pmm.free_frames * PAGE_SIZE;
    out.flags |= HWINFO_HAS_MEMORY;

    struct heap_stats hs;
    heap_get_stats(&hs);
    out.heap_total = hs.total_size;
    out.heap_used  = hs.used_size;
    out.flags |= HWINFO_HAS_HEAP;

    struct sched_stats ss;
    scheduler_get_stats(&ss);
    out.process_count = ss.active_processes;
    out.process_max   = MAX_PROCESSES;
    out.flags |= HWINFO_HAS_PROCESSES;

    const struct hypervisor_info *hv = device_get_hypervisor();
    if (hv && hv->detected) {
        copy_str(out.hypervisor, hv->name, sizeof(out.hypervisor));
        copy_str(out.machine, hv->vendor_string, sizeof(out.machine));
        out.form_factor = HWINFO_FORM_VM;
        out.flags |= HWINFO_HAS_HYPERVISOR | HWINFO_HAS_FORM_FACTOR;
    } else {
        copy_str(out.hypervisor, "none", sizeof(out.hypervisor));
        copy_str(out.machine, "baremetal", sizeof(out.machine));
    }

    memcpy(info, &out, sizeof(out));
    return 0;
}

int64_t sys_puts(const char *str) {
    if (!str) return SYSCALL_EFAULT;
    if (fb_is_available()) {
        fb_con_print(str);
        fb_con_putchar('\n');
    } else {
        vga_writestring(str);
        vga_putchar('\n');
    }
    return 0;
}

int64_t sys_get_cmdline(char *buf, size_t len) {
    if (!buf) return SYSCALL_EFAULT;
    if (!is_user_range(buf, len)) return SYSCALL_EFAULT;
    if (len == 0) return 0;

    struct process *p = scheduler_current();
    if (!p) return SYSCALL_EINVAL;

    size_t n = 0;
    while (n + 1 < len && p->cmdline[n]) {
        buf[n] = p->cmdline[n];
        n++;
    }
    buf[n] = '\0';
    return (int64_t)n;
}

int64_t sys_listdir(const char *path, struct fat32_dirent *entries, int max_entries) {
    struct vfs_dirent *tmp;

    if (!entries) return SYSCALL_EFAULT;
    if (max_entries <= 0) return SYSCALL_EINVAL;

    size_t total = sizeof(struct fat32_dirent) * (size_t)max_entries;
    if (!is_user_range(entries, total)) return SYSCALL_EFAULT;

    tmp = (struct vfs_dirent *)kmalloc(sizeof(*tmp) * (size_t)max_entries);
    if (!tmp) return SYSCALL_ENOMEM;

    char kpath[256];
    kpath[0] = '\0';
    if (path) {
        size_t i = 0;
        for (; i < sizeof(kpath) - 1; i++) {
            if (!is_user_range(path + i, 1)) {
                kfree(tmp);
                return SYSCALL_EFAULT;
            }
            char c = path[i];
            kpath[i] = c;
            if (c == 0) break;
        }
        if (i >= sizeof(kpath) - 1) {
            kfree(tmp);
            return SYSCALL_EINVAL;
        }
    }

    int count = vfs_listdir((path && kpath[0] != '\0') ? kpath : NULL,
                            tmp,
                            max_entries);
    if (count < 0) {
        kfree(tmp);
        return SYSCALL_EINVAL;
    }

    for (int i = 0; i < count; i++) {
        memset(&entries[i], 0, sizeof(entries[i]));
        strncpy(entries[i].name, tmp[i].name, sizeof(entries[i].name) - 1);
        entries[i].size = tmp[i].size;
        entries[i].attr = tmp[i].attr;
        entries[i].cluster = tmp[i].fs_data;
    }

    kfree(tmp);
    return (int64_t)count;
}

int64_t sys_input(void *buf, size_t count) {
    if (!buf)   return SYSCALL_EFAULT;
    if (!count) return 0;

    char  *p   = (char *)buf;
    size_t got = 0;
    while (got < count) {
        char c = keyboard_getchar_buffered();
        p[got++] = c;
        if (c == '\n') break;
    }
    return (int64_t)got;
}

int64_t sys_input_peek(char *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, 1)) return SYSCALL_EFAULT;
    char c = 0;
    int got = keyboard_try_getchar(&c);
    if (!got) return 0;
    *out = c;
    return 1;
}

int64_t sys_proclist(struct proc_info *out, size_t max) {
    if (!out) return SYSCALL_EFAULT;
    if (max == 0) return SYSCALL_EINVAL;
    if (max > MAX_PROCESSES) max = MAX_PROCESSES;

    size_t total = sizeof(struct proc_info) * max;
    if (!is_user_range(out, total)) return SYSCALL_EFAULT;

    struct proc_info tmp[MAX_PROCESSES];
    int count = scheduler_list_processes(tmp, (int)max);
    if (count < 0) return SYSCALL_EINVAL;
    memcpy(out, tmp, (size_t)count * sizeof(struct proc_info));
    return count;
}

int64_t sys_yield(void) {
    schedule();
    return 0;
}

int64_t sys_time_read(struct numos_calendar_time *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    struct numos_calendar_time now;
    if (timer_get_wall_clock(&now) != 0) return SYSCALL_EINVAL;
    memcpy(out, &now, sizeof(now));
    return 0;
}

int64_t sys_timer_create(uint64_t delay_ms, uint64_t period_ms, uint32_t flags) {
    struct process *cur = scheduler_current();
    if (!cur) return SYSCALL_EINVAL;
    if (flags & ~NUMOS_TIMER_PERIODIC) return SYSCALL_EINVAL;
    if ((flags & NUMOS_TIMER_PERIODIC) && period_ms == 0) return SYSCALL_EINVAL;

    int owner = (cur->group_id > 0) ? cur->group_id : cur->pid;
    int timer_id = timer_create_object(owner, delay_ms, period_ms, flags);
    if (timer_id < 0) return SYSCALL_ENOMEM;
    return timer_id;
}

int64_t sys_timer_wait(int timer_id) {
    struct process *cur = scheduler_current();
    if (!cur) return SYSCALL_EINVAL;

    uint64_t wake_ms = 0;
    int owner = (cur->group_id > 0) ? cur->group_id : cur->pid;
    if (timer_prepare_wait_object(owner, timer_id, &wake_ms) != 0) {
        return SYSCALL_EINVAL;
    }

    uint64_t now = timer_get_uptime_ms();
    if (wake_ms > now) process_sleep_until(wake_ms);

    if (timer_complete_wait_object(owner, timer_id) != 0) {
        return SYSCALL_EINVAL;
    }

    return 0;
}

int64_t sys_timer_info(int timer_id, struct numos_timer_info *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    struct process *cur = scheduler_current();
    if (!cur) return SYSCALL_EINVAL;

    struct numos_timer_info info;
    int owner = (cur->group_id > 0) ? cur->group_id : cur->pid;
    if (timer_get_object_info(owner, timer_id, &info) != 0) {
        return SYSCALL_EINVAL;
    }

    memcpy(out, &info, sizeof(info));
    return 0;
}

int64_t sys_timer_cancel(int timer_id) {
    struct process *cur = scheduler_current();
    if (!cur) return SYSCALL_EINVAL;
    int owner = (cur->group_id > 0) ? cur->group_id : cur->pid;
    return (timer_cancel_object(owner, timer_id) == 0)
         ? 0 : SYSCALL_EINVAL;
}

int64_t sys_reboot(void) {
    __asm__ volatile("cli");
    outb(0x64, 0xFE);
    while (1) __asm__ volatile("hlt");
    return 0;
}

int64_t sys_poweroff(void) {
    __asm__ volatile("cli");

    /* Try the common VM poweroff ports used by QEMU, Bochs, and VirtualBox. */
    outw(0x604, 0x2000);
    outw(0xB004, 0x2000);
    outw(0x4004, 0x3400);

    while (1) __asm__ volatile("hlt");
    return 0;
}

/* =========================================================================
 * Framebuffer syscall implementations
 * ======================================================================= */

/*
 * sys_fb_info — query a single property of the framebuffer.
 * field: 0=width, 1=height, 2=bpp, 3=available
 */
int64_t sys_fb_info(uint64_t field) {
    if (!fb_is_available()) {
        switch (field) {
            case 0: return (int64_t)(VGA_WIDTH * 8);
            case 1: return (int64_t)(VGA_HEIGHT * 16);
            case 2: return 0;
            case 3: return 0;
            default: return SYSCALL_EINVAL;
        }
    }

    switch (field) {
        case 0: return fb_get_width();
        case 1: return fb_get_height();
        case 2: return fb_get_bpp();
        case 3: return 1;
        default: return SYSCALL_EINVAL;
    }
}

/*
 * sys_fb_write — write a buffer of chars to the framebuffer console.
 * When FB is not active, falls through to VGA.
 */
int64_t sys_fb_write(const char *buf, size_t len) {
    if (!buf) return SYSCALL_EFAULT;
    if (fb_is_available()) {
        fb_con_write(buf, len);
    } else {
        vga_write(buf, len);
    }
    return (int64_t)len;
}

/* sys_fb_clear — clear the framebuffer console. */
int64_t sys_fb_clear(void) {
    if (fb_is_available()) {
        fb_con_clear();
        return 0;
    }
    vga_clear();
    return 0;
}

/* sys_fb_setcolor — change the console text colour. */
int64_t sys_fb_setcolor(uint32_t fg, uint32_t bg) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_con_set_color(fg, bg);
    return 0;
}

/* sys_fb_setpixel — set a single pixel in the framebuffer. */
int64_t sys_fb_setpixel(int x, int y, uint32_t color) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_set_pixel(x, y, color);
    return 0;
}

/* sys_fb_fillrect — fill a rectangle in the framebuffer. */
int64_t sys_fb_fillrect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_is_available()) return SYSCALL_ENOSYS;
    fb_fill_rect(x, y, w, h, color);
    return 0;
}

int64_t sys_con_scroll(void) {
    if (fb_is_available()) {
        fb_con_enter_scroll_mode();
    } else {
        vga_enter_scroll_mode();
    }
    return 0;
}

int64_t sys_disk_info(struct numos_disk_info *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    struct numos_disk_info info;
    memset(&info, 0, sizeof(info));
    info.sector_size = 512;
    info.present = ata_primary_master.exists ? 1u : 0u;
    info.writable = ata_primary_master.exists ? 1u : 0u;
    info.sector_count = ata_primary_master.sectors;
    copy_str(info.model, ata_primary_master.model, sizeof(info.model));

    memcpy(out, &info, sizeof(info));
    return 0;
}

int64_t sys_disk_read(uint64_t lba, void *buf, uint32_t sector_count) {
    if (!buf) return SYSCALL_EFAULT;
    if (!sector_count) return 0;
    if (!is_user_range(buf, (size_t)sector_count * 512u)) return SYSCALL_EFAULT;
    if (!ata_primary_master.exists) return SYSCALL_EINVAL;
    if (sector_count > 255u) return SYSCALL_EINVAL;
    if (lba >= ata_primary_master.sectors) return SYSCALL_EINVAL;
    if (lba + sector_count > ata_primary_master.sectors) return SYSCALL_EINVAL;

    return ata_read_sectors(&ata_primary_master, lba, (uint8_t)sector_count, buf) == 0
         ? 0 : SYSCALL_EINVAL;
}

int64_t sys_disk_write(uint64_t lba, const void *buf, uint32_t sector_count) {
    if (!buf) return SYSCALL_EFAULT;
    if (!sector_count) return 0;
    if (!is_user_range(buf, (size_t)sector_count * 512u)) return SYSCALL_EFAULT;
    if (!ata_primary_master.exists) return SYSCALL_EINVAL;
    if (sector_count > 255u) return SYSCALL_EINVAL;
    if (lba >= ata_primary_master.sectors) return SYSCALL_EINVAL;
    if (lba + sector_count > ata_primary_master.sectors) return SYSCALL_EINVAL;

    return ata_write_sectors(&ata_primary_master, lba, (uint8_t)sector_count, buf) == 0
         ? 0 : SYSCALL_EINVAL;
}

int64_t sys_usb_controller_count(void) {
    return usb_controller_count();
}

int64_t sys_usb_controller_info(int index, struct numos_usb_controller_info *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    struct usb_controller_info info;
    if (usb_get_controller_info(index, &info) != 0) return SYSCALL_EINVAL;

    struct numos_usb_controller_info user_info;
    memset(&user_info, 0, sizeof(user_info));
    memcpy(&user_info, &info, sizeof(user_info));
    memcpy(out, &user_info, sizeof(user_info));
    return 0;
}

int64_t sys_usb_port_info(int controller_index, int port_index,
                          struct numos_usb_port_info *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    struct usb_port_info info;
    if (usb_get_port_info(controller_index, port_index, &info) != 0) {
        return SYSCALL_EINVAL;
    }

    struct numos_usb_port_info user_info;
    memset(&user_info, 0, sizeof(user_info));
    memcpy(&user_info, &info, sizeof(user_info));
    memcpy(out, &user_info, sizeof(user_info));
    return 0;
}

int64_t sys_thread_create(void *start, void *arg, void *trampoline) {
    if (!start || !trampoline) return SYSCALL_EFAULT;
    if (!is_user_range(start, 1) || !is_user_range(trampoline, 1)) {
        return SYSCALL_EFAULT;
    }

    struct process *cur = scheduler_current();
    if (!cur || !cur->vm_space || cur->user_entry == 0) return SYSCALL_EINVAL;

    struct process *thread =
        process_spawn_user_thread(cur->name,
                                  (uint64_t)(uintptr_t)trampoline,
                                  (uint64_t)(uintptr_t)start,
                                  (uint64_t)(uintptr_t)arg);
    if (!thread) return SYSCALL_ENOMEM;
    return thread->pid;
}

int64_t sys_thread_join(int tid, uint64_t *out_value) {
    if (out_value && !is_user_range(out_value, sizeof(*out_value))) {
        return SYSCALL_EFAULT;
    }

    struct process *cur = scheduler_current();
    if (!cur) return SYSCALL_EINVAL;
    if (tid <= 0 || tid == cur->pid) return SYSCALL_EINVAL;

    struct process *target = scheduler_find_process(tid);
    if (!target) return SYSCALL_EINVAL;
    if (target->group_id != cur->group_id) return SYSCALL_EINVAL;

    while (target->state != PROC_ZOMBIE && target->state != PROC_UNUSED) {
        schedule();
        target = scheduler_find_process(tid);
        if (!target) return SYSCALL_EINVAL;
    }

    if (target->state == PROC_UNUSED) return 0;
    if (out_value) *out_value = target->thread_exit_value;
    process_reap(target);
    return 0;
}

int64_t sys_thread_exit(uint64_t value) {
    process_exit_value(value);
    while (1) __asm__ volatile("hlt");
    return 0;
}

int64_t sys_thread_self(void) {
    struct process *cur = scheduler_current();
    return cur ? cur->pid : 0;
}

int64_t sys_net_info(struct numos_net_info *out) {
    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    struct net_info info;
    if (net_get_info(&info) != 0) return SYSCALL_EINVAL;

    struct numos_net_info user_info;
    memset(&user_info, 0, sizeof(user_info));
    memcpy(&user_info, &info, sizeof(user_info));
    memcpy(out, &user_info, sizeof(user_info));
    return 0;
}

int64_t sys_net_dhcp(uint32_t timeout_ms) {
    return (net_request_dhcp(timeout_ms) == 0) ? 0 : SYSCALL_EINVAL;
}

int64_t sys_net_ping(const uint8_t *ipv4, uint32_t timeout_ms,
                     struct numos_net_ping_result *out) {
    if (!ipv4 || !out) return SYSCALL_EFAULT;
    if (!is_user_range(ipv4, 4)) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;

    uint8_t addr[4];
    memcpy(addr, ipv4, sizeof(addr));

    struct net_ping_result result;
    if (net_ping_ipv4(addr, timeout_ms, &result) != 0) return SYSCALL_EINVAL;

    struct numos_net_ping_result user_result;
    memset(&user_result, 0, sizeof(user_result));
    memcpy(&user_result, &result, sizeof(user_result));
    memcpy(out, &user_result, sizeof(user_result));
    return 0;
}

int64_t sys_net_tcp_connect(const uint8_t *ipv4, uint16_t port, uint32_t timeout_ms) {
    uint8_t addr[4];

    if (!ipv4) return SYSCALL_EFAULT;
    if (!is_user_range(ipv4, sizeof(addr))) return SYSCALL_EFAULT;
    memcpy(addr, ipv4, sizeof(addr));
    return net_tcp_connect_ipv4(addr, port, timeout_ms);
}

int64_t sys_net_tcp_send(int handle, const void *buf, size_t len, uint32_t timeout_ms) {
    if (!buf) return SYSCALL_EFAULT;
    if (len && !is_user_range(buf, len)) return SYSCALL_EFAULT;
    return net_tcp_send(handle, buf, len, timeout_ms);
}

int64_t sys_net_tcp_recv(int handle, void *buf, size_t len, uint32_t timeout_ms) {
    if (!buf) return SYSCALL_EFAULT;
    if (len && !is_user_range(buf, len)) return SYSCALL_EFAULT;
    return net_tcp_recv(handle, buf, len, timeout_ms);
}

int64_t sys_net_tcp_close(int handle, uint32_t timeout_ms) {
    return net_tcp_close(handle, timeout_ms);
}

int64_t sys_net_tcp_info(int handle, struct numos_net_tcp_info *out) {
    struct net_tcp_info info;
    struct numos_net_tcp_info user_info;

    if (!out) return SYSCALL_EFAULT;
    if (!is_user_range(out, sizeof(*out))) return SYSCALL_EFAULT;
    if (net_tcp_get_info(handle, &info) != 0) return SYSCALL_EINVAL;

    memset(&user_info, 0, sizeof(user_info));
    memcpy(&user_info, &info, sizeof(user_info));
    memcpy(out, &user_info, sizeof(user_info));
    return 0;
}

int64_t sys_net_tls_probe(const uint8_t *ipv4,
                          uint16_t port,
                          const char *server_name,
                          uint32_t flags,
                          uint32_t timeout_ms,
                          struct numos_net_tls_result *out) {
    uint8_t addr[4];
    char server_name_copy[64];
    struct net_tls_result result;
    struct numos_net_tls_result user_result;

    if (!ipv4 || !server_name || !out) return SYSCALL_EFAULT;
    if (!is_user_range(ipv4, sizeof(addr)) ||
        !is_user_range(server_name, 1) ||
        !is_user_range(out, sizeof(*out))) {
        return SYSCALL_EFAULT;
    }

    memcpy(addr, ipv4, sizeof(addr));
    size_t i = 0;
    for (; i + 1 < sizeof(server_name_copy); i++) {
        if (!is_user_range(server_name + i, 1)) return SYSCALL_EFAULT;
        server_name_copy[i] = server_name[i];
        if (server_name_copy[i] == 0) break;
    }
    server_name_copy[sizeof(server_name_copy) - 1] = '\0';

    if (net_tls_probe_ipv4(addr, port, server_name_copy, flags, timeout_ms, &result) != 0) {
        return SYSCALL_EINVAL;
    }

    memset(&user_result, 0, sizeof(user_result));
    memcpy(&user_result, &result, sizeof(user_result));
    memcpy(out, &user_result, sizeof(user_result));
    return 0;
}

int64_t sys_net_http_get(const struct numos_net_http_request *request,
                         void *buf,
                         size_t len,
                         struct numos_net_http_result *out) {
    struct net_http_request kernel_request;
    struct net_http_result kernel_result;
    ssize_t rc;

    if (!request || !buf || !out) return SYSCALL_EFAULT;
    if (!is_user_range(request, sizeof(*request)) ||
        !is_user_range(buf, len) ||
        !is_user_range(out, sizeof(*out))) {
        return SYSCALL_EFAULT;
    }

    memcpy(&kernel_request, request, sizeof(kernel_request));
    rc = net_http_get_ipv4(&kernel_request, buf, len, &kernel_result);
    if (rc < 0) return SYSCALL_EINVAL;

    struct numos_net_http_result user_result;
    memset(&user_result, 0, sizeof(user_result));
    memcpy(&user_result, &kernel_result, sizeof(user_result));
    memcpy(out, &user_result, sizeof(user_result));
    return rc;
}

/* =========================================================================
 * Dispatcher
 * ======================================================================= */

int64_t syscall_dispatch(struct syscall_regs *regs) {
    uint64_t nr  = regs->rax;
    int64_t  ret = SYSCALL_ENOSYS;

    stats.total_calls++;
    if (nr < SYSCALL_MAX) stats.calls_per_number[nr]++;
    else                  stats.unknown_calls++;

    __asm__ volatile("sti");

    switch ((int)nr) {
        case SYS_READ:
            ret = sys_read((int)regs->rdi, (void*)regs->rsi, (size_t)regs->rdx);
            break;
        case SYS_INPUT:
            ret = sys_input((void*)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_INPUT_PEEK:
            ret = sys_input_peek((char*)regs->rdi);
            break;
        case SYS_YIELD:
            ret = sys_yield();
            break;
        case SYS_WRITE:
            ret = sys_write((int)regs->rdi, (const void*)regs->rsi, (size_t)regs->rdx);
            break;
        case SYS_OPEN:
            ret = sys_open((const char*)regs->rdi, (int)regs->rsi, (int)regs->rdx);
            break;
        case SYS_EXEC:
            ret = sys_exec((const char*)regs->rdi);
            break;
        case SYS_EXEC_ARGV:
            ret = sys_exec_argv((const char*)regs->rdi, (const char*)regs->rsi);
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
        case SYS_SYSINFO:
            ret = sys_sysinfo((struct sysinfo *)regs->rdi);
            break;
        case SYS_HWINFO:
            ret = sys_hwinfo((struct hwinfo *)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_PUTS:
            ret = sys_puts((const char*)regs->rdi);
            break;
        case SYS_GET_CMDLINE:
            ret = sys_get_cmdline((char*)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_LISTDIR:
            ret = sys_listdir((const char*)regs->rdi,
                              (struct fat32_dirent *)regs->rsi,
                              (int)regs->rdx);
            break;
        case SYS_PROCLIST:
            ret = sys_proclist((struct proc_info *)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_TIME_READ:
            ret = sys_time_read((struct numos_calendar_time *)regs->rdi);
            break;
        case SYS_TIMER_CREATE:
            ret = sys_timer_create(regs->rdi, regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_TIMER_WAIT:
            ret = sys_timer_wait((int)regs->rdi);
            break;
        case SYS_TIMER_INFO:
            ret = sys_timer_info((int)regs->rdi,
                                 (struct numos_timer_info *)regs->rsi);
            break;
        case SYS_TIMER_CANCEL:
            ret = sys_timer_cancel((int)regs->rdi);
            break;
        case SYS_CON_SCROLL:
            ret = sys_con_scroll();
            break;
        case SYS_DISK_INFO:
            ret = sys_disk_info((struct numos_disk_info *)regs->rdi);
            break;
        case SYS_DISK_READ:
            ret = sys_disk_read(regs->rdi, (void *)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_DISK_WRITE:
            ret = sys_disk_write(regs->rdi, (const void *)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_USB_CONTROLLER_COUNT:
            ret = sys_usb_controller_count();
            break;
        case SYS_USB_CONTROLLER_INFO:
            ret = sys_usb_controller_info((int)regs->rdi,
                                          (struct numos_usb_controller_info *)regs->rsi);
            break;
        case SYS_USB_PORT_INFO:
            ret = sys_usb_port_info((int)regs->rdi, (int)regs->rsi,
                                    (struct numos_usb_port_info *)regs->rdx);
            break;
        case SYS_THREAD_CREATE:
            ret = sys_thread_create((void *)regs->rdi, (void *)regs->rsi,
                                    (void *)regs->rdx);
            break;
        case SYS_THREAD_JOIN:
            ret = sys_thread_join((int)regs->rdi, (uint64_t *)regs->rsi);
            break;
        case SYS_THREAD_EXIT:
            ret = sys_thread_exit(regs->rdi);
            break;
        case SYS_THREAD_SELF:
            ret = sys_thread_self();
            break;
        case SYS_NET_INFO:
            ret = sys_net_info((struct numos_net_info *)regs->rdi);
            break;
        case SYS_NET_DHCP:
            ret = sys_net_dhcp((uint32_t)regs->rdi);
            break;
        case SYS_NET_PING:
            ret = sys_net_ping((const uint8_t *)regs->rdi,
                               (uint32_t)regs->rsi,
                               (struct numos_net_ping_result *)regs->rdx);
            break;
        case SYS_NET_TCP_CONNECT:
            ret = sys_net_tcp_connect((const uint8_t *)regs->rdi,
                                      (uint16_t)regs->rsi,
                                      (uint32_t)regs->rdx);
            break;
        case SYS_NET_TCP_SEND:
            ret = sys_net_tcp_send((int)regs->rdi,
                                   (const void *)regs->rsi,
                                   (size_t)regs->rdx,
                                   (uint32_t)regs->r10);
            break;
        case SYS_NET_TCP_RECV:
            ret = sys_net_tcp_recv((int)regs->rdi,
                                   (void *)regs->rsi,
                                   (size_t)regs->rdx,
                                   (uint32_t)regs->r10);
            break;
        case SYS_NET_TCP_CLOSE:
            ret = sys_net_tcp_close((int)regs->rdi, (uint32_t)regs->rsi);
            break;
        case SYS_NET_TCP_INFO:
            ret = sys_net_tcp_info((int)regs->rdi,
                                   (struct numos_net_tcp_info *)regs->rsi);
            break;
        case SYS_NET_TLS_PROBE:
            ret = sys_net_tls_probe((const uint8_t *)regs->rdi,
                                    (uint16_t)regs->rsi,
                                    (const char *)regs->rdx,
                                    (uint32_t)regs->r10,
                                    (uint32_t)regs->r8,
                                    (struct numos_net_tls_result *)regs->r9);
            break;
        case SYS_NET_HTTP_GET:
            ret = sys_net_http_get((const struct numos_net_http_request *)regs->rdi,
                                   (void *)regs->rsi,
                                   (size_t)regs->rdx,
                                   (struct numos_net_http_result *)regs->r10);
            break;
        case SYS_POWEROFF:
            ret = sys_poweroff();
            break;

        /* Framebuffer syscalls */
        case SYS_FB_INFO:
            ret = sys_fb_info(regs->rdi);
            break;
        case SYS_FB_WRITE:
            ret = sys_fb_write((const char*)regs->rdi, (size_t)regs->rsi);
            break;
        case SYS_FB_CLEAR:
            ret = sys_fb_clear();
            break;
        case SYS_FB_SETCOLOR:
            ret = sys_fb_setcolor((uint32_t)regs->rdi, (uint32_t)regs->rsi);
            break;
        case SYS_FB_SETPIXEL:
            ret = sys_fb_setpixel((int)regs->rdi, (int)regs->rsi, (uint32_t)regs->rdx);
            break;
        case SYS_FB_FILLRECT:
            ret = sys_fb_fillrect((int)regs->rdi, (int)regs->rsi,
                                  (int)regs->rdx, (int)regs->r10,
                                  (uint32_t)regs->r8);
            break;
        case SYS_REBOOT:
            ret = sys_reboot();
            break;

        default:
            stats.errors++;
            ret = SYSCALL_ENOSYS;
            break;
    }

    __asm__ volatile("cli");
    regs->rax = (uint64_t)ret;
    return ret;
}

/* =========================================================================
 * Diagnostics
 * ======================================================================= */

void syscall_print_stats(void) {
    static const char *names[SYSCALL_MAX];
    for (int i = 0; i < SYSCALL_MAX; i++) names[i] = NULL;
    names[SYS_READ]      = "read";
    names[SYS_INPUT]     = "input";
    names[SYS_INPUT_PEEK]= "input_peek";
    names[SYS_YIELD]     = "yield";
    names[SYS_WRITE]     = "write";
    names[SYS_OPEN]      = "open";
    names[SYS_EXEC]      = "exec";
    names[SYS_EXEC_ARGV] = "exec_argv";
    names[SYS_CLOSE]     = "close";
    names[SYS_EXIT]      = "exit";
    names[SYS_GETPID]    = "getpid";
    names[SYS_SLEEP_MS]  = "sleep_ms";
    names[SYS_UPTIME_MS] = "uptime_ms";
    names[SYS_SYSINFO]   = "sysinfo";
    names[SYS_HWINFO]    = "hwinfo";
    names[SYS_PUTS]      = "puts";
    names[SYS_GET_CMDLINE] = "get_cmdline";
    names[SYS_LISTDIR]     = "listdir";
    names[SYS_PROCLIST]    = "proclist";
    names[SYS_TIME_READ]   = "time_read";
    names[SYS_TIMER_CREATE]= "timer_create";
    names[SYS_TIMER_WAIT]  = "timer_wait";
    names[SYS_TIMER_INFO]  = "timer_info";
    names[SYS_TIMER_CANCEL]= "timer_cancel";
    names[SYS_CON_SCROLL]  = "con_scroll";
    names[SYS_DISK_INFO]   = "disk_info";
    names[SYS_DISK_READ]   = "disk_read";
    names[SYS_DISK_WRITE]  = "disk_write";
    names[SYS_USB_CONTROLLER_COUNT] = "usb_controller_count";
    names[SYS_USB_CONTROLLER_INFO]  = "usb_controller_info";
    names[SYS_USB_PORT_INFO]        = "usb_port_info";
    names[SYS_THREAD_CREATE]        = "thread_create";
    names[SYS_THREAD_JOIN]          = "thread_join";
    names[SYS_THREAD_EXIT]          = "thread_exit";
    names[SYS_THREAD_SELF]          = "thread_self";
    names[SYS_NET_INFO]             = "net_info";
    names[SYS_NET_DHCP]             = "net_dhcp";
    names[SYS_NET_PING]             = "net_ping";
    names[SYS_NET_TCP_CONNECT]      = "net_tcp_connect";
    names[SYS_NET_TCP_SEND]         = "net_tcp_send";
    names[SYS_NET_TCP_RECV]         = "net_tcp_recv";
    names[SYS_NET_TCP_CLOSE]        = "net_tcp_close";
    names[SYS_NET_TCP_INFO]         = "net_tcp_info";
    names[SYS_NET_TLS_PROBE]        = "net_tls_probe";
    names[SYS_NET_HTTP_GET]         = "net_http_get";
    names[SYS_POWEROFF]             = "poweroff";
    names[SYS_REBOOT]    = "reboot";
    names[SYS_FB_INFO]   = "fb_info";
    names[SYS_FB_WRITE]  = "fb_write";
    names[SYS_FB_CLEAR]  = "fb_clear";
    names[SYS_FB_SETCOLOR]= "fb_setcolor";
    names[SYS_FB_SETPIXEL]= "fb_setpixel";
    names[SYS_FB_FILLRECT]= "fb_fillrect";

    vga_writestring("\nSyscall Statistics:\n");
    vga_writestring("  Total: "); print_dec(stats.total_calls);
    vga_writestring("  Errors: "); print_dec(stats.errors); vga_writestring("\n");
    for (int i = 0; i < SYSCALL_MAX; i++) {
        if (!stats.calls_per_number[i]) continue;
        vga_writestring("  ["); print_dec((uint64_t)i); vga_writestring("] ");
        vga_writestring(names[i] ? names[i] : "?");
        vga_writestring(": "); print_dec(stats.calls_per_number[i]);
        vga_writestring("\n");
    }
}
