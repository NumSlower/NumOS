#ifndef NUMOS_USER_SYSCALLS_H
#define NUMOS_USER_SYSCALLS_H

typedef unsigned long  uint64_t;
typedef long           int64_t;
typedef unsigned int   uint32_t;
typedef int            int32_t;
typedef unsigned short uint16_t;
typedef unsigned char  uint8_t;
typedef unsigned long  size_t;
typedef long           ssize_t;
typedef unsigned long  uintptr_t;
typedef long           intptr_t;

/* FAT32 directory entry for SYS_LISTDIR */
#define FAT32_MAX_FILENAME 255
#define FAT32_ATTR_DIRECTORY 0x10
struct fat32_dirent {
    char name[FAT32_MAX_FILENAME];
    uint32_t size;
    uint8_t attr;
    uint32_t cluster;
};

/* Hardware info (SYS_HWINFO) */
#define NUMOS_HWINFO_VERSION 1
#define HWINFO_STR_VENDOR_LEN 16
#define HWINFO_STR_NAME_LEN   32

enum hwinfo_form_factor {
    HWINFO_FORM_UNKNOWN = 0,
    HWINFO_FORM_DESKTOP = 1,
    HWINFO_FORM_LAPTOP  = 2,
    HWINFO_FORM_SERVER  = 3,
    HWINFO_FORM_VM      = 4
};

enum hwinfo_power_source {
    HWINFO_POWER_UNKNOWN = 0,
    HWINFO_POWER_AC      = 1,
    HWINFO_POWER_BATTERY = 2
};

enum hwinfo_battery_state {
    HWINFO_BATTERY_NONE        = 0,
    HWINFO_BATTERY_PRESENT     = 1 << 0,
    HWINFO_BATTERY_CHARGING    = 1 << 1,
    HWINFO_BATTERY_DISCHARGING = 1 << 2
};

enum hwinfo_flags {
    HWINFO_HAS_FORM_FACTOR = 1 << 0,
    HWINFO_HAS_POWER       = 1 << 1,
    HWINFO_HAS_BATTERY     = 1 << 2,
    HWINFO_HAS_CPU         = 1 << 3,
    HWINFO_HAS_MEMORY      = 1 << 4,
    HWINFO_HAS_HYPERVISOR  = 1 << 5,
    HWINFO_HAS_HEAP        = 1 << 6,
    HWINFO_HAS_UPTIME      = 1 << 7,
    HWINFO_HAS_PROCESSES   = 1 << 8
};

struct hwinfo {
    uint32_t size;
    uint32_t version;
    uint32_t flags;
    uint32_t form_factor;
    uint32_t power_source;
    int32_t  battery_percent;
    uint32_t battery_state;
    uint32_t cpu_count;
    uint32_t cpu_mhz;
    uint64_t mem_total;
    uint64_t mem_free;
    uint64_t heap_total;
    uint64_t heap_used;
    uint64_t uptime_ms;
    uint32_t process_count;
    uint32_t process_max;
    char     cpu_vendor[HWINFO_STR_VENDOR_LEN];
    char     hypervisor[HWINFO_STR_NAME_LEN];
    char     machine[HWINFO_STR_NAME_LEN];
};

/* Process info (SYS_PROCLIST) */
#define PROCINFO_NAME_LEN 32
struct proc_info {
    int      pid;
    int      state;
    uint32_t flags;
    uint64_t total_ticks;
    uint64_t created_at_ms;
    uint64_t load_base;
    uint64_t load_end;
    uint64_t memory_bytes;
    char     name[PROCINFO_NAME_LEN];
};

struct numos_calendar_time {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
    uint8_t  valid;
    uint64_t uptime_ms;
};

struct numos_timer_info {
    int32_t  id;
    uint32_t flags;
    uint64_t deadline_ms;
    uint64_t period_ms;
    uint64_t remaining_ms;
};

#define NUMOS_DISK_MODEL_LEN 41

struct numos_disk_info {
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t present;
    uint32_t writable;
    char     model[NUMOS_DISK_MODEL_LEN];
};

#define NUMOS_TIMER_PERIODIC 0x01u

