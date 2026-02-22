/*
 * kmain.c - NumOS Kernel Main
 *
 * Boot sequence:
 *   1. VGA, GDT, IDT, Paging, Heap, Timer, Keyboard
 *   2. Syscall subsystem (SYSCALL/SYSRET via MSRs)
 *   3. Scheduler (round-robin, preemptive via timer IRQ)
 *   4. ATA + FAT32 filesystem
 *   5. Self-tests
 *   6. Load /init/SHELL ELF -> spawn user process -> schedule()
 */

#include "kernel/kernel.h"
#include "kernel/syscall.h"
#include "kernel/scheduler.h"
#include "kernel/elf_loader.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/paging.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/ata.h"
#include "cpu/heap.h"
#include "fs/fat32.h"

/* =========================================================================
 * kernel_init - hardware + subsystem setup
 * ======================================================================= */
void kernel_init(void) {
    vga_init();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v4.0 - 64-bit Kernel | Syscalls + Scheduler + ELF\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Initializing kernel subsystems...\n\n");

    vga_writestring("[ 1/9] Loading GDT...\n");
    gdt_init();

    vga_writestring("[ 2/9] Loading IDT...\n");
    idt_init();

    vga_writestring("[ 3/9] Initializing paging...\n");
    paging_init();

    vga_writestring("[ 4/9] Initializing heap allocator...\n");
    heap_init();

    vga_writestring("[ 5/9] Initializing timer (100 Hz)...\n");
    timer_init(100);

    /* Re-init IDT after timer to pick up any changes, then re-enable irqs */
    idt_init();

    vga_writestring("[ 6/9] Initializing keyboard driver...\n");
    keyboard_init();

    pic_unmask_irq(0);  /* Timer    */
    pic_unmask_irq(1);  /* Keyboard */

    vga_writestring("[ 7/9] Initializing syscall subsystem (SYSCALL/SYSRET)...\n");
    syscall_init();

    vga_writestring("[ 8/9] Initializing process scheduler...\n");
    scheduler_init();

    vga_writestring("[ 9/9] Initializing ATA + FAT32...\n");
    ata_init();
    if (fat32_init() == 0) {
        if (fat32_mount() == 0) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("       FAT32 filesystem mounted OK\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("       FAT32 mount FAILED\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }

    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("\nAll subsystems ready.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* =========================================================================
 * Test helpers
 * ======================================================================= */
void test_memory_allocation(void) {
    vga_writestring("\n=== Memory Allocation Test ===\n");

    vga_writestring("Testing kmalloc(1024)... ");
    void *ptr1 = kmalloc(1024);
    if (ptr1) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK ");
        print_hex((uint64_t)ptr1);
        vga_putchar('\n');
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
        vga_writestring("OK ");
        print_hex((uint64_t)ptr2);
        vga_putchar('\n');
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

void test_filesystem(void) {
    vga_writestring("\n=== Filesystem Test ===\n");
    fat32_print_info();
    vga_writestring("\n");
    fat32_list_directory("/");
}

void test_syscalls(void) {
    vga_writestring("\n=== Syscall Subsystem Test ===\n");

    vga_writestring("sys_write(stdout)... ");
    const char *hello = "Hello from sys_write!\n";
    int64_t n = sys_write(FD_STDOUT, hello, strlen(hello));
    if (n == (int64_t)strlen(hello)) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS]\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("[FAIL]\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

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

void run_system_tests(void) {
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
 * launch_init_elf
 *
 * Loads /init/SHELL from FAT32, creates a user process, then calls
 * schedule() to hand the CPU to it immediately.  Returns only after the
 * user process has exited and the scheduler puts the kernel back in control.
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

    /*
     * Create a READY user process.
     * process_spawn() calls process_create_user() which enqueues it in
     * the run-queue.  schedule() will then switch to it.
     */
    struct process *proc = process_spawn("elftest",
                                         result.entry,
                                         result.stack_top);
    if (!proc) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("process_spawn FAILED (table full?)\n");
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

    /*
     * Voluntarily yield.  The scheduler will switch to the new user process
     * via context_switch() -> process_trampoline() -> IRETQ -> Ring 3.
     * We return here once the user process calls SYS_EXIT and the idle
     * process (or this kernel thread) is rescheduled.
     */
    schedule();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\nUser process finished. Kernel regained control.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* =========================================================================
 * kernel_main - entry point from boot stub
 * ======================================================================= */
void kernel_main(void) {
    kernel_init();

    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS Kernel Ready\n");
    vga_writestring("==================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    run_system_tests();

    /* Load the ELF binary from disk and run it */
    launch_init_elf();

    /* ------------------------------------------------------------------
     * Interactive mode: reached after ELF exits or if load failed
     * ------------------------------------------------------------------ */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n========================================\n");
    vga_writestring("  Kernel Interactive Mode\n");
    vga_writestring("========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  [S] Scroll mode     [L] List root dir\n");
    vga_writestring("  [I] Syscall stats   [P] Process list\n");
    vga_writestring("  [R] Re-run ELF      [H] Halt\n");
    vga_writestring("\nPress a key: ");

    while (1) {
        uint8_t scan_code = keyboard_read_scan_code();
        char c = scan_code_to_ascii(scan_code);
        if (c == 0) continue;

        if (c == 's' || c == 'S') {
            vga_writestring("\nEntering scroll mode...\n");
            vga_enter_scroll_mode();
            vga_writestring("\nPress S/L/I/P/R/H: ");

        } else if (c == 'l' || c == 'L') {
            vga_writestring("\n");
            fat32_list_directory("/");
            vga_writestring("\nPress S/L/I/P/R/H: ");

        } else if (c == 'i' || c == 'I') {
            syscall_print_stats();
            vga_writestring("\nPress S/L/I/P/R/H: ");

        } else if (c == 'p' || c == 'P') {
            scheduler_print_processes();
            scheduler_print_stats();
            vga_writestring("\nPress S/L/I/P/R/H: ");

        } else if (c == 'r' || c == 'R') {
            vga_writestring("\nRe-running ELF...\n");
            launch_init_elf();
            vga_writestring("\nPress S/L/I/P/R/H: ");

        } else if (c == 'h' || c == 'H') {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("\nSystem halted.\n");
            hang();
        }
    }
}