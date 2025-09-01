#include "kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/disk.h"
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
    vga_writestring("NumOS v2.2 - 64-bit Operating System with Persistent Storage\n");
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
    
    /* Initialize disk subsystem */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("Initializing disk subsystem...\n");
    int disk_result = disk_init();
    if (disk_result == DISK_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("- Disk subsystem initialized successfully\n");
        vga_writestring("- Default disk image created/mounted\n");
        vga_writestring("- Disk cache enabled\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("- Disk initialization failed\n");
        vga_writestring("  File operations may not persist\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
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
    vga_writestring("- Persistent disk storage available\n");
    
    /* Initialize FAT32 filesystem */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing FAT32 filesystem with disk persistence...\n");
    int fat32_result = fat32_init();
    if (fat32_result == FAT32_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 filesystem driver initialized\n");
        
        /* Attempt to mount the filesystem */
        fat32_result = fat32_mount();
        if (fat32_result == FAT32_SUCCESS) {
            vga_writestring("- FAT32 filesystem mounted successfully\n");
            vga_writestring("- File operations will persist across reboots\n");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
            vga_writestring("- FAT32 mount failed (disk may not be formatted)\n");
            vga_writestring("  Use 'fat32mount' command to retry\n");
            vga_writestring("  Data will still be stored but may not be FAT32 compatible\n");
        }
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 initialization failed\n");
        vga_writestring("  Filesystem commands will not be available\n");
    }
    
}

void kernel_main(void) {
    /* Initialize all kernel subsystems */
    kernel_init();
    
    /* Start the shell - this will run the main command loop */
    // vga_clear();
    //  shell_run();
    
    /* If shell exits, clean up and halt */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nShell exited. Cleaning up...\n");
    
    /* Shutdown subsystems in reverse order */
    shell_shutdown();
    
    /* Unmount FAT32 and flush caches */
    fat32_unmount();
    
    /* Shutdown disk subsystem (flushes all caches) */
    disk_shutdown();
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("All data flushed to persistent storage.\n");
    vga_writestring("System shutdown complete.\n");
    
    /* Halt the system */
    hang();
}