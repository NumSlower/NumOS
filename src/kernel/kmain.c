/*
 * kmain.c - NumOS Kernel Entry Point
 *
 * Changes from previous version:
 *   - Graphics backend selection supports VGA, VESA, and BGA.
 *   - kernel_main interactive loop gained 'V' key to print video mode info.
 *   - multiboot2 module tag parsed; ramdisk used when present.
 *   - Removed duplicate idt_init() call.
 */

#include "kernel/kernel.h"
#include "kernel/arch.h"
#include "kernel/config.h"
#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "kernel/process.h"
#include "kernel/elf_loader.h"
#include "kernel/multiboot2.h"
#include "drivers/graphices/graphics.h"
#include "drivers/graphices/vga.h"
#include "drivers/keyboard.h"
#include "drivers/font.h"
#include "drivers/framebuffer.h"
#include "drivers/graphices/vesa.h"
#include "drivers/ramdisk.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/fpu.h"
#include "cpu/paging.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/ata.h"
#include "drivers/device.h"
#include "cpu/heap.h"
#include "fs/fat32.h"
#include "fs/vfs.h"

#ifdef FONT_DATA_AVAILABLE
#include "drivers/font_data.h"
#endif

static char init_path_buf[128];
static const char *init_path = NUMOS_INIT_PATH;
static volatile uint64_t kernel_thread_probe_runs = 0;

static void kernel_thread_probe(void *arg) {
    volatile uint64_t *runs = (volatile uint64_t *)arg;
    if (runs) (*runs)++;
}

/* =========================================================================
 * Boot display helpers
 * ======================================================================= */

static const char *mb2_get_cmdline(uint64_t info_phys) {
    struct mb2_tag *tag = mb2_find_tag(info_phys, MB2_TAG_CMDLINE);
    if (!tag) return NULL;
    return (const char *)((uint8_t *)tag + sizeof(struct mb2_tag));
}

static const char *cmdline_get_value(const char *cmdline, const char *key,
                                     char *out, size_t cap) {
    if (!cmdline || !key || !out || cap == 0) return NULL;
    size_t key_len = strlen(key);
    const char *p = cmdline;
    while (*p) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        const char *tok = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        size_t tok_len = (size_t)(p - tok);
        if (tok_len > key_len && strncmp(tok, key, key_len) == 0) {
            size_t val_len = tok_len - key_len;
            if (val_len >= cap) val_len = cap - 1;
            memcpy(out, tok + key_len, val_len);
            out[val_len] = '\0';
            return out;
        }
    }
    return NULL;
}

static void resolve_init_path(uint64_t info_phys) {
    const char *cmdline = mb2_get_cmdline(info_phys);
    const char *val = cmdline_get_value(cmdline, "init=", init_path_buf,
                                        sizeof(init_path_buf));
    if (val && val[0]) {
        init_path = val;
    } else {
        init_path = NUMOS_INIT_PATH;
    }
}

static int resolve_graphics_backend(uint64_t info_phys) {
    const char *cmdline = mb2_get_cmdline(info_phys);
    char value[16];
    const char *gfx = cmdline_get_value(cmdline, "gfx=", value, sizeof(value));
    if (gfx && gfx[0]) {
        int backend = graphics_backend_from_name(gfx);
        if (backend >= GRAPHICS_BACKEND_AUTO) return backend;
    }

    return NUMOS_GRAPHICS_DEFAULT_BACKEND;
}

static const char *init_proc_name(void) {
    static char name_buf[16];
    const char *p = init_path;
    const char *base = init_path;
    while (*p) {
        if (*p == '/') base = p + 1;
        p++;
    }
    int i = 0;
    while (base[i] && base[i] != '.' && i < (int)(sizeof(name_buf) - 1)) {
        name_buf[i] = base[i];
        i++;
    }
    if (i == 0) {
        name_buf[0] = 'i';
        name_buf[1] = 'n';
        name_buf[2] = 'i';
        name_buf[3] = 't';
        name_buf[4] = '\0';
    } else {
        name_buf[i] = '\0';
    }
    return name_buf;
}

