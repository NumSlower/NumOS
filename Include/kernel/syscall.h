#ifndef SYSCALL_H
#define SYSCALL_H

#include "lib/base.h"
#include "drivers/timer.h"
#include "kernel/sysinfo.h"
#include "kernel/hwinfo.h"
#include "kernel/procinfo.h"

/* =========================================================================
 * NumOS Syscall ABI  (x86-64, Linux-compatible numbering)
 *
 * Calling convention:
 *   rax = syscall number (in) / return value (out)
 *   rdi = arg1,  rsi = arg2,  rdx = arg3
 *   r10 = arg4,  r8  = arg5,  r9  = arg6
 * ========================================================================= */

/* Standard syscalls */
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
#define SYS_SLEEP_MS    35
#define SYS_GETPID      39
#define SYS_EXIT        60
#define SYS_UPTIME_MS   96
#define SYS_SYSINFO     99
#define SYS_HWINFO      100
#define SYS_REBOOT      169
#define SYS_PUTS        200
/* Keyboard input (custom). arg1=buf, arg2=count */
#define SYS_INPUT       207
/* Execute a user ELF from the FAT32 volume. arg1=path */
#define SYS_EXEC        208
/* Execute a user ELF with a command line. arg1=path, arg2=cmdline */
#define SYS_EXEC_ARGV   209
/* Copy the current process cmdline into a user buffer. arg1=buf, arg2=len */
#define SYS_GET_CMDLINE 210
/* List directory entries into a user buffer. arg1=path, arg2=buf, arg3=max */
#define SYS_LISTDIR     211
/* List processes into a user buffer. arg1=buf, arg2=max */
#define SYS_PROCLIST    212
/* Non-blocking keyboard read. arg1=buf */
#define SYS_INPUT_PEEK  213
#define SYS_YIELD       214
/* Read RTC-backed wall clock. arg1=struct numos_calendar_time* */
#define SYS_TIME_READ   215
/* Create timer object. arg1=delay_ms, arg2=period_ms, arg3=flags */
#define SYS_TIMER_CREATE 216
/* Wait for timer object. arg1=timer_id */
#define SYS_TIMER_WAIT  217
/* Query timer object. arg1=timer_id, arg2=struct numos_timer_info* */
#define SYS_TIMER_INFO  218
/* Cancel timer object. arg1=timer_id */
#define SYS_TIMER_CANCEL 219
/* Enter console scrollback mode. */
#define SYS_CON_SCROLL  220
/* Raw primary ATA disk info. arg1=struct numos_disk_info* */
#define SYS_DISK_INFO   221
/* Raw primary ATA sector read. arg1=lba, arg2=buf, arg3=sector_count */
#define SYS_DISK_READ   222
/* Raw primary ATA sector write. arg1=lba, arg2=buf, arg3=sector_count */
#define SYS_DISK_WRITE  223
#define SYS_USB_CONTROLLER_COUNT 224
#define SYS_USB_CONTROLLER_INFO  225
#define SYS_USB_PORT_INFO        226
#define SYS_THREAD_CREATE        227
#define SYS_THREAD_JOIN          228
#define SYS_THREAD_EXIT          229
#define SYS_THREAD_SELF          230
#define SYS_NET_INFO             231
#define SYS_NET_DHCP             232
#define SYS_NET_PING             233
#define SYS_POWEROFF             234
#define SYS_NET_TCP_CONNECT      235
#define SYS_NET_TCP_SEND         236
#define SYS_NET_TCP_RECV         237
#define SYS_NET_TCP_CLOSE        238
#define SYS_NET_TCP_INFO         239
#define SYS_NET_TLS_PROBE        240
#define SYS_NET_HTTP_GET         241

