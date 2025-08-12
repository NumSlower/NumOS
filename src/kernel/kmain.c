#include "kernel.h"
#include "vga.h"
#include "keyboard.h"

void kernel_init(void) {
    // Initialize VGA text mode
    vga_init();
    
    // Initialize keyboard
    keyboard_init();
    
    // Display welcome message
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("Welcome to NumOS\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Type 'help' for available commands.\n");
    vga_putchar('\n');
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