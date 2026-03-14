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
 * executed as the first user process.  When it exits, the kernel enters an
 * interactive menu that supports re-running the ELF, scrollback, and more.
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
#include "drivers/device.h"
#include "cpu/heap.h"
#include "fs/fat32.h"

/* =========================================================================
 * Self-test helpers
 * ======================================================================= */

/*
 * test_memory_allocation - exercise kmalloc/kzalloc and print heap stats.
 */
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

/*
 * test_filesystem - print FAT32 volume info and list the root directory.
 */
static void test_filesystem(void) {
    vga_writestring("\n=== Filesystem Test ===\n");
    fat32_print_info();
    vga_putchar('\n');
    fat32_list_directory("/");
}

/*
 * test_syscalls - exercise each syscall implementation directly from C
 * and print a pass/fail result for each.
 */
static void test_syscalls(void) {
    vga_writestring("\n=== Syscall Subsystem Test ===\n");

    /* sys_write: write to stdout */
    vga_writestring("sys_write(stdout)... ");
    const char *hello     = "Hello from sys_write!\n";
    int64_t     written   = sys_write(FD_STDOUT, hello, strlen(hello));
    if (written == (int64_t)strlen(hello)) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS]\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("[FAIL]\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* sys_write: bad file descriptor should return EBADF */
    vga_writestring("sys_write(bad_fd)... ");
    if (sys_write(99, "x", 1) == SYSCALL_EBADF) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("[PASS] EBADF\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }

    /* sys_getpid */
    vga_writestring("sys_getpid()... ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("[PASS] pid=");
    print_dec((uint64_t)sys_getpid());
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* sys_uptime_ms */
    vga_writestring("sys_uptime_ms()... ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("[PASS] uptime=");
    print_dec((uint64_t)sys_uptime_ms());
    vga_writestring(" ms\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* sys_puts */
    vga_writestring("sys_puts()... ");
    sys_puts("[PASS] sys_puts: this line via the syscall handler");

    syscall_print_stats();
}

/*
 * run_system_tests - run all built-in self-tests in sequence.
 */
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

/*
 * launch_init_elf - load /init/SHELL from FAT32 and run it as a user process.
 *
 * Returns after the user process exits.  If loading fails, returns immediately
 * so the caller drops into interactive mode.
 *
 * The kernel busy-waits in a "sti; hlt" loop while the process runs so it
 * does not consume a scheduler slot.  It calls schedule() on each wakeup
 * in case the process is blocked and needs another runnable process to run.
 */
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

    /* Create a READY user process; enqueued by process_spawn() */
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

    /* Store the ELF load extent for virtual memory cleanup on exit */
    proc->load_base = result.load_base;
    proc->load_end  = result.load_end;

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("User process spawned (pid ");
    print_dec((uint64_t)proc->pid);
    vga_writestring("). Yielding CPU...\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* Yield and keep re-scheduling until the process terminates */
    while (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
        schedule();
        if (proc->state != PROC_ZOMBIE && proc->state != PROC_UNUSED) {
            __asm__ volatile("sti; hlt" ::: "memory");
        }
    }

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\nUser process finished. Kernel regained control.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* Free the PCB slot so repeated [R] does not exhaust the process table */
    process_reap(proc);
}

/* =========================================================================
 * Subsystem initialisation
 * ======================================================================= */

/*
 * kernel_init - initialise all hardware and kernel subsystems in order.
 */
void kernel_init(void) {
    vga_init();

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v0.8.0-beta - 64-bit Kernel\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Initializing kernel subsystems...\n\n");

    vga_writestring("[ 1/10] Loading GDT...\n");
    gdt_init();

    vga_writestring("[ 2/10] Loading IDT...\n");
    idt_init();

    vga_writestring("[ 3/10] Initializing paging...\n");
    paging_init();

    vga_writestring("[ 4/10] Initializing heap allocator...\n");
    heap_init();

    vga_writestring("[ 5/10] Initializing timer (100 Hz)...\n");
    timer_init(100);

    /* Re-init the IDT after the timer to pick up any late changes,
     * then re-enable interrupts. */
    idt_init();

    vga_writestring("[ 6/10] Initializing keyboard driver...\n");
    keyboard_init();

    /* Unmask timer (IRQ 0) and keyboard (IRQ 1) now that handlers are ready */
    pic_unmask_irq(0);
    pic_unmask_irq(1);

    vga_writestring("[ 7/10] Initializing syscall subsystem (SYSCALL/SYSRET)...\n");
    syscall_init();

    vga_writestring("[ 8/10] Initializing process scheduler...\n");
    scheduler_init();

    vga_writestring("[ 9/10] Initializing device detection...\n");
    device_init();

    vga_writestring("[ 10/10] Initializing ATA + FAT32...\n");
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

    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("\nAll subsystems ready.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* =========================================================================
 * Kernel entry point
 * ======================================================================= */

/*
 * kernel_main - called from the 64-bit boot stub in entry64.asm.
 *
 * Runs subsystem init, self-tests, and the ELF launcher.
 * Falls into an interactive menu after the user process exits (or if the
 * ELF could not be loaded).
 */
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
     * Interactive mode - reached after ELF exits or if ELF failed to load
     * ---------------------------------------------------------------------- */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n========================================\n");
    vga_writestring("  Kernel Interactive Mode\n");
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