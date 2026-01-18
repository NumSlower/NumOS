/*
 * kmain.c - Kernel main without shell
 */

#include "kernel/kernel.h"
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
    
    /* Initialize disk subsystem */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing disk subsystem...\n");
    int disk_result = disk_init();
    if (disk_result == DISK_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("- Disk subsystem initialized successfully\n");
        vga_writestring("- ATA/IDE controller ready\n");
        vga_writestring("- Disk cache enabled\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("- Disk initialization failed\n");
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Unmask timer and keyboard IRQs */
    pic_unmask_irq(0); /* Timer */
    pic_unmask_irq(1); /* Keyboard */
    
    /* Initialize FAT32 filesystem */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing FAT32 filesystem...\n");
    int fat32_result = fat32_init();
    if (fat32_result == FAT32_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 filesystem driver initialized\n");
        
        fat32_result = fat32_mount();
        if (fat32_result == FAT32_SUCCESS) {
            vga_writestring("- FAT32 filesystem mounted successfully\n");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
            vga_writestring("- FAT32 mount failed (disk may need formatting)\n");
        }
    }
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Display system summary */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring("\nSystem Ready!\n");
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
    vga_writestring("Use UP/DOWN arrow keys (or W/S) to scroll through output\n");
    vga_writestring("Press 'Q' to exit scroll mode\n\n");
    
    /* Enter scroll mode for user to review boot messages */
    vga_enter_scroll_mode();
    
    /* After exiting scroll mode, idle */
    vga_writestring("System idle - Press Ctrl+Alt+Del to reboot\n");
    
    /* Idle loop */
    while (1) {
        __asm__ volatile("hlt");
    }
}