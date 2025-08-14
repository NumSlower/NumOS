#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "pic.h"
#include "timer.h"
#include "heap.h"
#include "fat32.h"
#include "binary.h"  // Add binary loader support

void kernel_init(void) {
    // Initialize VGA text mode first so we can see output
    vga_init();
    
    // Initialize GDT
    gdt_init();
    
    // Initialize enhanced paging system
    paging_init();
    
    // Initialize heap allocator
    heap_init();
    
    // Initialize timer (100Hz = 10ms ticks)
    timer_init(100);
    
    // Initialize IDT (this will also initialize PIC and enable interrupts)
    idt_init();
    
    // Initialize keyboard
    keyboard_init();
    
    // Unmask timer and keyboard IRQs
    pic_unmask_irq(0); // Timer
    pic_unmask_irq(1); // Keyboard
    
    // Display welcome message
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("Welcome to NumOS - 64-bit Operating System\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("System initialized successfully!\n");
    vga_writestring("- GDT loaded\n");
    vga_writestring("- Enhanced paging active\n");
    vga_writestring("- Heap allocator ready\n");
    vga_writestring("- Timer running at 100Hz\n");
    vga_writestring("- IDT loaded with exception/IRQ handlers\n");
    vga_writestring("- Interrupts enabled\n");
    vga_writestring("- Keyboard ready\n");
    
    // Initialize FAT32 filesystem
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("Initializing FAT32 filesystem...\n");
    int fat32_result = fat32_init();
    if (fat32_result == FAT32_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 filesystem initialized\n");
        
        // Attempt to mount the filesystem
        fat32_result = fat32_mount();
        if (fat32_result == FAT32_SUCCESS) {
            vga_writestring("- FAT32 filesystem mounted\n");
            vga_writestring("- Binary loader ready\n");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("- FAT32 mount failed (use 'fat32mount' command)\n");
        }
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("- FAT32 initialization failed\n");
    }
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("\nType 'help' for available commands.\n");
    vga_writestring("File commands: ls, cat, create, write\n");
    vga_writestring("Binary commands: loadbin, execbin, listbin\n");
    vga_writestring("Try: 'testbin' to test the binary loader\n\n");
    
    // Show initial system status
    vga_writestring("Initial system uptime: ");
    print_dec(timer_get_uptime_ms());
    vga_writestring(" ms\n\n");
}

void kernel_main(void) {
    // Initialize kernel subsystems
    kernel_init();
    
    // Command buffer
    char *command_buffer = (char*)kmalloc(256);
    if (!command_buffer) {
        panic("Failed to allocate command buffer");
    }
    
    // Main command loop
    while (1) {
        // Print prompt
        print_prompt();
        
        // Read command from user
        keyboard_read_line(command_buffer, 256);
        
        // Process the command
        process_command(command_buffer);
    }
    
    // This should never be reached, but clean up just in case
    kfree(command_buffer);
}