static void boot_step(int step, int total,
                       vga_color_t label_color,
                       const char *description,
                       vga_color_t badge_color,
                       const char *badge_text) {
    vga_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_writestring("  [");
    if (step < 10) vga_putchar('0');
    print_dec((uint64_t)step);
    vga_putchar('/');
    if (total < 10) vga_putchar('0');
    print_dec((uint64_t)total);
    vga_writestring("]  ");
    vga_setcolor(vga_entry_color(label_color, VGA_COLOR_BLACK));
    vga_writestring(description);
    vga_writestring("  ");
    vga_setcolor(vga_entry_color(badge_color, VGA_COLOR_BLACK));
    vga_writestring("["); vga_writestring(badge_text); vga_writestring("]");
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static void boot_ok(int step, int total,
                    vga_color_t label_color, const char *description) {
    boot_step(step, total, label_color, description,
              VGA_COLOR_LIGHT_GREEN, " OK ");
}

static void boot_banner(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    vga_writestring("  +=======================================================+\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("  |   ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN,    VGA_COLOR_BLACK)); vga_putchar('N');
    vga_setcolor(vga_entry_color(VGA_COLOR_CYAN,          VGA_COLOR_BLACK)); vga_putchar('u');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE,    VGA_COLOR_BLACK)); vga_putchar('m');
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE,          VGA_COLOR_BLACK)); vga_putchar('O');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK)); vga_putchar('S');
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("  v0.8.0-beta   64-bit x86 Kernel   MIT License        |\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    vga_writestring("  +=======================================================+\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_writestring("  |  Arch: ");
    vga_writestring(NUMOS_ARCH_NAME);
    vga_writestring("  |  Paging: 4-level  |  Mode: ");
    vga_writestring(NUMOS_CPU_MODE_NAME);
    vga_writestring("    |\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    vga_writestring("  +=======================================================+\n");

    vga_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
    vga_writestring("  Font: ");
    if (font_is_loaded()) {
        struct font_info fi;
        font_get_info(&fi);
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        print_dec((uint64_t)fi.width); vga_putchar('x');
        print_dec((uint64_t)fi.height);
        vga_writestring(" bitmap  glyphs=");
        print_dec((uint64_t)fi.num_glyphs);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        vga_writestring("built-in 8x16 fallback");
    }
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_putchar('\n');
}

static void boot_section(const char *label, vga_color_t color) {
    vga_setcolor(vga_entry_color(color, VGA_COLOR_BLACK));
    vga_writestring("\n  -- ");
    vga_writestring(label);
    vga_writestring(" ");
    int len = (int)strlen(label) + 5;
    for (int i = len; i < 52; i++) vga_putchar('-');
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

#if NUMOS_ENABLE_FRAMEBUFFER
static void boot_banner_fb(void) {
    vga_clear();
    boot_banner();
    boot_section("CPU & INTERRUPT INFRASTRUCTURE", VGA_COLOR_LIGHT_BLUE);
    boot_ok(1, 12, VGA_COLOR_LIGHT_CYAN, "GDT  segment table + TSS loaded");
    boot_ok(2, 12, VGA_COLOR_LIGHT_CYAN, "IDT  256 vectors armed");
    boot_section("MEMORY MANAGEMENT", VGA_COLOR_LIGHT_MAGENTA);
    boot_ok(3, 12, VGA_COLOR_LIGHT_MAGENTA, "PMM  physical frame allocator ready");
    boot_ok(4, 12, VGA_COLOR_YELLOW, "HEAP 128 MB best-fit allocator ready");

    const struct fb_mode_info *mode = graphics_get_active_mode();
    if (mode && graphics_is_framebuffer_backend(mode->backend)) {
        vga_writestring("  ");
        vga_writestring(graphics_backend_name(mode->backend));
        vga_writestring(": ");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        print_dec((uint64_t)mode->width); vga_writestring("x");
        print_dec((uint64_t)mode->height); vga_writestring("x");
        print_dec((uint64_t)mode->bpp);
        vga_writestring(" via ");
        vga_writestring(mode->source);
        vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_writestring("  VGA: text mode ready\n");
    }
}
#endif

static void boot_done(void) {
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("  +=======================================================+\n");
    vga_writestring("  |           All subsystems initialized OK               |\n");
    vga_writestring("  +=======================================================+\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_putchar('\n');
}

/* =========================================================================
 * Self-test helpers
 * ======================================================================= */

static void test_memory_allocation(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  === Memory Allocation Test ===\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  kmalloc(1024)... ");
    void *ptr1 = kmalloc(1024);
    if (ptr1) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK  0x"); print_hex((uint64_t)ptr1); vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        memset(ptr1, 0xAB, 1024); kfree(ptr1);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    vga_writestring("  kzalloc(2048)... ");
    void *ptr2 = kzalloc(2048);
    if (ptr2) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK  0x"); print_hex((uint64_t)ptr2); vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kfree(ptr2);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    vga_putchar('\n');
    heap_print_stats();

    vga_writestring("  Heap validate... ");
    if (heap_validate()) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_writestring("  VMM page test... ");
    void *tmp = vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
    if (tmp) {
        int mapped_before = paging_is_mapped((uint64_t)tmp);
        vmm_free_pages(tmp, 1);
        int mapped_after = paging_is_mapped((uint64_t)tmp);
        if (mapped_before && !mapped_after) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("OK\n");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("FAILED\n");
        }
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

static int boot_use_compact_fb_log(void) {
    const struct hypervisor_info *hv = device_get_hypervisor();
    return hv && hv->id == HYPERVISOR_VIRTUALBOX;
}

static void test_filesystem(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  === Filesystem Test ===\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    fat32_print_info();
    vga_putchar('\n');
    fat32_list_directory("/");
}

static void test_syscalls(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  === Syscall Subsystem Test ===\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  sys_write(stdout)... ");
    const char *hello   = "Hello from sys_write!\n";
    int64_t     written = sys_write(FD_STDOUT, hello, strlen(hello));
    vga_setcolor(written == (int64_t)strlen(hello)
                 ? vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK)
                 : vga_entry_color(VGA_COLOR_LIGHT_RED,   VGA_COLOR_BLACK));
    vga_writestring(written == (int64_t)strlen(hello) ? "[PASS]\n" : "[FAIL]\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  sys_write(bad_fd)... ");
    if (sys_write(99, "x", 1) == SYSCALL_EBADF) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS] EBADF\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    vga_writestring("  sys_getpid()... ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("[PASS] pid="); print_dec((uint64_t)sys_getpid()); vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  sys_uptime_ms()... ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("[PASS] uptime="); print_dec((uint64_t)sys_uptime_ms());
    vga_writestring(" ms\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  sys_sysinfo()... ");
    struct sysinfo info;
    if (sys_sysinfo(&info) == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS] version=");
        vga_writestring(info.version);
        vga_writestring(" uptime_ms=");
        print_dec(info.uptime_ms);
        vga_writestring("\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("[FAIL]\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  sys_puts()... ");
    sys_puts("[PASS] sys_puts: this line via the syscall handler");
    syscall_print_stats();
}

static void run_system_tests(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  ========================================\n");
    vga_writestring("      NumOS System Tests\n");
    vga_writestring("  ========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    test_memory_allocation();
    test_filesystem();
    test_syscalls();
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("  ========================================\n");
    vga_writestring("      Tests Complete\n");
    vga_writestring("  ========================================\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* =========================================================================
 * ELF launcher
 * ======================================================================= */

static void launch_init_elf(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  ========================================\n");
    vga_writestring("    Launching ELF: ");
    vga_writestring(init_path);
    vga_putchar('\n');
    vga_writestring("  ========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    uint64_t shell_cr3 = paging_create_user_pml4();
    if (!shell_cr3) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  process_spawn FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    struct elf_load_result result;
    struct page_table *saved_pml4 = paging_get_active_pml4();
    __asm__ volatile("cli");
    paging_set_active_pml4((struct page_table *)(uintptr_t)shell_cr3);
    int rc = elf_load_from_file(init_path, &result);
    paging_set_active_pml4(saved_pml4);
    __asm__ volatile("sti");

    if (rc != ELF_OK) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  ELF load FAILED: ");
        vga_writestring(result.error); vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("  Continuing in interactive mode.\n");
        return;
    }

    struct process *proc = process_spawn(init_proc_name(), result.entry,
                                         result.stack_top, result.stack_bottom);
    if (!proc) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  process_spawn FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    proc->load_base = result.load_base;
    proc->load_end  = result.load_end;
    proc->cr3       = shell_cr3;
    proc->flags    |= PROC_FLAG_VERIFIED;

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("  User process spawned (pid ");
    print_dec((uint64_t)proc->pid);
    vga_writestring("). Yielding CPU...\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    while (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
        schedule();
        if (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED)
            __asm__ volatile("sti; hlt" ::: "memory");
    }

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  User process finished. Kernel regained control.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    process_reap(proc);
}

/* =========================================================================
 * Subsystem initialisation
 * ======================================================================= */

void kernel_init(uint64_t mb2_info_phys) {

    /* Step 0: font must be before any output */
#ifdef FONT_DATA_AVAILABLE
    font_init(embedded_font_data, embedded_font_size);
#else
    font_init(NULL, 0);
#endif

    vga_init();
    device_detect_hypervisor_early();
#if NUMOS_VGA_FAST_MODE_ON_VBOX
    const struct hypervisor_info *hv = device_get_hypervisor();
    if (hv && hv->id == HYPERVISOR_VIRTUALBOX) vga_set_fast_mode(1);
#endif
    boot_banner();
    resolve_init_path(mb2_info_phys);

    /* ------------------------------------------------------------------
     * Parse multiboot2 info: find disk image module for RAM disk.
     * Done before heap_init so no kmalloc yet.
     * ------------------------------------------------------------------ */
    uint64_t ramdisk_phys = 0;
    uint64_t ramdisk_sz   = 0;
    const char *ramdisk_label = NULL;

    if (mb2_info_phys) {
        struct mb2_tag *mod_tag = mb2_find_tag(mb2_info_phys, MB2_TAG_MODULE);
        if (mod_tag) {
            struct mb2_tag_module *m = (struct mb2_tag_module *)mod_tag;
            ramdisk_phys = (uint64_t)m->mod_start;
            ramdisk_sz   = (uint64_t)(m->mod_end - m->mod_start);
            ramdisk_label = m->cmdline;
        }
    }

    /* ------------------------------------------------------------------ */
    boot_section("CPU & INTERRUPT INFRASTRUCTURE", VGA_COLOR_LIGHT_BLUE);
    vga_writestring("  Loading GDT descriptors...\n");
    gdt_init();
    boot_ok(1, 12, VGA_COLOR_LIGHT_CYAN, "GDT  segment table + TSS loaded");

    vga_writestring("  Installing IDT gates...\n");
    idt_init();
    boot_ok(2, 12, VGA_COLOR_LIGHT_CYAN, "IDT  256 vectors armed");

    boot_section("MEMORY MANAGEMENT", VGA_COLOR_LIGHT_MAGENTA);
    vga_writestring("  Setting up 4-level page tables...\n");

    extern char _kernel_end;
    uint64_t reserved_end = (uint64_t)(uintptr_t)&_kernel_end;

    if (mb2_info_phys) {
        struct mb2_info *info = (struct mb2_info *)(uintptr_t)mb2_info_phys;
        uint64_t info_end     = mb2_info_phys + (uint64_t)info->total_size;
        if (info_end > reserved_end) reserved_end = info_end;
    }
    if (ramdisk_phys && ramdisk_sz) {
        uint64_t mod_end = ramdisk_phys + ramdisk_sz;
        if (mod_end > reserved_end) reserved_end = mod_end;
    }

    paging_init(reserved_end);
    boot_ok(3, 12, VGA_COLOR_LIGHT_MAGENTA, "PMM  physical frame allocator ready");

    vga_writestring("  Initializing kernel heap...\n");
    heap_init();
    runtime_init();
    boot_ok(4, 12, VGA_COLOR_YELLOW, "HEAP 128 MB best-fit allocator ready");

    /* ------------------------------------------------------------------
     * VESA/VBE detection.
     * vesa_init() must run AFTER paging_init() and heap_init() because
     * the framebuffer pages are identity-mapped in paging_init().
     * Run it before graphics activation so the VESA descriptor is ready
     * for diagnostics and fallback decisions.
     * ------------------------------------------------------------------ */
    boot_section("DISPLAY", VGA_COLOR_LIGHT_CYAN);

    /* ----------------------------------------------------------------
     * Step 1: detect what framebuffer GRUB set up (if any).
     * vesa_init() reads the MB2 framebuffer tag and classifies the
     * source as "VBE via GRUB (real BIOS)", "BGA (Bochs/QEMU)", or
     * "VGA legacy" based on the physical base address.
     * ---------------------------------------------------------------- */
    int vesa_ok = vesa_init(mb2_info_phys);
    int requested_backend = resolve_graphics_backend(mb2_info_phys);

#if NUMOS_ENABLE_FRAMEBUFFER
    (void)vesa_ok;
    int active_backend = graphics_activate_auto(mb2_info_phys, requested_backend);
    int fb_active = graphics_is_framebuffer_backend(active_backend) &&
                    fb_is_available();

    /* ----------------------------------------------------------------
     * Step 3: print a single unambiguous DISPLAY STATUS line.
     *
     *   DISPLAY  [source]  [WxHxBPP]  base=0x...
     *
     * "source" is one of:
     *   VBE via GRUB (real BIOS)  - GRUB called INT 0x10 for us
     *   BGA (Bochs/QEMU)          - kernel programmed BGA I/O ports
     *   VGA text                  - no framebuffer, text mode only
     * ---------------------------------------------------------------- */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  *** DISPLAY STATUS ***\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  Request: ");
    vga_writestring(graphics_backend_name(requested_backend));
    vga_writestring("  priority=");
    print_dec((uint64_t)graphics_backend_priority(requested_backend));
    vga_putchar('\n');

    if (fb_active) {
        const struct fb_mode_info *mode = graphics_get_active_mode();
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("  Source : ");
        vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        vga_writestring(mode->source);
        vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("  Driver : ");
        vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        vga_writestring(graphics_backend_name(active_backend));
        vga_writestring("  priority=");
        print_dec((uint64_t)graphics_backend_priority(active_backend));
        vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("  Mode   : ");
        print_dec((uint64_t)mode->width);  vga_putchar('x');
        print_dec((uint64_t)mode->height); vga_putchar('x');
        print_dec((uint64_t)mode->bpp);    vga_writestring(" bpp\n");
        vga_writestring("  Base   : 0x");
        print_hex(mode->phys_base);        vga_putchar('\n');
        boot_ok(5, 12, VGA_COLOR_LIGHT_CYAN, "Framebuffer active");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
        vga_writestring("  Source : VGA text mode (no framebuffer)\n");
        vga_writestring("  Driver : VGA  priority=1\n");
        boot_step(5, 12, VGA_COLOR_YELLOW,
                  "VGA text mode (FB disabled or unavailable)",
                  VGA_COLOR_YELLOW, "TEXT");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  *** Press [V] at prompt for full VESA details ***\n");

    if (fb_active) {
        if (boot_use_compact_fb_log()) {
            vga_writestring("  Compact framebuffer boot log enabled on VirtualBox.\n");
        } else {
            boot_banner_fb();
        }
    }

#else /* NUMOS_ENABLE_FRAMEBUFFER == 0 */
    (void)vesa_ok;
    (void)requested_backend;
    vga_setcolor(vga_entry_color(VGA_COLOR_YELLOW, VGA_COLOR_BLACK));
    vga_writestring("\n  *** DISPLAY STATUS: VGA text mode ***\n");
    vga_writestring("      (NUMOS_ENABLE_FRAMEBUFFER=0 in config.h)\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    boot_step(5, 12, VGA_COLOR_YELLOW, "VGA text mode (FB disabled by config)",
              VGA_COLOR_YELLOW, "TEXT");
#endif /* NUMOS_ENABLE_FRAMEBUFFER */

    boot_section("TIMERS & INPUT", VGA_COLOR_CYAN);
    vga_writestring("  Programming PIT channel 0...\n");
    timer_init(100);
    boot_ok(6, 12, VGA_COLOR_CYAN, "PIT  100 Hz system timer");

    vga_writestring("  Enabling keyboard & timer IRQs...\n");
    keyboard_init();
    pic_unmask_irq(0);  /* PIT  timer    */
    pic_unmask_irq(1);  /* PS/2 keyboard */
    boot_ok(7, 12, VGA_COLOR_CYAN, "PS/2 keyboard driver + IRQ 0/1 unmasked");

    boot_section("KERNEL SERVICES", VGA_COLOR_LIGHT_GREEN);
    vga_writestring("  Configuring SYSCALL/SYSRET MSRs...\n");
    syscall_init();
    boot_ok(8, 12, VGA_COLOR_LIGHT_GREEN, "SYSCALL/SYSRET ABI configured");

    vga_writestring("  Enabling x87 and SSE state management...\n");
    if (!fpu_init()) {
        panic("kernel_init: CPU lacks required x87/SSE support");
    }

    vga_writestring("  Building idle process and run-queue...\n");
    scheduler_init();
    boot_ok(9, 12, VGA_COLOR_LIGHT_GREEN, "Scheduler round-robin ready");

    vga_writestring("  Probing kernel thread creation path...\n");
    kernel_thread_probe_runs = 0;
    struct process *probe = process_spawn_kernel("kthread-probe",
                                                 kernel_thread_probe,
                                                 (void *)&kernel_thread_probe_runs);
    if (probe) {
        while (probe->state != PROC_ZOMBIE && probe->state != PROC_UNUSED) {
            schedule();
        }
        process_reap(probe);
    }
    if (kernel_thread_probe_runs == 1) {
        vga_writestring("  Kernel thread probe OK\n");
    } else {
        vga_writestring("  Kernel thread probe skipped\n");
    }

    vga_writestring("  Starting secondary CPUs...\n");
    process_smp_init();

    boot_section("HARDWARE DETECTION", VGA_COLOR_LIGHT_BROWN);
    vga_writestring("  Scanning PCI bus and PS/2 ports...\n");
    device_init();
    boot_ok(10, 12, VGA_COLOR_LIGHT_BROWN, "Device registry populated");

    boot_section("STORAGE & FILESYSTEM", VGA_COLOR_LIGHT_RED);
    vga_writestring("  Probing ATA primary bus...\n");
    ata_init();
    boot_ok(11, 12, VGA_COLOR_LIGHT_BROWN, "ATA  physical disk probed");

    if (ramdisk_phys && ramdisk_sz) {
        vga_writestring("  Multiboot2 module found - initializing RAM disk...\n");
        if (ramdisk_label && ramdisk_label[0]) {
            vga_writestring("  Module: ");
            vga_writestring(ramdisk_label);
            vga_putchar('\n');
        }
        ramdisk_init(ramdisk_phys, ramdisk_sz);
        boot_ok(12, 12, VGA_COLOR_LIGHT_RED, "RAM  module loaded (priority)");
    } else {
        vga_writestring("  No multiboot2 module - using ATA disk only.\n");
        boot_ok(12, 12, VGA_COLOR_LIGHT_RED, "ATA  disk is the sole storage source");
    }

    vfs_init();
    int fat_ok = (fat32_init() == 0 &&
                  fat32_mount() == 0 &&
                  vfs_register_fat32_root() == 0) ? 1 : 0;
    if (fat_ok) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("  FAT32: Mounted OK\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  FAT32: MOUNT FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    boot_done();
}

/* =========================================================================
 * Kernel entry point
 * ======================================================================= */

void kernel_main(uint64_t mb2_info_phys) {
    kernel_init(mb2_info_phys);
    run_system_tests();
    launch_init_elf();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  ========================================\n");
    vga_writestring("    Kernel Interactive Mode\n");
    vga_writestring("  ========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  Graphics: ");
    vga_writestring(graphics_backend_name(graphics_get_active_backend()));
    if (graphics_is_framebuffer_backend(graphics_get_active_backend())) {
        const struct fb_mode_info *mode = graphics_get_active_mode();
        if (mode && mode->source[0]) {
            vga_writestring("  ");
            vga_writestring(mode->source);
        }
    } else {
        vga_writestring("  VGA text mode");
    }
    vga_writestring("\n");
    vga_writestring("  [S] Scroll mode     [L] List root dir\n");
    vga_writestring("  [I] Syscall stats   [P] Process list\n");
    vga_writestring("  [D] Device list     [V] Graphics mode info\n");
    vga_writestring("  [R] Re-run ELF      [H] Halt\n");
    vga_writestring("\nPress a key: ");

    while (1) {
        char c = keyboard_getchar();
        switch (c) {
            case 's': case 'S':
                vga_writestring("\nEntering scroll mode...\n");
                if (fb_is_available()) fb_con_enter_scroll_mode();
                else                  vga_enter_scroll_mode();
                vga_writestring("\nPress a key: "); break;
            case 'l': case 'L':
                vga_putchar('\n');
                fat32_list_directory_recursive("/");
                vga_writestring("\nPress a key: "); break;
            case 'i': case 'I':
                syscall_print_stats();
                vga_writestring("\nPress a key: "); break;
            case 'p': case 'P':
                scheduler_print_processes();
                scheduler_print_stats();
                vga_writestring("\nPress a key: "); break;
            case 'v': case 'V':
                graphics_print_info();
                vesa_print_info();
                vga_writestring("\nPress a key: "); break;
            case 'r': case 'R':
                vga_writestring("\nRe-running ELF...\n");
                launch_init_elf();
                vga_writestring("\nPress a key: "); break;
            case 'd': case 'D':
                device_print_all();
                vga_writestring("\nPress a key: "); break;
            case 'h': case 'H':
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                vga_writestring("\nSystem halted.\n");
                hang(); break;
            default: break;
        }
    }
}
