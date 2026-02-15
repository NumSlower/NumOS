/*
 * kmain.c - Kernel main with FAT32 filesystem support
 */

#include "kernel/kernel.h"
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

void kernel_init(void) {
    /* Initialize VGA text mode first so we can see output */
    vga_init();
    
    /* Display early boot message */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v3.0 - 64-bit Kernel with FAT32\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Initializing kernel subsystems...\n\n");
    
    /* Initialize GDT */
    vga_writestring("Loading GDT...\n");
    gdt_init();

    /* Initialize IDT */
    vga_writestring("Loading IDT...\n");
    idt_init();
    
    /* Initialize enhanced paging system */
    vga_writestring("Initializing paging system...\n");
    paging_init();
    
    /* Initialize heap allocator */
    vga_writestring("Initializing heap allocator...\n");
    heap_init();
    
    /* Initialize timer (100Hz = 10ms ticks) */
    vga_writestring("Initializing timer (100Hz)...\n");
    timer_init(100);
    
    /* Initialize IDT */
    vga_writestring("Loading IDT and enabling interrupts...\n");
    idt_init();

    /* Initialize keyboard */
    vga_writestring("Initializing keyboard driver...\n");
    keyboard_init();
    
    /* Unmask timer and keyboard IRQs */
    pic_unmask_irq(0); /* Timer */
    pic_unmask_irq(1); /* Keyboard */
    
    /* Initialize ATA/IDE disk controller */
    vga_writestring("\n");
    ata_init();
    
    /* Initialize FAT32 filesystem */
    vga_writestring("\n");
    if (fat32_init() == 0) {
        if (fat32_mount() == 0) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("✓ Filesystem mounted successfully\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }
    
    /* Display system summary */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("\nSystem Ready!\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

/* Simple test functions to demonstrate kernel capabilities */
void test_memory_allocation(void) {
    vga_writestring("\n=== Memory Allocation Test ===\n");
    
    /* Test kmalloc */
    vga_writestring("Testing kmalloc(1024)... ");
    void *ptr1 = kmalloc(1024);
    if (ptr1) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK ");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        print_hex((uint64_t)ptr1);
        vga_putchar('\n');
        
        /* Write some data */
        memset(ptr1, 0xAB, 1024);
        kfree(ptr1);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    /* Test kzalloc */
    vga_writestring("Testing kzalloc(2048)... ");
    void *ptr2 = kzalloc(2048);
    if (ptr2) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK ");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        print_hex((uint64_t)ptr2);
        vga_putchar('\n');
        kfree(ptr2);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    /* Test kcalloc */
    vga_writestring("Testing kcalloc(10, 512)... ");
    void *ptr3 = kcalloc(10, 512);
    if (ptr3) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK ");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        print_hex((uint64_t)ptr3);
        vga_putchar('\n');
        kfree(ptr3);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    /* Print heap statistics */
    vga_putchar('\n');
    heap_print_stats();
}

void test_paging(void) {
    vga_writestring("\n=== Paging System Test ===\n");
    
    /* Test virtual memory allocation */
    vga_writestring("Testing vmm_alloc_pages(4)... ");
    void *virt_pages = vmm_alloc_pages(4, PAGE_PRESENT | PAGE_WRITABLE);
    if (virt_pages) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK ");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        print_hex((uint64_t)virt_pages);
        vga_putchar('\n');
        
        /* Test writing to allocated pages */
        vga_writestring("Writing to allocated pages... ");
        memset(virt_pages, 0x42, PAGE_SIZE * 4);
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        vmm_free_pages(virt_pages, 4);
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    /* Print paging statistics */
    vga_putchar('\n');
    paging_print_stats();
}

void test_filesystem(void) {
    vga_writestring("\n=== Filesystem Test ===\n");
    
    /* Print filesystem information */
    fat32_print_info();
    
    /* List root directory contents */
    vga_writestring("\n");
    fat32_list_directory("/");
    
    /* Test directory creation - check if it exists first */
    vga_writestring("\nTesting mkdir('/test')... ");
    
    /* Check if test directory already exists */
    struct fat32_dirent test_info;
    if (fat32_stat("test", &test_info) == 0) {
        /* Directory already exists */
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_writestring("SKIP (already exists)\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        /* Directory doesn't exist, try to create it */
        if (fat32_mkdir("test") == 0) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("OK\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* List directory again to show new folder */
            vga_writestring("\nUpdated root directory:\n");
            fat32_list_directory("/");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("FAILED\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }
}

void run_system_tests(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n");
    vga_writestring("=========================================\n");
    vga_writestring("    NumOS System Tests\n");
    vga_writestring("=========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Run tests */
    test_memory_allocation();
    test_paging();
    test_filesystem();
    
    /* Final summary */
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("=========================================\n");
    vga_writestring("    Tests Complete\n");
    vga_writestring("=========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void kernel_main(void) {
    /* Initialize all kernel subsystems */
    kernel_init();
    
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS Kernel Ready with FAT32 Support\n");
    vga_writestring("======================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Running system tests...\n");
    
    /* Run system tests */
    run_system_tests();

    /* ── Kernel is now ready ──────────────────────────────────
     * The kernel has completed initialization and testing.
     * We now enter an interactive prompt where the user can:
     *   - Browse filesystem
     *   - Review boot messages (scroll mode)
     *   - Manually load user-space programs
     *   - Or just idle
     * ────────────────────────────────────────────────────────── */
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("========================================\n");
    vga_writestring("  Kernel Ready - Interactive Mode\n");
    vga_writestring("========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_writestring("\nAvailable commands:\n");
    vga_writestring("  [S] - Enter scroll mode (review boot messages)\n");
    vga_writestring("  [L] - List root directory\n");
    vga_writestring("  [H] - Halt system\n");
    vga_writestring("\nPress a key to continue...\n");
    
    /* Interactive command loop */
    while (1) {
        uint8_t scan_code = keyboard_read_scan_code();
        char c = scan_code_to_ascii(scan_code);
        
        if (c == 0) {
            continue;  /* Ignore non-ASCII keys */
        }
        
        if (c == 's' || c == 'S') {
            /* Enter scroll mode */
            vga_writestring("\nEntering scroll mode...\n");
            vga_enter_scroll_mode();
            vga_writestring("\nExited scroll mode.\n");
            vga_writestring("Press S/L/H: ");
            
        } else if (c == 'l' || c == 'L') {
            /* List directory */
            vga_writestring("\n");
            fat32_list_directory("/");
            vga_writestring("\nPress S/L/H: ");
            
        } else if (c == 'h' || c == 'H') {
            /* Halt */
            vga_writestring("\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("System halted by user.\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            hang();  /* Halt permanently */
        }
    }

    /* Halt */
    while (1) {
        __asm__ volatile("hlt");
    }
}