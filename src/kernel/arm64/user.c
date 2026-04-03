#include "kernel/kernel.h"
#include "kernel/arm64.h"
#include "kernel/elf_loader.h"
#include "drivers/serial.h"
#include "drivers/framebuffer.h"
#include "drivers/graphices/vga.h"
#include "drivers/timer.h"
#include "cpu/paging.h"
#include "fs/fat32.h"
#include "fs/vfs.h"

#define ARM64_SPSR_MODE_EL0T 0x0ULL
#define ARM64_SPSR_MODE_EL1H 0x5ULL
#define ARM64_ESR_EC_SHIFT   26
#define ARM64_ESR_EC_MASK    0x3Fu
#define ARM64_ESR_EC_SVC64   0x15u

#define ARM64_SYS_READ        0
#define ARM64_SYS_WRITE       1
#define ARM64_SYS_OPEN        2
#define ARM64_SYS_CLOSE       3
#define ARM64_SYS_SLEEP_MS    35
#define ARM64_SYS_EXIT        60
#define ARM64_SYS_UPTIME_MS   96
#define ARM64_SYS_FB_INFO     201
#define ARM64_SYS_INPUT       207
#define ARM64_SYS_GET_CMDLINE 210

#define ARM64_FB_FIELD_WIDTH  0
#define ARM64_FB_FIELD_HEIGHT 1
#define ARM64_FB_FIELD_BPP    2

struct arm64_user_state {
    int active;
    int exited;
    int exit_code;
    char cmdline[256];
    struct elf_load_result image;
};

static struct arm64_user_state user_state;
uint64_t arm64_kernel_resume_sp = 0;
uint64_t arm64_kernel_resume_x19 = 0;
uint64_t arm64_kernel_resume_x20 = 0;
uint64_t arm64_kernel_resume_x21 = 0;
uint64_t arm64_kernel_resume_x22 = 0;
uint64_t arm64_kernel_resume_x23 = 0;
uint64_t arm64_kernel_resume_x24 = 0;
uint64_t arm64_kernel_resume_x25 = 0;
uint64_t arm64_kernel_resume_x26 = 0;
uint64_t arm64_kernel_resume_x27 = 0;
uint64_t arm64_kernel_resume_x28 = 0;
uint64_t arm64_kernel_resume_x29 = 0;
uint64_t arm64_kernel_resume_lr = 0;
uint64_t arm64_user_exit_value = 0;

extern void arm64_user_exit_trampoline(void);

__asm__(
".text\n"
".global arm64_user_exit_trampoline\n"
"arm64_user_exit_trampoline:\n"
"    adrp x9, arm64_user_exit_value\n"
"    add  x9, x9, :lo12:arm64_user_exit_value\n"
"    ldr  x0, [x9]\n"
"    adrp x9, arm64_kernel_resume_sp\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_sp\n"
"    ldr  x10, [x9]\n"
"    mov  sp, x10\n"
"    adrp x9, arm64_kernel_resume_x19\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x19\n"
"    ldr  x19, [x9]\n"
"    adrp x9, arm64_kernel_resume_x20\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x20\n"
"    ldr  x20, [x9]\n"
"    adrp x9, arm64_kernel_resume_x21\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x21\n"
"    ldr  x21, [x9]\n"
"    adrp x9, arm64_kernel_resume_x22\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x22\n"
"    ldr  x22, [x9]\n"
"    adrp x9, arm64_kernel_resume_x23\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x23\n"
"    ldr  x23, [x9]\n"
"    adrp x9, arm64_kernel_resume_x24\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x24\n"
"    ldr  x24, [x9]\n"
"    adrp x9, arm64_kernel_resume_x25\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x25\n"
"    ldr  x25, [x9]\n"
"    adrp x9, arm64_kernel_resume_x26\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x26\n"
"    ldr  x26, [x9]\n"
"    adrp x9, arm64_kernel_resume_x27\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x27\n"
"    ldr  x27, [x9]\n"
"    adrp x9, arm64_kernel_resume_x28\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x28\n"
"    ldr  x28, [x9]\n"
"    adrp x9, arm64_kernel_resume_x29\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_x29\n"
"    ldr  x29, [x9]\n"
"    adrp x9, arm64_kernel_resume_lr\n"
"    add  x9, x9, :lo12:arm64_kernel_resume_lr\n"
"    ldr  x30, [x9]\n"
"    ret\n");

