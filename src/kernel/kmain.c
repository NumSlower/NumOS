/*
 * kmain.c - Kernel main without filesystem/disk support
 */

#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/paging.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "cpu/heap.h"

void kernel_init(void) {
    /* Initialize VGA text mode first so we can see output */
    vga_init();
    
    /* Display early boot message */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v2.5 - 64-bit Kernel\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Initializing kernel subsystems...\n\n");
    
    /* Initialize GDT */
    vga_writestring("Loading GDT...\n");
    gdt_init();
    
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
    
    /* Print VM regions */
    vga_putchar('\n');
    paging_print_vm_regions();
}

void test_timer(void) {
    vga_writestring("\n=== Timer Test ===\n");
    
    struct timer_stats stats = timer_get_stats();
    
    vga_writestring("Timer frequency: ");
    print_dec(stats.frequency);
    vga_writestring(" Hz\n");
    
    vga_writestring("Current ticks: ");
    print_dec(stats.ticks);
    vga_putchar('\n');
    
    vga_writestring("Uptime: ");
    print_dec(stats.seconds);
    vga_writestring(" seconds (");
    print_dec(stats.uptime_ms);
    vga_writestring(" ms)\n");
    
    /* Test timer delay */
    vga_writestring("\nTesting 1 second delay... ");
    uint64_t start = timer_get_uptime_ms();
    timer_sleep(1000);
    uint64_t end = timer_get_uptime_ms();
    uint64_t elapsed = end - start;
    
    vga_writestring("Elapsed: ");
    print_dec(elapsed);
    vga_writestring(" ms ");
    
    if (elapsed >= 990 && elapsed <= 1010) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("OK\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("FAILED\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void test_keyboard(void) {
    vga_writestring("\n=== Keyboard Test ===\n");
    vga_writestring("Type some characters (press Enter to finish):\n> ");
    
    char buffer[128];
    int pos = 0;
    
    while (pos < 127) {
        char c = keyboard_getchar();
        
        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            vga_putchar('\n');
            break;
        } else if (c == '\b') {
            if (pos > 0) {
                pos--;
                vga_putchar('\b');
            }
        } else if (c >= 32 && c <= 126) {
            buffer[pos++] = c;
            vga_putchar(c);
        }
    }
    
    if (pos >= 127) {
        buffer[127] = '\0';
    }
    
    vga_writestring("\nYou typed: \"");
    vga_writestring(buffer);
    vga_writestring("\" (");
    print_dec(pos);
    vga_writestring(" characters)\n");
}

void run_system_tests(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n");
    vga_writestring("=====================================\n");
    vga_writestring("    NumOS System Tests\n");
    vga_writestring("=====================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Run tests */
    test_memory_allocation();
    test_paging();
    test_timer();
    test_keyboard();
    
    /* Final summary */
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("=====================================\n");
    vga_writestring("    Tests Complete\n");
    vga_writestring("=====================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void kernel_main(void) {
    /* Initialize all kernel subsystems */
    kernel_init();
    
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS Kernel Ready\n");
    vga_writestring("===================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Running system tests...\n");
    
    /* Run system tests */
    run_system_tests();
    
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("All tests completed! Use arrow keys to scroll.\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Press any key to enter scroll mode...\n");
    
    /* Wait for keypress */
    keyboard_getchar();
    
    /* Enter scroll mode for user to review output */
    vga_enter_scroll_mode();
    
    /* After exiting scroll mode, idle */
    vga_writestring("\nSystem idle - Press Ctrl+Alt+Del to reboot\n");
    
    /* Idle loop */
    while (1) {
        __asm__ volatile("hlt");
    }
}