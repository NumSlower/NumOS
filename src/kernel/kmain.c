/*
 * kmain.c - Fixed kernel main with proper shell loading
 */

#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "drivers/disk.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/paging.h"
#include "kernel/syscall.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "cpu/heap.h"
#include "fs/fat32.h"
#include "kernel/elf.h"
#include "kernel/process.h"

void kernel_init(void) {
    /* Initialize VGA text mode first so we can see output */
    vga_init();
    
    /* Display early boot message */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v2.5 - 64-bit OS with ELF Loader\n");
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
    
    /* Initialize system call interface */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing system call interface...\n");
    syscall_init();
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("- System call handler registered (INT 0x80)\n");
    vga_writestring("- User-space API available\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Initialize process management */
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("\nInitializing process management...\n");
    process_init();
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("- Process table initialized\n");
    vga_writestring("- Context switching ready\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
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

int load_shell_from_disk(void **shell_data, uint32_t *shell_size) {
    vga_writestring("\n=== Loading Shell from Disk ===\n");
    
    /* Try different possible paths - FAT32 uses uppercase */
    const char *shell_paths[] = {
        "SHELL",       /* FAT32 uppercase, no extension - PRIMARY */
        "SHELL.BIN",   /* With extension */
        "shell",       /* Lowercase fallback */
        "shell.bin",   /* Lowercase with extension */
        NULL
    };
    
    const char *found_path = NULL;
    
    /* Check which path exists */
    vga_writestring("Searching for shell binary...\n");
    for (int i = 0; shell_paths[i] != NULL; i++) {
        vga_writestring("  Trying: '");
        vga_writestring(shell_paths[i]);
        vga_writestring("'");
        
        if (fat32_exists(shell_paths[i])) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring(" - FOUND!\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            found_path = shell_paths[i];
            break;
        } else {
            vga_writestring(" - not found\n");
        }
    }
    
    if (!found_path) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("\nERROR: Shell binary not found on disk!\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("\nAvailable files on disk:\n");
        fat32_list_files();
        vga_writestring("\n");
        return -1;
    }
    
    /* Get file size */
    *shell_size = fat32_get_file_size(found_path);
    vga_writestring("\nShell file: '");
    vga_writestring(found_path);
    vga_writestring("'\n");
    vga_writestring("Size: ");
    print_dec(*shell_size);
    vga_writestring(" bytes\n");
    
    if (*shell_size == 0 || *shell_size > 1024 * 1024) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Invalid shell file size\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return -1;
    }
    
    /* Allocate memory for shell */
    *shell_data = kmalloc(*shell_size);
    if (!*shell_data) {
        vga_writestring("ERROR: Failed to allocate memory for shell\n");
        return -1;
    }
    
    vga_writestring("Allocated ");
    print_dec(*shell_size);
    vga_writestring(" bytes at 0x");
    print_hex((uint64_t)*shell_data);
    vga_writestring("\n");
    
    /* Open and read shell file */
    vga_writestring("Opening '");
    vga_writestring(found_path);
    vga_writestring("'...\n");
    
    struct fat32_file *file = fat32_fopen(found_path, "r");
    if (!file) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Failed to open shell file\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kfree(*shell_data);
        *shell_data = NULL;
        return -1;
    }
    
    vga_writestring("Reading shell binary...\n");
    size_t bytes_read = fat32_fread(*shell_data, 1, *shell_size, file);
    fat32_fclose(file);
    
    if (bytes_read != *shell_size) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Read ");
        print_dec(bytes_read);
        vga_writestring(" bytes, expected ");
        print_dec(*shell_size);
        vga_writestring("\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kfree(*shell_data);
        *shell_data = NULL;
        return -1;
    }
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("Successfully loaded ");
    print_dec(bytes_read);
    vga_writestring(" bytes\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    return 0;
}

void kernel_main(void) {
    /* Initialize all kernel subsystems */
    kernel_init();
    
    /* List available files for debugging */
    vga_writestring("Filesystem contents:\n");
    fat32_list_files();
    vga_writestring("\n");
    
    /* Load shell from disk */
    void *shell_data = NULL;
    uint32_t shell_size = 0;
    
    if (load_shell_from_disk(&shell_data, &shell_size) != 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("\nFailed to load shell from disk!\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("\nKernel will now idle...\n");
        hang();
    }
    
    /* Validate and load ELF */
    vga_writestring("\n=== ELF Validation and Loading ===\n");
    
    int valid = elf_validate(shell_data);
    if (valid != ELF_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Invalid ELF file (code ");
        print_dec(valid);
        vga_writestring(")\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        
        /* Show first few bytes for debugging */
        vga_writestring("First 16 bytes of file:\n  ");
        uint8_t *bytes = (uint8_t*)shell_data;
        for (uint32_t i = 0; i < 16 && i < shell_size; i++) {
            if (bytes[i] < 0x10) vga_putchar('0');
            print_hex32(bytes[i]);
            vga_putchar(' ');
        }
        vga_writestring("\n");
        
        kfree(shell_data);
        hang();
    }
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("ELF validation passed!\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Print ELF information */
    elf_print_info(shell_data);
    
    /* Load ELF into memory */
    uint64_t entry_point = 0;
    int load_result = elf_load(shell_data, &entry_point);
    
    if (load_result != ELF_SUCCESS) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Failed to load ELF (code ");
        print_dec(load_result);
        vga_writestring(")\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kfree(shell_data);
        hang();
    }
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("\nELF loaded successfully!\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Setup user stack */
    vga_writestring("\n=== Setting Up User Environment ===\n");
    uint64_t stack_top = elf_setup_user_stack();
    if (stack_top == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Failed to setup user stack\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kfree(shell_data);
        hang();
    }
    
    /* Create process */
    vga_writestring("\n=== Creating User Process ===\n");
    struct process *shell_proc = process_create("shell", entry_point, stack_top);
    if (!shell_proc) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("ERROR: Failed to create process\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        kfree(shell_data);
        hang();
    }
    
    /* Free the ELF data (it's been loaded into process memory) */
    kfree(shell_data);
    
    /* Final status */
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("=== Ready to Execute User Process ===\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Process: ");
    vga_writestring(shell_proc->name);
    vga_writestring(" (PID ");
    print_dec(shell_proc->pid);
    vga_writestring(")\n");
    vga_writestring("Entry point: 0x");
    print_hex(entry_point);
    vga_writestring("\nStack: 0x");
    print_hex(stack_top);
    vga_writestring("\n\n");
    
    /* Give user time to read */
    timer_sleep(2000);
    
    /* Clear screen for shell */
    vga_clear();
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("Executing userspace shell...\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Execute the process (switch to ring 3) */
    process_exec(shell_proc);
    
    /* If we return here, the process has exited */
    vga_writestring("\n\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("Shell process terminated\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Kernel will now idle...\n");
    
    /* Idle loop */
    while (1) {
        __asm__ volatile("hlt");
    }
}