/* Syscall numbers */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_SLEEP_MS    35
#define SYS_GETPID      39
#define SYS_EXIT        60
#define SYS_UPTIME_MS   96
#define SYS_SYSINFO     99
#define SYS_HWINFO      100
#define SYS_REBOOT      169
#define SYS_PUTS        200
#define SYS_FB_INFO     201
#define SYS_FB_WRITE    202
#define SYS_FB_CLEAR    203
#define SYS_FB_SETCOLOR 204
#define SYS_FB_SETPIXEL 205
#define SYS_FB_FILLRECT 206
#define SYS_INPUT       207
#define SYS_EXEC        208
#define SYS_EXEC_ARGV   209
#define SYS_GET_CMDLINE 210
#define SYS_LISTDIR     211
#define SYS_PROCLIST    212
#define SYS_INPUT_PEEK  213
#define SYS_YIELD       214
#define SYS_TIME_READ   215
#define SYS_TIMER_CREATE 216
#define SYS_TIMER_WAIT  217
#define SYS_TIMER_INFO  218
#define SYS_TIMER_CANCEL 219
#define SYS_CON_SCROLL 220
#define SYS_DISK_INFO  221
#define SYS_DISK_READ  222
#define SYS_DISK_WRITE 223

/* Special key codes returned by SYS_INPUT and SYS_INPUT_PEEK. */
#define KEY_SPECIAL_UP    '\x01'
#define KEY_SPECIAL_DOWN  '\x02'
#define KEY_SPECIAL_LEFT  '\x03'
#define KEY_SPECIAL_RIGHT '\x04'

/* File descriptors */
#define FD_STDIN   0
#define FD_STDOUT  1
#define FD_STDERR  2

/* FAT32 open flags */
#define FAT32_O_RDONLY      0x01
#define FAT32_O_WRONLY      0x02
#define FAT32_O_RDWR        0x03
#define FAT32_O_CREAT       0x04
#define FAT32_O_TRUNC       0x08
#define FAT32_O_APPEND      0x10