/* ---- Framebuffer syscalls -----------------------------------------------
 *
 * When the BGA framebuffer is not active these return SYSCALL_ENOSYS
 * (except SYS_FB_INFO which returns 0 for field 3 / "available").
 *
 * SYS_FB_INFO   (201)
 *   arg1 = field selector:
 *     0 → framebuffer width  in pixels
 *     1 → framebuffer height in pixels
 *     2 → bits per pixel (always 32)
 *     3 → 1 if BGA is active, 0 otherwise
 *   returns: the requested value
 *
 * SYS_FB_WRITE  (202)
 *   arg1 = pointer to char buffer
 *   arg2 = byte count
 *   returns: bytes written, or negative errno
 *   NOTE: sys_write(fd=1, ...) also routes here when FB is active, so
 *   existing user-space programs need no changes.
 *
 * SYS_FB_CLEAR  (203)
 *   Clears the framebuffer console.  returns 0.
 *
 * SYS_FB_SETCOLOR (204)
 *   arg1 = foreground colour  0x00RRGGBB
 *   arg2 = background colour  0x00RRGGBB  (pass 0xFFFFFFFF for transparent)
 *   returns 0
 *
 * SYS_FB_SETPIXEL (205)
 *   arg1 = x,  arg2 = y,  arg3 = colour 0x00RRGGBB
 *   returns 0
 *
 * SYS_FB_FILLRECT (206)
 *   arg1=x, arg2=y, arg3=w, arg4(r10)=h, arg5(r8)=colour
 *   returns 0
 * -------------------------------------------------------------------- */
#define SYS_FB_INFO     201
#define SYS_FB_WRITE    202
#define SYS_FB_CLEAR    203
#define SYS_FB_SETCOLOR 204
#define SYS_FB_SETPIXEL 205
#define SYS_FB_FILLRECT 206

#define SYSCALL_MAX     256

/* Well-known file descriptors */
#define FD_STDIN    0
#define FD_STDOUT   1
#define FD_STDERR   2

/* Return value conventions */
#define SYSCALL_SUCCESS   0
#define SYSCALL_EBADF   (-9)
#define SYSCALL_ENOMEM  (-12)
#define SYSCALL_EFAULT  (-14)
#define SYSCALL_EINVAL  (-22)
#define SYSCALL_ENOSYS  (-38)

/* Saved CPU state at syscall entry */
struct syscall_regs {
    uint64_t rax;
    uint64_t rdi;
    uint64_t rsi;
    uint64_t rdx;
    uint64_t r10;
    uint64_t r8;
    uint64_t r9;
    uint64_t rcx;
    uint64_t r11;
    uint64_t rbx;
    uint64_t rbp;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
    uint64_t rsp;
};

struct syscall_stats {
    uint64_t total_calls;
    uint64_t calls_per_number[SYSCALL_MAX];
    uint64_t unknown_calls;
    uint64_t errors;
};

struct fat32_dirent;

#define NUMOS_DISK_MODEL_LEN 41

struct numos_disk_info {
    uint64_t sector_count;
    uint32_t sector_size;
    uint32_t present;
    uint32_t writable;
    char     model[NUMOS_DISK_MODEL_LEN];
};

struct numos_usb_controller_info {
    uint8_t  present;
    uint8_t  host_type;
    uint8_t  port_count;
    uint8_t  irq_line;
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint8_t  reserved;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t io_base;
    char     name[48];
};

struct numos_usb_port_info {
    uint8_t  present;
    uint8_t  connected;
    uint8_t  enabled;
    uint8_t  low_speed;
    uint8_t  owner;
    uint8_t  powered;
    uint8_t  reset;
    uint8_t  reserved;
    uint16_t status_raw;
};

struct numos_net_info {
    uint8_t  present;
    uint8_t  link_up;
    uint8_t  dhcp_configured;
    uint8_t  reserved0;
    uint8_t  mac[6];
    uint8_t  ipv4[4];
    uint8_t  netmask[4];
    uint8_t  gateway[4];
    uint8_t  dhcp_server[4];
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint8_t  reserved1;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    char     driver[32];
    char     interface_name[48];
};

struct numos_net_ping_result {
    uint8_t  success;
    uint8_t  ttl;
    uint16_t seq;
    uint32_t roundtrip_ms;
};

struct numos_net_tcp_info {
    uint8_t  state;
    uint8_t  reset;
    uint8_t  remote_closed;
    uint8_t  reserved0;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t recv_ready;
    uint32_t send_ready;
    uint8_t  remote_ip[4];
};

struct numos_net_tls_result {
    uint8_t  success;
    uint8_t  secure;
    uint16_t protocol_version;
    uint16_t cipher_suite;
    uint16_t remote_port;
    uint8_t  remote_ip[4];
    char     server_name[64];
};

struct numos_net_http_request {
    uint8_t  remote_ip[4];
    uint16_t remote_port;
    uint16_t secure;
    uint32_t flags;
    uint32_t timeout_ms;
    char     host[64];
    char     path[192];
};