static void arm64_save_kernel_resume_state(void) {
    __asm__ volatile(
        "mov %0, sp\n\t"
        "mov %1, x19\n\t"
        "mov %2, x20\n\t"
        "mov %3, x21\n\t"
        "mov %4, x22\n\t"
        "mov %5, x23\n\t"
        "mov %6, x24\n\t"
        "mov %7, x25\n\t"
        "mov %8, x26\n\t"
        "mov %9, x27\n\t"
        "mov %10, x28\n\t"
        "mov %11, x29\n\t"
        : "=r"(arm64_kernel_resume_sp),
          "=r"(arm64_kernel_resume_x19),
          "=r"(arm64_kernel_resume_x20),
          "=r"(arm64_kernel_resume_x21),
          "=r"(arm64_kernel_resume_x22),
          "=r"(arm64_kernel_resume_x23),
          "=r"(arm64_kernel_resume_x24),
          "=r"(arm64_kernel_resume_x25),
          "=r"(arm64_kernel_resume_x26),
          "=r"(arm64_kernel_resume_x27),
          "=r"(arm64_kernel_resume_x28),
          "=r"(arm64_kernel_resume_x29)
        :
        : "memory");
}

static void copy_text(char *dst, size_t cap, const char *src) {
    if (!dst || cap == 0) return;
    size_t i = 0;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void arm64_log_hex(const char *label, uint64_t value) {
    serial_write(label);
    print_hex(value);
    serial_putc('\n');
}

static void arm64_return_to_kernel(struct arm64_exception_frame *frame,
                                   int exit_code) {
    user_state.active = 0;
    user_state.exited = 1;
    user_state.exit_code = exit_code;
    arm64_user_exit_value = (uint64_t)(int64_t)exit_code;
    frame->elr_el1 = (uint64_t)(uintptr_t)&arm64_user_exit_trampoline;
    frame->spsr_el1 = ARM64_SPSR_MODE_EL1H;
}

static int64_t arm64_sys_read(int64_t fd, void *buf, size_t count) {
    if (!buf) return -1;
    if (fd == 0) {
        char *out = (char *)buf;
        size_t got = 0;
        while (got < count) {
            out[got++] = serial_getc();
            break;
        }
        return (int64_t)got;
    }
    if (fd < 3) return -1;
    return vfs_read((int)fd - 3, buf, count);
}

static int64_t arm64_sys_write(int64_t fd, const void *buf, size_t count) {
    if (!buf) return -1;
    if (fd == 1 || fd == 2) {
        if (fb_is_available()) {
            fb_con_write((const char *)buf, count);
        } else {
            serial_write_len((const char *)buf, count);
        }
        return (int64_t)count;
    }
    if (fd < 3) return -1;
    return vfs_write((int)fd - 3, buf, count);
}

static int64_t arm64_sys_open(const char *path, int flags, int mode) {
    (void)mode;
    if (!path) return -1;
    int fd = vfs_open(path, flags);
    if (fd < 0) return -1;
    return fd + 3;
}

static int64_t arm64_sys_close(int64_t fd) {
    if (fd < 3) return -1;
    return vfs_close((int)fd - 3);
}

static int64_t arm64_sys_sleep_ms(uint64_t ms) {
    uint64_t until = timer_get_uptime_ms() + ms;
    while (timer_get_uptime_ms() < until) {
        __asm__ volatile("wfe");
    }
    return 0;
}

static int64_t arm64_sys_uptime_ms(void) {
    return (int64_t)timer_get_uptime_ms();
}

static int64_t arm64_sys_input(void *buf, size_t count) {
    return arm64_sys_read(0, buf, count);
}

static int64_t arm64_sys_get_cmdline(char *buf, size_t count) {
    if (!buf || count == 0) return -1;
    copy_text(buf, count, user_state.cmdline);
    return 0;
}

static int64_t arm64_sys_fb_info(int64_t field) {
    if (!fb_is_available()) return -1;
    if (field == ARM64_FB_FIELD_WIDTH) return fb_get_width();
    if (field == ARM64_FB_FIELD_HEIGHT) return fb_get_height();
    if (field == ARM64_FB_FIELD_BPP) return fb_get_bpp();
    return -1;
}

static int64_t arm64_dispatch_syscall(struct arm64_exception_frame *frame) {
    uint64_t nr = frame->x[8];

    switch (nr) {
        case ARM64_SYS_READ:
            return arm64_sys_read((int64_t)frame->x[0],
                                  (void *)(uintptr_t)frame->x[1],
                                  (size_t)frame->x[2]);
        case ARM64_SYS_WRITE:
            return arm64_sys_write((int64_t)frame->x[0],
                                   (const void *)(uintptr_t)frame->x[1],
                                   (size_t)frame->x[2]);
        case ARM64_SYS_OPEN:
            return arm64_sys_open((const char *)(uintptr_t)frame->x[0],
                                  (int)frame->x[1],
                                  (int)frame->x[2]);
        case ARM64_SYS_CLOSE:
            return arm64_sys_close((int64_t)frame->x[0]);
        case ARM64_SYS_SLEEP_MS:
            return arm64_sys_sleep_ms(frame->x[0]);
        case ARM64_SYS_UPTIME_MS:
            return arm64_sys_uptime_ms();
        case ARM64_SYS_INPUT:
            return arm64_sys_input((void *)(uintptr_t)frame->x[0],
                                   (size_t)frame->x[1]);
        case ARM64_SYS_GET_CMDLINE:
            return arm64_sys_get_cmdline((char *)(uintptr_t)frame->x[0],
                                         (size_t)frame->x[1]);
        case ARM64_SYS_FB_INFO:
            return arm64_sys_fb_info((int64_t)frame->x[0]);
        case ARM64_SYS_EXIT:
            arm64_return_to_kernel(frame, (int)frame->x[0]);
            return 0;
        default:
            return -1;
    }
}

static void arm64_enter_user(uint64_t entry, uint64_t stack_top) {
    __asm__ volatile(
        "msr sp_el0, %0\n\t"
        "msr elr_el1, %1\n\t"
        "mov x0, xzr\n\t"
        "mov x1, xzr\n\t"
        "mov x2, xzr\n\t"
        "msr spsr_el1, %2\n\t"
        "isb\n\t"
        "eret\n\t"
        :
        : "r"(stack_top), "r"(entry), "r"(ARM64_SPSR_MODE_EL0T)
        : "x0", "x1", "x2", "memory");
}

int arm64_run_init_program(const char *path, const char *cmdline) {
    if (!path || !path[0]) return -1;

    memset(&user_state, 0, sizeof(user_state));
    copy_text(user_state.cmdline, sizeof(user_state.cmdline), cmdline);

    int rc = elf_load_from_file(path, &user_state.image);
    if (rc != ELF_OK) {
        serial_write("User init load failed: ");
        serial_write(user_state.image.error);
        serial_putc('\n');
        return rc;
    }

    serial_write("User init launch: ");
    serial_write(path);
    serial_putc('\n');

    user_state.active = 1;
    user_state.exited = 0;
    user_state.exit_code = 0;
    arm64_save_kernel_resume_state();
    arm64_kernel_resume_lr = (uint64_t)(uintptr_t)&&user_return;
    arm64_enter_user(user_state.image.entry, user_state.image.stack_top);

user_return:
    if (user_state.image.load_end > user_state.image.load_base) {
        uint64_t stack_top_page =
            paging_align_up(user_state.image.stack_top, PAGE_SIZE);
        elf_unload(user_state.image.load_base,
                   user_state.image.load_end,
                   user_state.image.stack_bottom,
                   stack_top_page);
    }
    return (int)(int64_t)arm64_user_exit_value;
}

void arm64_handle_exception(struct arm64_exception_frame *frame) {
    uint32_t ec = (uint32_t)((frame->esr_el1 >> ARM64_ESR_EC_SHIFT) &
                              ARM64_ESR_EC_MASK);

    if (user_state.active && ec == ARM64_ESR_EC_SVC64) {
        frame->x[0] = (uint64_t)arm64_dispatch_syscall(frame);
        if (user_state.active) frame->elr_el1 += 4;
        return;
    }

    serial_write("\nARM64 exception\n");
    arm64_log_hex("ESR: ", frame->esr_el1);
    arm64_log_hex("FAR: ", frame->far_el1);
    arm64_log_hex("ELR: ", frame->elr_el1);

    if (user_state.active) {
        arm64_return_to_kernel(frame, -1);
        return;
    }

    hang();
}