static inline int64_t sys_call0(int64_t n) {
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_call1(int64_t n, int64_t a1) {
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_call2(int64_t n, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_call3(int64_t n, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_call4(int64_t n, int64_t a1, int64_t a2,
                                int64_t a3, int64_t a4) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_call5(int64_t n, int64_t a1, int64_t a2,
                                int64_t a3, int64_t a4, int64_t a5) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a4;
    register int64_t r8  __asm__("r8")  = a5;
    __asm__ volatile("syscall"
                     : "=a"(ret)
                     : "a"(n), "D"(a1), "S"(a2), "d"(a3), "r"(r10), "r"(r8)
                     : "rcx", "r11", "memory");
    return ret;
}

static inline int64_t sys_read(int fd, void *buf, size_t count) {
    return sys_call3(SYS_READ, fd, (int64_t)buf, (int64_t)count);
}

static inline int64_t sys_input(void *buf, size_t count) {
    return sys_call2(SYS_INPUT, (int64_t)buf, (int64_t)count);
}

static inline int64_t sys_input_peek(char *out) {
    return sys_call1(SYS_INPUT_PEEK, (int64_t)out);
}

static inline int64_t sys_write(int fd, const void *buf, size_t count) {
    return sys_call3(SYS_WRITE, fd, (int64_t)buf, (int64_t)count);
}

static inline int64_t sys_open(const char *path, int flags, int mode) {
    return sys_call3(SYS_OPEN, (int64_t)path, flags, mode);
}

static inline int64_t sys_close(int fd) {
    return sys_call1(SYS_CLOSE, fd);
}

static inline int64_t sys_exit(int status) {
    return sys_call1(SYS_EXIT, status);
}

static inline int64_t sys_getpid(void) {
    return sys_call0(SYS_GETPID);
}

static inline int64_t sys_sleep_ms(uint64_t ms) {
    return sys_call1(SYS_SLEEP_MS, (int64_t)ms);
}

static inline int64_t sys_con_scroll(void) {
    return sys_call0(SYS_CON_SCROLL);
}

static inline int64_t sys_uptime_ms(void) {
    return sys_call0(SYS_UPTIME_MS);
}

static inline int64_t sys_sysinfo(void *info) {
    return sys_call1(SYS_SYSINFO, (int64_t)info);
}

static inline int64_t sys_hwinfo(void *info, size_t len) {
    return sys_call2(SYS_HWINFO, (int64_t)info, (int64_t)len);
}

static inline int64_t sys_reboot(void) {
    return sys_call0(SYS_REBOOT);
}

static inline int64_t sys_exec(const char *path) {
    return sys_call1(SYS_EXEC, (int64_t)path);
}

static inline int64_t sys_exec_argv(const char *path, const char *cmdline) {
    return sys_call2(SYS_EXEC_ARGV, (int64_t)path, (int64_t)cmdline);
}

static inline int64_t sys_get_cmdline(char *buf, size_t len) {
    return sys_call2(SYS_GET_CMDLINE, (int64_t)buf, (int64_t)len);
}

static inline int64_t sys_listdir(const char *path, struct fat32_dirent *entries,
                                  int max_entries) {
    return sys_call3(SYS_LISTDIR, (int64_t)path, (int64_t)entries,
                     (int64_t)max_entries);
}

static inline int64_t sys_proclist(struct proc_info *out, size_t max) {
    return sys_call2(SYS_PROCLIST, (int64_t)out, (int64_t)max);
}

static inline int64_t sys_fb_info(uint64_t field) {
    return sys_call1(SYS_FB_INFO, (int64_t)field);
}

static inline int64_t sys_fb_write(const char *buf, size_t len) {
    return sys_call2(SYS_FB_WRITE, (int64_t)buf, (int64_t)len);
}

static inline int64_t sys_fb_clear(void) {
    return sys_call0(SYS_FB_CLEAR);
}

static inline int64_t sys_fb_setcolor(uint32_t fg, uint32_t bg) {
    return sys_call2(SYS_FB_SETCOLOR, (int64_t)fg, (int64_t)bg);
}

static inline int64_t sys_fb_setpixel(int x, int y, uint32_t color) {
    return sys_call3(SYS_FB_SETPIXEL, x, y, (int64_t)color);
}

static inline int64_t sys_fb_fillrect(int x, int y, int w, int h,
                                      uint32_t color) {
    return sys_call5(SYS_FB_FILLRECT, x, y, w, h, (int64_t)color);
}

static inline int64_t sys_yield(void) {
    return sys_call0(SYS_YIELD);
}

static inline int64_t sys_time_read(struct numos_calendar_time *out) {
    return sys_call1(SYS_TIME_READ, (int64_t)out);
}

static inline int64_t sys_timer_create(uint64_t delay_ms, uint64_t period_ms,
                                       uint32_t flags) {
    return sys_call3(SYS_TIMER_CREATE, (int64_t)delay_ms,
                     (int64_t)period_ms, (int64_t)flags);
}

static inline int64_t sys_timer_wait(int timer_id) {
    return sys_call1(SYS_TIMER_WAIT, timer_id);
}

static inline int64_t sys_timer_info(int timer_id,
                                     struct numos_timer_info *out) {
    return sys_call2(SYS_TIMER_INFO, timer_id, (int64_t)out);
}

static inline int64_t sys_timer_cancel(int timer_id) {
    return sys_call1(SYS_TIMER_CANCEL, timer_id);
}

static inline int64_t sys_disk_info(struct numos_disk_info *out) {
    return sys_call1(SYS_DISK_INFO, (int64_t)out);
}

static inline int64_t sys_disk_read(uint64_t lba, void *buf, uint32_t sector_count) {
    return sys_call3(SYS_DISK_READ, (int64_t)lba, (int64_t)buf, (int64_t)sector_count);
}

static inline int64_t sys_disk_write(uint64_t lba, const void *buf, uint32_t sector_count) {
    return sys_call3(SYS_DISK_WRITE, (int64_t)lba, (int64_t)buf, (int64_t)sector_count);
}

#endif /* NUMOS_USER_SYSCALLS_H */