struct numos_net_http_result {
    uint16_t status_code;
    uint16_t protocol_version;
    uint16_t cipher_suite;
    uint16_t remote_port;
    uint8_t  secure;
    uint8_t  truncated;
    uint8_t  headers_included;
    uint8_t  reserved0;
    uint32_t bytes_received;
    uint32_t body_offset;
    uint8_t  remote_ip[4];
    char     content_type[64];
    char     location[192];
};

void    syscall_init(void);
int64_t syscall_dispatch(struct syscall_regs *regs);

int64_t sys_read(int fd, void *buf, size_t count);
int64_t sys_write(int fd, const void *buf, size_t count);
int64_t sys_open(const char *path, int flags, int mode);
int64_t sys_close(int fd);
int64_t sys_exit(int status);
int64_t sys_getpid(void);
int64_t sys_sleep_ms(uint64_t ms);
int64_t sys_uptime_ms(void);
int64_t sys_sysinfo(struct sysinfo *info);
int64_t sys_hwinfo(struct hwinfo *info, size_t len);
int64_t sys_puts(const char *str);
int64_t sys_input(void *buf, size_t count);
int64_t sys_input_peek(char *out);
int64_t sys_reboot(void);
int64_t sys_exec(const char *path);
int64_t sys_exec_argv(const char *path, const char *cmdline);
int64_t sys_get_cmdline(char *buf, size_t len);
int64_t sys_listdir(const char *path, struct fat32_dirent *entries, int max_entries);
int64_t sys_proclist(struct proc_info *out, size_t max);
int64_t sys_yield(void);
int64_t sys_time_read(struct numos_calendar_time *out);
int64_t sys_timer_create(uint64_t delay_ms, uint64_t period_ms, uint32_t flags);
int64_t sys_timer_wait(int timer_id);
int64_t sys_timer_info(int timer_id, struct numos_timer_info *out);
int64_t sys_timer_cancel(int timer_id);
int64_t sys_con_scroll(void);
int64_t sys_disk_info(struct numos_disk_info *out);
int64_t sys_disk_read(uint64_t lba, void *buf, uint32_t sector_count);
int64_t sys_disk_write(uint64_t lba, const void *buf, uint32_t sector_count);
int64_t sys_usb_controller_count(void);
int64_t sys_usb_controller_info(int index, struct numos_usb_controller_info *out);
int64_t sys_usb_port_info(int controller_index, int port_index,
                          struct numos_usb_port_info *out);
int64_t sys_thread_create(void *start, void *arg, void *trampoline);
int64_t sys_thread_join(int tid, uint64_t *out_value);
int64_t sys_thread_exit(uint64_t value);
int64_t sys_thread_self(void);
int64_t sys_net_info(struct numos_net_info *out);
int64_t sys_net_dhcp(uint32_t timeout_ms);
int64_t sys_net_ping(const uint8_t *ipv4, uint32_t timeout_ms,
                     struct numos_net_ping_result *out);
int64_t sys_net_tcp_connect(const uint8_t *ipv4, uint16_t port, uint32_t timeout_ms);
int64_t sys_net_tcp_send(int handle, const void *buf, size_t len, uint32_t timeout_ms);
int64_t sys_net_tcp_recv(int handle, void *buf, size_t len, uint32_t timeout_ms);
int64_t sys_net_tcp_close(int handle, uint32_t timeout_ms);
int64_t sys_net_tcp_info(int handle, struct numos_net_tcp_info *out);
int64_t sys_net_tls_probe(const uint8_t *ipv4,
                          uint16_t port,
                          const char *server_name,
                          uint32_t flags,
                          uint32_t timeout_ms,
                          struct numos_net_tls_result *out);
int64_t sys_net_http_get(const struct numos_net_http_request *request,
                         void *buf,
                         size_t len,
                         struct numos_net_http_result *out);
int64_t sys_poweroff(void);

/* Framebuffer syscall implementations */
int64_t sys_fb_info(uint64_t field);
int64_t sys_fb_write(const char *buf, size_t len);
int64_t sys_fb_clear(void);
int64_t sys_fb_setcolor(uint32_t fg, uint32_t bg);
int64_t sys_fb_setpixel(int x, int y, uint32_t color);
int64_t sys_fb_fillrect(int x, int y, int w, int h, uint32_t color);

void syscall_print_stats(void);

extern void syscall_entry(void);

#endif /* SYSCALL_H */
