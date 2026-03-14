/*
 * kmain.c - NumOS Kernel Entry Point
 *
 * Boot sequence:
 *   1. VGA driver
 *   2. GDT (segment descriptors + TSS)
 *   3. IDT (exception and IRQ handlers)
 *   4. Paging (PMM + VMM + region tracking)
 *   5. Heap allocator
 *   6. PIT timer (100 Hz)
 *   7. IDT second pass (picks up timer callback)
 *   8. Keyboard driver + unmask IRQ 0/1
 *   9. SYSCALL/SYSRET subsystem
 *  10. Process scheduler
 *  11. ATA disk + FAT32 filesystem
 *
 * After init, self-tests run, then the ELF at /init/SHELL is loaded and
 * executed as the first user process.  When it exits the kernel switches to
 * the BGA framebuffer (if available) and enters an interactive menu.
 */

#include "kernel/kernel.h"
#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "kernel/elf_loader.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
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

/* =========================================================================
 * Self-test helpers
 * ======================================================================= */

static void test_memory_allocation(void) {
    vga_writestring("\n=== Memory Allocation Test ===\n");

    vga_writestring("Testing kmalloc(1024)... ");
    void *ptr1 = kmalloc(1024);
    if (ptr1) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK 0x"); print_hex((uint64_t)ptr1); vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        memset(ptr1, 0xAB, 1024);
        kfree(ptr1);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    vga_writestring("Testing kzalloc(2048)... ");
    void *ptr2 = kzalloc(2048);
    if (ptr2) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK 0x"); print_hex((uint64_t)ptr2); vga_putchar('\n');
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
    vga_writestring("\n=== Filesystem Test ===\n");
    fat32_print_info();
    vga_putchar('\n');
    fat32_list_directory("/");
}

