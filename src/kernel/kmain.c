#include "kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/paging.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "cpu/heap.h"
#include "fs/fat32.h"
#include "usr/shell.h"

void kernel_init(void) {
    /* Initialize VGA text mode first so we can see output */
    vga_init();
    
    /* Display early boot message */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v2.1 - 64-bit Operating System\n");
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
    
    /* Initialize IDT (this will also initialize PIC and enable interrupts) */
    vga_writestring("Loading IDT and enabling interrupts...\n");
    idt_init();
    
    /* Initialize keyboard */
    vga_writestring("Initializing keyboard driver...\n");
    keyboard_init();
    
    /* Unmask timer and keyboard IRQs */
    pic_unmask_irq(0); /* Timer */
    pic_unmask_irq(1); /* Keyboard */
    
    /* Display subsystem initialization status */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("\nCore subsystems initialized successfully!\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("- GDT loaded and active\n");
    vga_writestring("- Enhanced 4-level paging enabled\n");
    vga_writestring("- Kernel heap allocator ready\n");
    vga_writestring("- Timer running at 100Hz\n");
    vga_writestring("- IDT loaded with exception/IRQ handlers\n");
    vga_writestring("- Hardware interrupts enabled\n");
    vga_writestring("- Keyboard input ready\n");
    
    /* Initialize FAT32 filesystem */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing FAT32 filesystem...\n");
    int fat32_result = fat32_init();
    if (fat32_result == FAT32_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 filesystem driver initialized\n");
        
        /* Attempt to mount the filesystem */
        fat32_result = fat32_mount();
        if (fat32_result == FAT32_SUCCESS) {
            vga_writestring("- FAT32 filesystem mounted successfully\n");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
            vga_writestring("- FAT32 mount failed (disk may not be formatted)\n");
            vga_writestring("  Use 'fat32mount' command to retry\n");
        }
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 initialization failed\n");
        vga_writestring("  Filesystem commands will not be available\n");
    }
    
    /* Initialize shell */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing command shell...\n");
    shell_init();
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("- Shell initialized with built-in commands\n");
    vga_writestring("- Command registry ready\n");
    
    /* Show system summary */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("\n" "System Ready!\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Display memory statistics */
    vga_writestring("Memory Status:\n");
    vga_writestring("  Physical frames: ");
    print_dec(pmm_get_free_frames());
    vga_writestring(" free / ");
    print_dec(pmm_get_total_frames());
    vga_writestring(" total\n");
    
    struct heap_stats heap_stats = heap_get_stats();
    vga_writestring("  Heap memory:     ");
    print_dec(heap_stats.free_size);
    vga_writestring(" bytes free / ");
    print_dec(heap_stats.total_size);
    vga_writestring(" bytes total\n");
    
    /* Display uptime */
    vga_writestring("  Boot time:       ");
    print_dec(timer_get_uptime_ms());
    vga_writestring(" ms\n");
    
    vga_writestring("\n");
}

void kernel_main(void) {
    /* Initialize all kernel subsystems */
    kernel_init();
    
    /* Start the shell - this will run the main command loop */
    vga_clear();
    shell_run();
    
    /* If shell exits, clean up and halt */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nShell exited. Cleaning up...\n");
    
    shell_shutdown();
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("System shutdown complete.\n");
    
    /* Halt the system */
    hang();
}