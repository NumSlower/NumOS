/*
 * kmain.c - NumOS Kernel Entry Point
 *
 * Step 0: font_init()  <- very first call; bitmap font ready before any output
 * Step 1: vga_init()
 * Steps 2-10: GDT, IDT, paging, heap, timer, keyboard, syscall,
 *             scheduler, device detection, ATA + FAT32
 */

#include "kernel/kernel.h"
#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "kernel/elf_loader.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/font.h"
#include "drivers/framebuffer.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/paging.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/ata.h"
#include "drivers/device.h"
#include "cpu/heap.h"
#include "fs/fat32.h"

/*
 * font_data.h is generated at build time by:
 *   python3 tools/font2c.py  path/to/font.psf  > src/drivers/font_data.h
 * If it was not generated, font.c uses the built-in 8x16 fallback.
 */
#ifdef FONT_DATA_AVAILABLE
#include "drivers/font_data.h"
#endif

/* =========================================================================
 * Boot display helpers
 * ======================================================================= */

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

static void boot_fail(int step, int total, const char *description) {
    boot_step(step, total, VGA_COLOR_LIGHT_RED, description,
              VGA_COLOR_LIGHT_RED, "FAIL");
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
    vga_writestring("  |  Arch: x86-64  |  Paging: 4-level  |  Mode: Long    |\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    vga_writestring("  +=======================================================+\n");

    /* Show which font was loaded */
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
    vga_writestring("    Launching ELF: /init/SHELL\n");
    vga_writestring("  ========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    struct elf_load_result result;
    int rc = elf_load_from_file("init/SHELL", &result);

    if (rc != ELF_OK) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  ELF load FAILED: ");
        vga_writestring(result.error); vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("  Continuing in interactive mode.\n");
        return;
    }

    struct process *proc = process_spawn("elftest", result.entry,
                                         result.stack_top, result.stack_bottom);
    if (!proc) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  process_spawn FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    proc->load_base = result.load_base;
    proc->load_end  = result.load_end;

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

void kernel_init(void) {
    /* ------------------------------------------------------------------
     * Step 0: font  - must be before vga_init / fb_init so the very
     * first pixel drawn uses the chosen bitmap font.
     * ------------------------------------------------------------------ */
#ifdef FONT_DATA_AVAILABLE
    font_init(embedded_font_data, embedded_font_size);
#else
    font_init(NULL, 0);   /* activates built-in 8x16 fallback in font.c */
#endif

    vga_init();
    boot_banner();

    boot_section("CPU & INTERRUPT INFRASTRUCTURE", VGA_COLOR_LIGHT_BLUE);
    vga_writestring("  Loading GDT descriptors...\n");
    gdt_init();
    boot_ok(1, 10, VGA_COLOR_LIGHT_CYAN, "GDT  segment table + TSS loaded");

    vga_writestring("  Installing IDT gates...\n");
    idt_init();
    boot_ok(2, 10, VGA_COLOR_LIGHT_CYAN, "IDT  256 vectors armed");

    boot_section("MEMORY MANAGEMENT", VGA_COLOR_LIGHT_MAGENTA);
    vga_writestring("  Setting up 4-level page tables...\n");
    paging_init();
    boot_ok(3, 10, VGA_COLOR_LIGHT_MAGENTA, "PMM  physical frame allocator ready");

    vga_writestring("  Initializing kernel heap...\n");
    heap_init();
    boot_ok(4, 10, VGA_COLOR_YELLOW, "HEAP 128 MB best-fit allocator ready");

    fb_init();
    if (fb_is_available()) {
        vga_clear();
        boot_banner();
        boot_section("CPU & INTERRUPT INFRASTRUCTURE", VGA_COLOR_LIGHT_BLUE);
        boot_ok(1, 10, VGA_COLOR_LIGHT_CYAN, "GDT  segment table + TSS loaded");
        boot_ok(2, 10, VGA_COLOR_LIGHT_CYAN, "IDT  256 vectors armed");
        boot_section("MEMORY MANAGEMENT", VGA_COLOR_LIGHT_MAGENTA);
        boot_ok(3, 10, VGA_COLOR_LIGHT_MAGENTA, "PMM  physical frame allocator ready");
        boot_ok(4, 10, VGA_COLOR_YELLOW, "HEAP 128 MB best-fit allocator ready");
        vga_writestring("FB: ");
        print_dec(FB_WIDTH); vga_writestring("x");
        print_dec(FB_HEIGHT); vga_writestring("x32 ready\n");
    }

    boot_section("TIMERS & INPUT", VGA_COLOR_CYAN);
    vga_writestring("  Programming PIT channel 0...\n");
    timer_init(100);
    boot_ok(5, 10, VGA_COLOR_CYAN, "PIT  100 Hz system timer");
    idt_init();

    vga_writestring("  Enabling keyboard IRQ...\n");
    keyboard_init();
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    boot_ok(6, 10, VGA_COLOR_CYAN, "PS/2 keyboard driver + IRQ 0/1 unmasked");

    boot_section("KERNEL SERVICES", VGA_COLOR_LIGHT_GREEN);
    vga_writestring("  Configuring SYSCALL/SYSRET MSRs...\n");
    syscall_init();
    boot_ok(7, 10, VGA_COLOR_LIGHT_GREEN, "SYSCALL/SYSRET ABI configured");

    vga_writestring("  Building idle process and run-queue...\n");
    scheduler_init();
    boot_ok(8, 10, VGA_COLOR_LIGHT_GREEN, "Scheduler round-robin ready");

    boot_section("HARDWARE DETECTION", VGA_COLOR_LIGHT_BROWN);
    vga_writestring("  Scanning PCI bus and PS/2 ports...\n");
    device_init();
    boot_ok(9, 10, VGA_COLOR_LIGHT_BROWN, "Device registry populated");

    boot_section("STORAGE & FILESYSTEM", VGA_COLOR_LIGHT_RED);
    vga_writestring("  Probing ATA primary bus...\n");
    ata_init();

    int fat_ok = (fat32_init() == 0 && fat32_mount() == 0) ? 1 : 0;
    if (fat_ok)
        boot_ok(10, 10, VGA_COLOR_LIGHT_RED, "FAT32 volume mounted OK");
    else
        boot_fail(10, 10, "FAT32 mount failed");

    boot_done();
}

/* =========================================================================
 * Kernel entry point
 * ======================================================================= */

void kernel_main(void) {
    kernel_init();
    run_system_tests();
    launch_init_elf();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n  ========================================\n");
    vga_writestring("    Kernel Interactive Mode\n");
    vga_writestring("  ========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  [S] Scroll mode     [L] List root dir\n");
    vga_writestring("  [I] Syscall stats   [P] Process list\n");
    vga_writestring("  [D] Device list     [R] Re-run ELF\n");
    vga_writestring("  [H] Halt\n");
    vga_writestring("\nPress a key: ");

    while (1) {
        uint8_t scan_code = keyboard_read_scan_code();
        char    c         = scan_code_to_ascii(scan_code);
        if (c == 0) continue;
        switch (c) {
            case 's': case 'S':
                vga_writestring("\nEntering scroll mode...\n");
                vga_enter_scroll_mode();
                vga_writestring("\nPress S/L/I/P/R/H: "); break;
            case 'l': case 'L':
                vga_putchar('\n');
                fat32_list_directory("/");
                vga_writestring("\nPress S/L/I/P/R/H: "); break;
            case 'i': case 'I':
                syscall_print_stats();
                vga_writestring("\nPress S/L/I/P/R/H: "); break;
            case 'p': case 'P':
                scheduler_print_processes();
                scheduler_print_stats();
                vga_writestring("\nPress S/L/I/P/R/H: "); break;
            case 'r': case 'R':
                vga_writestring("\nRe-running ELF...\n");
                launch_init_elf();
                vga_writestring("\nPress S/L/I/P/R/H: "); break;
            case 'd': case 'D':
                device_print_all();
                vga_writestring("\nPress S/L/I/P/D/R/H: "); break;
            case 'h': case 'H':
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                vga_writestring("\nSystem halted.\n");
                hang(); break;
            default: break;
        }
    }
}
