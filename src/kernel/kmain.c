/*
 * kmain.c - Kernel main without userspace support
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

/* Simple command buffer */
#define CMD_BUFFER_SIZE 256
static char cmd_buffer[CMD_BUFFER_SIZE];

void print_prompt(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("numos> ");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void process_command(const char *command);

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
    vga_writestring("Welcome to NumOS Kernel Shell\n");
    vga_writestring("Type 'help' for available commands\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Main command loop */
    while (1) {
        print_prompt();
        keyboard_read_line(cmd_buffer, CMD_BUFFER_SIZE);
        
        if (cmd_buffer[0] != '\0') {
            process_command(cmd_buffer);
        }
    }
}

void process_command(const char *command) {
    /* Skip leading whitespace */
    while (*command == ' ' || *command == '\t') {
        command++;
    }
    
    if (strcmp(command, "help") == 0) {
        vga_writestring("Available commands:\n");
        vga_writestring("  help      - Show this help message\n");
        vga_writestring("  clear     - Clear the screen\n");
        vga_writestring("  sysinfo   - Display system information\n");
        vga_writestring("  heap      - Show heap statistics\n");
        vga_writestring("  disk      - Show disk information\n");
        vga_writestring("  ls        - List files on disk\n");
        vga_writestring("  cat       - Display file contents (cat <filename>)\n");
        vga_writestring("  write     - Write to file (write <filename> <text>)\n");
        vga_writestring("  shutdown  - Shutdown the system\n");
        vga_writestring("  reboot    - Reboot the system\n");
        
    } else if (strcmp(command, "clear") == 0) {
        vga_clear();
        
    } else if (strcmp(command, "sysinfo") == 0) {
        vga_writestring("NumOS System Information\n");
        vga_writestring("========================\n");
        vga_writestring("Version: 2.5 (64-bit Kernel)\n");
        vga_writestring("Architecture: x86-64\n");
        vga_writestring("Uptime: ");
        print_dec(timer_get_uptime_seconds());
        vga_writestring(" seconds\n");
        
        vga_writestring("\nMemory:\n");
        struct heap_stats stats = heap_get_stats();
        vga_writestring("  Total heap: ");
        print_dec(stats.total_size / 1024);
        vga_writestring(" KB\n");
        vga_writestring("  Used: ");
        print_dec(stats.used_size / 1024);
        vga_writestring(" KB\n");
        vga_writestring("  Free: ");
        print_dec(stats.free_size / 1024);
        vga_writestring(" KB\n");
        
    } else if (strcmp(command, "heap") == 0) {
        heap_print_stats();
        
    } else if (strcmp(command, "disk") == 0) {
        disk_list_disks();
        
    } else if (strcmp(command, "ls") == 0) {
        fat32_list_files();
        
    } else if (strncmp(command, "cat ", 4) == 0) {
        const char *filename = command + 4;
        
        /* Skip whitespace */
        while (*filename == ' ') filename++;
        
        if (*filename == '\0') {
            vga_writestring("Usage: cat <filename>\n");
            return;
        }
        
        if (!fat32_exists(filename)) {
            vga_writestring("File not found: ");
            vga_writestring(filename);
            vga_putchar('\n');
            return;
        }
        
        uint32_t size = fat32_get_file_size(filename);
        if (size == 0) {
            vga_writestring("File is empty or cannot be read\n");
            return;
        }
        
        if (size > 4096) {
            vga_writestring("File too large (max 4KB for display)\n");
            return;
        }
        
        void *buffer = kmalloc(size + 1);
        if (!buffer) {
            vga_writestring("Out of memory\n");
            return;
        }
        
        struct fat32_file *file = fat32_fopen(filename, "r");
        if (!file) {
            vga_writestring("Failed to open file\n");
            kfree(buffer);
            return;
        }
        
        size_t read = fat32_fread(buffer, 1, size, file);
        fat32_fclose(file);
        
        if (read > 0) {
            ((char*)buffer)[read] = '\0';
            vga_writestring((char*)buffer);
            vga_putchar('\n');
        } else {
            vga_writestring("Failed to read file\n");
        }
        
        kfree(buffer);
        
    } else if (strncmp(command, "write ", 6) == 0) {
        const char *args = command + 6;
        
        /* Skip whitespace */
        while (*args == ' ') args++;
        
        /* Find filename end */
        const char *filename_end = args;
        while (*filename_end && *filename_end != ' ') filename_end++;
        
        if (*filename_end == '\0') {
            vga_writestring("Usage: write <filename> <text>\n");
            return;
        }
        
        /* Extract filename */
        size_t filename_len = filename_end - args;
        char filename[256];
        if (filename_len >= sizeof(filename)) {
            vga_writestring("Filename too long\n");
            return;
        }
        
        strncpy(filename, args, filename_len);
        filename[filename_len] = '\0';
        
        /* Get text */
        const char *text = filename_end + 1;
        while (*text == ' ') text++;
        
        if (*text == '\0') {
            vga_writestring("No text to write\n");
            return;
        }
        
        /* Write to file */
        struct fat32_file *file = fat32_fopen(filename, "w");
        if (!file) {
            vga_writestring("Failed to create file\n");
            return;
        }
        
        size_t written = fat32_fwrite(text, 1, strlen(text), file);
        fat32_fclose(file);
        
        if (written > 0) {
            vga_writestring("Wrote ");
            print_dec(written);
            vga_writestring(" bytes to ");
            vga_writestring(filename);
            vga_putchar('\n');
        } else {
            vga_writestring("Failed to write to file\n");
        }
        
    } else if (strcmp(command, "shutdown") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_writestring("Shutting down...\n");
        
        /* Flush disk caches */
        disk_shutdown();
        fat32_unmount();
        
        vga_writestring("System halted. You can power off now.\n");
        hang();
        
    } else if (strcmp(command, "reboot") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_writestring("Rebooting...\n");
        
        /* Flush disk caches */
        disk_shutdown();
        fat32_unmount();
        
        /* Wait a bit */
        timer_sleep(1000);
        
        /* Triple fault to reboot */
        uint8_t good = 0x02;
        while (good & 0x02) {
            good = inb(0x64);
        }
        outb(0x64, 0xFE);
        
        /* If that didn't work, hang */
        hang();
        
    } else if (command[0] == '\0') {
        /* Empty command, do nothing */
        
    } else {
        vga_writestring("Unknown command: ");
        vga_writestring(command);
        vga_writestring("\n");
        vga_writestring("Type 'help' for available commands\n");
    }
}