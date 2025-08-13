#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "pic.h"

void kernel_init(void) {
    // Initialize VGA text mode first so we can see output
    vga_init();
    
    // Initialize GDT
    gdt_init();
    
    // Initialize paging system
    paging_init();
    
    // Initialize IDT (this will also initialize PIC and enable interrupts)
    idt_init();
    
    // Initialize keyboard
    keyboard_init();
    
    // Unmask keyboard IRQ
    pic_unmask_irq(1);
    
    // Display welcome message
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("Welcome to NumOS - 64-bit Operating System\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("System initialized successfully!\n");
    vga_writestring("- GDT loaded\n");
    vga_writestring("- Paging active\n");
    vga_writestring("- IDT loaded with exception/IRQ handlers\n");
    vga_writestring("- Interrupts enabled\n");
    vga_writestring("- Keyboard ready\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("\nType 'help' for available commands.\n\n");
}

void kernel_main(void) {
    // Initialize kernel subsystems
    kernel_init();
    
    // Command buffer
    char command_buffer[256];
    
    // Main command loop
    while (1) {
        // Print prompt
        print_prompt();
        
        // Read command from user
        keyboard_read_line(command_buffer, sizeof(command_buffer));
        
        // Process the command
        process_command(command_buffer);
    }
}