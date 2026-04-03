#include "kernel/kernel.h"
#include "kernel/arm64.h"
#include "kernel/fdt.h"
#include "kernel/elf_loader.h"
#include "drivers/serial.h"
#include "drivers/ramdisk.h"
#include "drivers/framebuffer.h"
#include "drivers/font.h"
#include "drivers/graphices/vga.h"
#include "cpu/idt.h"
#include "cpu/fpu.h"
#include "cpu/heap.h"
#include "cpu/paging.h"
#include "drivers/timer.h"
#include "fs/fat32.h"
#include "fs/vfs.h"

#ifndef NUMOS_INIT_PATH
#define NUMOS_INIT_PATH "/bin/empty.elf"
#endif

static void __attribute__((no_stack_protector))
serial_write_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> shift) & 0xFu]);
    }
}

static void __attribute__((no_stack_protector))
serial_write_dec64(uint64_t value) {
    char buffer[21];
    int pos = 20;
    buffer[20] = '\0';

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value > 0 && pos > 0) {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    }

    serial_write(&buffer[pos]);
}

static void __attribute__((no_stack_protector))
banner_hex_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_hex64(value);
    serial_putc('\n');
}

static void __attribute__((no_stack_protector))
banner_dec_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_dec64(value);
    serial_putc('\n');
}

static void __attribute__((no_stack_protector))
banner_signed_line(const char *label, int64_t value) {
    serial_write(label);
    if (value < 0) {
        serial_putc('-');
        serial_write_dec64((uint64_t)(-value));
    } else {
        serial_write_dec64((uint64_t)value);
    }
    serial_putc('\n');
}

static void __attribute__((no_stack_protector))
banner_text_line(const char *label, const char *value) {
    serial_write(label);
    serial_write(value ? value : "(null)");
    serial_putc('\n');
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

static void resolve_init_path(const char *bootargs, char *out, size_t cap) {
    copy_text(out, cap, NUMOS_INIT_PATH);
    if (!bootargs || !bootargs[0]) return;

    const char *p = bootargs;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (p[0] == '\0') break;
        if (p[0] == 'i' && p[1] == 'n' && p[2] == 'i' && p[3] == 't' &&
            p[4] == '=') {
            p += 5;
            size_t i = 0;
            while (p[i] && p[i] != ' ' && p[i] != '\t' && i + 1 < cap) {
                out[i] = p[i];
                i++;
            }
            out[i] = '\0';
            return;
        }
        while (*p && *p != ' ' && *p != '\t') p++;
    }
}

static void __attribute__((no_stack_protector))
probe_init_elf(const char *path) {
    struct elf64_hdr hdr;
    int fd = vfs_open(path, FAT32_O_RDONLY);
    if (fd < 0) {
        banner_text_line("Init ELF: ", "open failed");
        return;
    }

    ssize_t n = vfs_read(fd, &hdr, sizeof(hdr));
    vfs_close(fd);
    if (n != (ssize_t)sizeof(hdr)) {
        banner_text_line("Init ELF: ", "short read");
        return;
    }

    if (elf_validate(&hdr) != ELF_OK) {
        banner_text_line("Init ELF: ", "validation failed");
        return;
    }

    banner_hex_line("Init ELF entry: ", hdr.e_entry);
}

static int arm64_init_framebuffer(uint64_t fdt_addr) {
    struct numos_fdt_framebuffer fb;
    if (fdt_find_simple_framebuffer(fdt_addr, &fb) != 0) return -1;

    struct fb_mode_info mode;
    memset(&mode, 0, sizeof(mode));
    mode.backend = 2;
    mode.width = (int)fb.width;
    mode.height = (int)fb.height;
    mode.bpp = (int)fb.bpp;
    mode.pitch = (int)fb.stride;
    mode.bytespp = (int)((fb.bpp + 7u) / 8u);
    mode.phys_base = fb.base;
    mode.red_pos = fb.red_pos;
    mode.red_size = fb.red_size;
    mode.green_pos = fb.green_pos;
    mode.green_size = fb.green_size;
    mode.blue_pos = fb.blue_pos;
    mode.blue_size = fb.blue_size;
    copy_text(mode.source, sizeof(mode.source), "FDT simplefb");

    font_init(NULL, 0);
    vga_init();
    if (!fb_init_from_mode(&mode)) return -1;

    banner_dec_line("Framebuffer width: ", fb.width);
    banner_dec_line("Framebuffer height: ", fb.height);
    return 0;
}

void arm64_boot_main(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (void)arg1;
    (void)arg2;
    (void)arg3;

    struct numos_fdt_bootargs bootargs;
    char init_path[128];

    serial_init();
    serial_write("[1] serial ok\n");
    runtime_init();
    serial_write("[2] runtime ok\n");
    idt_init();
    serial_write("[3] vectors ok\n");
    fpu_init();
    serial_write("[4] fpu ok\n");
    paging_init(0);
    serial_write("[5] paging ok\n");
    heap_init();
    serial_write("[6] heap ok\n");
    timer_init(0);
    serial_write("[7] timer ok\n");
    if (arm64_init_framebuffer(arg0) == 0) {
        serial_write("[8] framebuffer ok\n");
    } else {
        serial_write("[8] framebuffer skipped\n");
    }

    serial_write("\nNumOS ARM64 bring up\n");
    serial_write("Target: QEMU virt\n");
    banner_hex_line("Boot arg: ", arg0);
    banner_hex_line("Core: ", arm64_core_id());
    banner_hex_line("EL: ", arm64_exception_level());
    banner_hex_line("CNTFRQ: ", arm64_counter_frequency());
    banner_dec_line("Uptime ms: ", timer_get_uptime_ms());
    if (fdt_get_bootargs(arg0, &bootargs) == 0) {
        banner_text_line("Bootargs: ", bootargs.text);
    } else {
        bootargs.text[0] = '\0';
        banner_text_line("Bootargs: ", "(none)");
    }
    resolve_init_path(bootargs.text, init_path, sizeof(init_path));
    banner_text_line("Init path: ", init_path);

    struct numos_fdt_initrd initrd;
    if (fdt_find_initrd(arg0, &initrd) == 0) {
        banner_hex_line("Initrd start: ", initrd.start);
        banner_hex_line("Initrd end: ", initrd.end);
        ramdisk_init(initrd.start, initrd.end - initrd.start);

        vfs_init();
        if (fat32_init() == 0 &&
            fat32_mount() == 0 &&
            vfs_register_fat32_root() == 0) {
            banner_text_line("Storage: ", "ramdisk FAT32 mounted");
            fat32_list_directory("/");
            probe_init_elf(init_path);
            int init_rc = arm64_run_init_program(init_path, "");
            banner_signed_line("Init exit code: ", (int64_t)init_rc);
        } else {
            banner_text_line("Storage: ", "mount failed");
        }
    } else {
        banner_text_line("Storage: ", "no initrd in device tree");
    }

    serial_write("Status: arm64 bring up complete\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
