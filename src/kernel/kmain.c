#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "pic.h"
#include "timer.h"
#include "heap.h"
#include "ata.h"
#include "fat32.h"

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
    
    // Initialize ATA/IDE disk driver
    if (ata_init() == 0) {
        vga_writestring("ATA driver initialized successfully\n");
    } else {
        vga_writestring("Warning: ATA driver initialization failed\n");
    }
    
    // Unmask timer and keyboard IRQs
    pic_unmask_irq(0); // Timer
    pic_unmask_irq(1); // Keyboard
    
    // Display welcome message
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("Welcome to NumOS - 64-bit Operating System with File System Support\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("System initialized successfully!\n");
    vga_writestring("- GDT loaded\n");
    vga_writestring("- Enhanced paging active\n");
    vga_writestring("- Heap allocator ready\n");
    vga_writestring("- Timer running at 100Hz\n");
    vga_writestring("- IDT loaded with exception/IRQ handlers\n");
    vga_writestring("- Interrupts enabled\n");
    vga_writestring("- Keyboard ready\n");
    vga_writestring("- ATA/IDE disk driver loaded\n");
    vga_writestring("- FAT32 file system support available\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nTo get started with file operations:\n");
    vga_writestring("1. Type 'drives' to see available disk drives\n");
    vga_writestring("2. Type 'mount 0' to mount the first drive\n");
    vga_writestring("3. Type 'ls' to list files in the root directory\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("\nType 'help' for all available commands.\n\n");
    
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
    
    // Show available drives on startup
    vga_writestring("Scanning for disk drives...\n");
    ata_print_drives();
    vga_putchar('\n');
    
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