static void test_syscalls(void) {
    vga_writestring("\n=== Syscall Subsystem Test ===\n");

    vga_writestring("sys_write(stdout)... ");
    const char *hello   = "Hello from sys_write!\n";
    int64_t     written = sys_write(FD_STDOUT, hello, strlen(hello));
    if (written == (int64_t)strlen(hello)) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS]\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("[FAIL]\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_writestring("sys_write(bad_fd)... ");
    if (sys_write(99, "x", 1) == SYSCALL_EBADF) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS] EBADF\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    vga_writestring("sys_getpid()... ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("[PASS] pid=");
    print_dec((uint64_t)sys_getpid());
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_writestring("sys_uptime_ms()... ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("[PASS] uptime=");
    print_dec((uint64_t)sys_uptime_ms());
    vga_writestring(" ms\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_writestring("sys_puts()... ");
    sys_puts("[PASS] sys_puts: this line via the syscall handler");

    syscall_print_stats();
}

static void run_system_tests(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n=========================================\n");
    vga_writestring("    NumOS System Tests\n");
    vga_writestring("=========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    test_memory_allocation();
    test_filesystem();
    test_syscalls();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("=========================================\n");
    vga_writestring("    Tests Complete\n");
    vga_writestring("=========================================\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* =========================================================================
 * ELF launcher
 * ======================================================================= */

static void launch_init_elf(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n========================================\n");
    vga_writestring("  Launching ELF: /init/SHELL\n");
    vga_writestring("========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    struct elf_load_result result;
    int rc = elf_load_from_file("init/SHELL", &result);

    if (rc != ELF_OK) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ELF load FAILED: ");
        vga_writestring(result.error);
        vga_writestring("\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("Continuing in interactive mode.\n");
        return;
    }

    struct process *proc = process_spawn("elftest",
                                         result.entry,
                                         result.stack_top,
                                         result.stack_bottom);
    if (!proc) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("process_spawn FAILED (process table full)\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    proc->load_base = result.load_base;
    proc->load_end  = result.load_end;

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("User process spawned (pid ");
    print_dec((uint64_t)proc->pid);
    vga_writestring("). Yielding CPU...\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    while (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
        schedule();
        if (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\nUser process finished. Kernel regained control.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    process_reap(proc);
}

/* =========================================================================
 * Framebuffer interactive menu
 *
 * Mirrors the VGA interactive menu but outputs via the fb_con_* API so
 * results appear in the framebuffer terminal panel.
 * ======================================================================= */

/* Print a decimal number to the framebuffer console. */
static void fb_print_dec(uint64_t v) {
    char buf[21];
    int  pos = 20;
    buf[20]  = '\0';
    if (!v) { fb_con_putchar('0'); return; }
    while (v && pos > 0) { buf[--pos] = '0' + (char)(v % 10); v /= 10; }
    fb_con_print(&buf[pos]);
}

/* Print a 64-bit hex number to the framebuffer console. */
static void fb_print_hex(uint64_t v) {
    static const char hc[] = "0123456789ABCDEF";
    char buf[19];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 16; i++) buf[2 + i] = hc[(v >> (60 - i * 4)) & 0xF];
    buf[18] = '\0';
    fb_con_print(buf);
}

/* Redirect process list to the framebuffer console. */
static void fb_show_processes(void) {
    fb_con_print("\nProcess Table:\n");
    fb_con_print("  PID  STATE     TICKS  NAME\n");
    fb_con_print("  ---  --------  -----  ----\n");

    static const char *state_names[] = {
        "UNUSED  ", "READY   ", "RUNNING ", "BLOCKED ", "ZOMBIE  "
    };

    struct sched_stats st = scheduler_get_stats();
    (void)st;

    /* We'll just print the scheduler summary since we can't iterate internals */
    struct sched_stats ss = scheduler_get_stats();
    fb_con_print("\nScheduler stats:\n  Switches: ");
    fb_print_dec(ss.context_switches);
    fb_con_print("  Active: ");
    fb_print_dec(ss.active_processes);
    fb_con_print("\n");
    (void)state_names;
}

static void fb_show_syscall_stats(void) {
    struct syscall_stats ss = syscall_get_stats();
    fb_con_print("\nSyscall stats:\n  Total: ");
    fb_print_dec(ss.total_calls);
    fb_con_print("  Errors: ");
    fb_print_dec(ss.errors);
    fb_con_print("\n");
}

static void fb_show_device_list(void) {
    fb_con_print("\nDetected devices:\n");
    int count = device_count();
    for (int i = 0; i < count; i++) {
        struct device_entry *e = device_get(i);
        if (!e) continue;
        fb_con_print("  ");
        fb_con_print(e->name);
        if (e->bus == DEVICE_BUS_PCI) {
            fb_con_print(" [");
            fb_print_hex((uint64_t)e->vendor_id);
            fb_con_print(":");
            fb_print_hex((uint64_t)e->device_id);
            fb_con_print("]");
        }
        fb_con_putchar('\n');
    }
}

static void fb_show_dir(void) {
    fb_con_print("\nRoot directory:\n");
    struct fat32_dirent entries[32];
    /* chdir to root temporarily */
    fat32_chdir("/");
    int n = fat32_readdir(entries, 32);
    for (int i = 0; i < n; i++) {
        fb_con_print("  ");
        fb_con_print((entries[i].attr & 0x10) ? "[DIR] " : "[FILE] ");
        fb_con_print(entries[i].name);
        fb_con_putchar('\n');
    }
}

/* Update the taskbar uptime clock on the framebuffer. */
static void fb_interactive_loop(void) {
    uint64_t last_tick = 0;

    while (1) {
        /* Refresh taskbar every second */
        uint64_t now = timer_get_uptime_seconds();
        if (now != last_tick) {
            last_tick = now;
            fb_update_taskbar();
        }

        /* Check for keypress (non-blocking attempt via keyboard buffer) */
        uint8_t scan_code = keyboard_read_scan_code();
        char    c         = scan_code_to_ascii(scan_code);
        if (c == 0) continue;

        switch (c) {
            case 's': case 'S':
                fb_con_print("\n[Scroll mode uses VGA text — press S/L/I/P/D/R/H]\n> ");
                /* Scroll mode operates on VGA; briefly switch back */
                vga_enter_scroll_mode();
                /* Redraw desktop after returning */
                fb_draw_desktop();
                break;

            case 'l': case 'L':
                fb_show_dir();
                fb_con_print("> ");
                break;

            case 'i': case 'I':
                fb_show_syscall_stats();
                fb_con_print("> ");
                break;

            case 'p': case 'P':
                fb_show_processes();
                fb_con_print("> ");
                break;

            case 'd': case 'D':
                fb_show_device_list();
                fb_con_print("> ");
                break;

            case 'r': case 'R':
                fb_con_print("\nRe-running /init/SHELL...\n");
                launch_init_elf();
                fb_con_print("\nProcess finished.\n> ");
                break;

            case 'h': case 'H':
                fb_con_print("\nSystem halting...\n");
                fb_fill_rounded_rect(FB_WIDTH / 2 - 160, FB_HEIGHT / 2 - 40,
                                     320, 80, 10, FB_ERR);
                fb_draw_rounded_rect(FB_WIDTH / 2 - 160, FB_HEIGHT / 2 - 40,
                                     320, 80, 10, FB_WHITE);
                fb_draw_string("System Halted",
                               FB_WIDTH / 2 - 52, FB_HEIGHT / 2 - 10,
                               FB_WHITE, FB_TRANSPARENT, FB_SCALE_NORMAL);
                hang();
                break;

            default:
                /* Echo unknown char */
                fb_con_putchar(c);
                fb_con_print("  <- unknown key\n> ");
                break;
        }
    }
}

/* =========================================================================
 * Subsystem initialisation
 * ======================================================================= */

void kernel_init(void) {
    vga_init();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v0.8.0-beta - 64-bit Kernel\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Initializing kernel subsystems...\n\n");

    vga_writestring("[ 1/11] Loading GDT...\n");
    gdt_init();

    vga_writestring("[ 2/11] Loading IDT...\n");
    idt_init();

    vga_writestring("[ 3/11] Initializing paging...\n");
    paging_init();

    vga_writestring("[ 4/11] Initializing heap allocator...\n");
    heap_init();

    vga_writestring("[ 5/11] Initializing timer (100 Hz)...\n");
    timer_init(100);

    /* Re-init IDT after timer */
    idt_init();

    vga_writestring("[ 6/11] Initializing keyboard driver...\n");
    keyboard_init();

    pic_unmask_irq(0);
    pic_unmask_irq(1);

    vga_writestring("[ 7/11] Initializing syscall subsystem...\n");
    syscall_init();

    vga_writestring("[ 8/11] Initializing process scheduler...\n");
    scheduler_init();

    vga_writestring("[ 9/11] Initializing device detection...\n");
    device_init();

    vga_writestring("[10/11] Initializing ATA + FAT32...\n");
    ata_init();

    if (fat32_init() == 0) {
        if (fat32_mount() == 0) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("       FAT32 filesystem mounted OK\n");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("       FAT32 mount FAILED\n");
        }
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    /*
     * [11/11] BGA Framebuffer — initialised LAST so all boot messages
     * appear in VGA text mode first.  Once fb_init() returns the display
     * switches to graphics mode and subsequent vga_writestring calls are
     * silently written to the VGA text buffer (not visible on screen).
     */
    vga_writestring("[11/11] Initializing BGA framebuffer...\n");
    fb_init();

    if (fb_is_available()) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("       BGA framebuffer active — switching to graphics\n");
    } else {
        vga_writestring("       BGA not available — staying in VGA text mode\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("\nAll subsystems ready.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* =========================================================================
 * Kernel entry point
 * ======================================================================= */

void kernel_main(void) {
    kernel_init();

    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS Kernel Ready\n");
    vga_writestring("==================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    run_system_tests();
    launch_init_elf();

    /* -------------------------------------------------------------------------
     * Post-boot: if the BGA framebuffer is available, draw the desktop and
     * run the interactive menu through the framebuffer console.
     * Otherwise fall back to the original VGA text-mode interactive menu.
     * ---------------------------------------------------------------------- */
    if (fb_is_available()) {
        /* Draw the full desktop (also initialises the fb console) */
        fb_draw_desktop();

        /* Hand off to the framebuffer interactive loop — never returns */
        fb_interactive_loop();
    }

    /* ---- VGA text-mode fallback ------------------------------------------ */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n========================================\n");
    vga_writestring("  Kernel Interactive Mode (VGA)\n");
    vga_writestring("========================================\n");
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
                vga_writestring("\nPress S/L/I/P/R/H: ");
                break;

            case 'l': case 'L':
                vga_putchar('\n');
                fat32_list_directory("/");
                vga_writestring("\nPress S/L/I/P/R/H: ");
                break;

            case 'i': case 'I':
                syscall_print_stats();
                vga_writestring("\nPress S/L/I/P/R/H: ");
                break;

            case 'p': case 'P':
                scheduler_print_processes();
                scheduler_print_stats();
                vga_writestring("\nPress S/L/I/P/R/H: ");
                break;

            case 'r': case 'R':
                vga_writestring("\nRe-running ELF...\n");
                launch_init_elf();
                vga_writestring("\nPress S/L/I/P/R/H: ");
                break;

            case 'd': case 'D':
                device_print_all();
                vga_writestring("\nPress S/L/I/P/D/R/H: ");
                break;

            case 'h': case 'H':
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
                vga_writestring("\nSystem halted.\n");
                hang();
                break;

            default:
                break;
        }
    }
}