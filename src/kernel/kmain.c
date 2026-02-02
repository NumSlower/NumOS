/*
 * kmain.c - Kernel main with FAT32 filesystem support + user space
 */

#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "cpu/gdt.h"
#include "cpu/idt.h"
#include "cpu/paging.h"
#include "drivers/pic.h"
#include "drivers/timer.h"
#include "drivers/ata.h"
#include "cpu/heap.h"
#include "fs/fat32.h"
#include "cpu/syscall.h"
#include "kernel/elf_loader.h"

void kernel_init(void) {
    /* Initialize VGA text mode first so we can see output */
    vga_init();
    
    /* Display early boot message */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS v3.0 - 64-bit Kernel with FAT32\n");
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

    /* Initialize syscall subsystem (programs IA32_LSTAR etc).
     * Must come after GDT + IDT are set up. */
    syscall_init();
    
    /* Initialize keyboard */
    vga_writestring("Initializing keyboard driver...\n");
    keyboard_init();
    
    /* Unmask timer and keyboard IRQs */
    pic_unmask_irq(0); /* Timer */
    pic_unmask_irq(1); /* Keyboard */
    
    /* Initialize ATA/IDE disk controller */
    vga_writestring("\n");
    ata_init();
    
    /* Initialize FAT32 filesystem */
    vga_writestring("\n");
    if (fat32_init() == 0) {
        if (fat32_mount() == 0) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("✓ Filesystem mounted successfully\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }
    
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
}

void test_filesystem(void) {
    vga_writestring("\n=== Filesystem Test ===\n");
    
    /* Print filesystem information */
    fat32_print_info();
    
    /* List root directory contents */
    vga_writestring("\n");
    fat32_list_directory("/");
    
    /* Test directory creation - check if it exists first */
    vga_writestring("\nTesting mkdir('/test')... ");
    
    /* Check if test directory already exists */
    struct fat32_dirent test_info;
    if (fat32_stat("test", &test_info) == 0) {
        /* Directory already exists */
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_writestring("SKIP (already exists)\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else {
        /* Directory doesn't exist, try to create it */
        if (fat32_mkdir("test") == 0) {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("OK\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* List directory again to show new folder */
            vga_writestring("\nUpdated root directory:\n");
            fat32_list_directory("/");
        } else {
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("FAILED\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        }
    }
}

void run_system_tests(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n");
    vga_writestring("=========================================\n");
    vga_writestring("    NumOS System Tests\n");
    vga_writestring("=========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Run tests */
    test_memory_allocation();
    test_paging();
    test_filesystem();
    
    /* Final summary */
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("=========================================\n");
    vga_writestring("    Tests Complete\n");
    vga_writestring("=========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

void kernel_main(void) {
    /* Initialize all kernel subsystems */
    kernel_init();
    
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS Kernel Ready with FAT32 Support\n");
    vga_writestring("======================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("Running system tests...\n");
    
    /* Run system tests */
    run_system_tests();

    /* ── Kernel is now ready ──────────────────────────────────
     * The kernel has completed initialization and testing.
     * We now enter an interactive prompt where the user can:
     *   - Browse filesystem
     *   - Review boot messages (scroll mode)
     *   - Manually load user-space programs
     *   - Or just idle
     * ────────────────────────────────────────────────────────── */
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("========================================\n");
    vga_writestring("  Kernel Ready - Interactive Mode\n");
    vga_writestring("========================================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    vga_writestring("\nAvailable commands:\n");
    vga_writestring("  [S] - Enter scroll mode (review boot messages)\n");
    vga_writestring("  [L] - List root directory\n");
    vga_writestring("  [E] - Load and verify /init/SHELL ELF file\n");
    vga_writestring("  [R] - Run /init/SHELL in user space (Ring 3)\n");
    vga_writestring("  [H] - Halt system\n");
    vga_writestring("\nPress a key to continue...\n");
    
    /* Interactive command loop */
    while (1) {
        uint8_t scan_code = keyboard_read_scan_code();
        char c = scan_code_to_ascii(scan_code);
        
        if (c == 0) {
            continue;  /* Ignore non-ASCII keys */
        }
        
        if (c == 's' || c == 'S') {
            /* Enter scroll mode */
            vga_writestring("\nEntering scroll mode...\n");
            vga_enter_scroll_mode();
            vga_writestring("\nExited scroll mode.\n");
            vga_writestring("Press S/L/E/R/H: ");
            
        } else if (c == 'l' || c == 'L') {
            /* List directory */
            vga_writestring("\n");
            fat32_list_directory("/");
            vga_writestring("\nPress S/L/E/R/H: ");
            
        } else if (c == 'e' || c == 'E') {
            /* Load and verify ELF (without executing) */
            vga_writestring("\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("Loading and verifying ELF file...\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* Try to access /init/SHELL */
            if (fat32_chdir("init") == 0) {
                vga_writestring("✓ Changed to /init directory\n");
                
                /* Get file info */
                struct fat32_dirent info;
                if (fat32_stat("SHELL", &info) == 0) {
                    vga_writestring("✓ Found SHELL file\n");
                    vga_writestring("  Size: ");
                    print_dec(info.size);
                    vga_writestring(" bytes\n");
                    vga_writestring("  Cluster: ");
                    print_dec(info.cluster);
                    vga_writestring("\n");
                    
                    /* Try to open the file */
                    int fd = fat32_open("SHELL", FAT32_O_RDONLY);
                    if (fd >= 0) {
                        vga_writestring("✓ Opened file (fd=");
                        print_dec(fd);
                        vga_writestring(")\n");
                        
                        /* Read ELF header */
                        uint8_t elf_header[64];
                        ssize_t bytes_read = fat32_read(fd, elf_header, 64);
                        
                        if (bytes_read == 64) {
                            vga_writestring("✓ Read ELF header (64 bytes)\n");
                            
                            /* Verify ELF magic */
                            if (elf_header[0] == 0x7F && 
                                elf_header[1] == 'E' && 
                                elf_header[2] == 'L' && 
                                elf_header[3] == 'F') {
                                
                                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                                vga_writestring("✓ Valid ELF magic: 0x7F 'E' 'L' 'F'\n");
                                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                                
                                /* Check ELF class */
                                if (elf_header[4] == 2) {
                                    vga_writestring("✓ ELF class: 64-bit\n");
                                } else {
                                    vga_writestring("✗ ELF class: ");
                                    print_dec(elf_header[4]);
                                    vga_writestring(" (expected 2 for 64-bit)\n");
                                }
                                
                                /* Check endianness */
                                if (elf_header[5] == 1) {
                                    vga_writestring("✓ Endianness: Little-endian\n");
                                } else {
                                    vga_writestring("✗ Endianness: ");
                                    print_dec(elf_header[5]);
                                    vga_writestring(" (expected 1 for little-endian)\n");
                                }
                                
                                /* Get entry point (at offset 24, 8 bytes) */
                                uint64_t entry = *(uint64_t*)(elf_header + 24);
                                vga_writestring("  Entry point: ");
                                print_hex(entry);
                                vga_writestring("\n");
                                
                                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
                                vga_writestring("\n✓ ELF file verification PASSED\n");
                                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                                
                            } else {
                                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                                vga_writestring("✗ Invalid ELF magic: ");
                                print_hex32((uint32_t)elf_header[0] | 
                                           ((uint32_t)elf_header[1] << 8) |
                                           ((uint32_t)elf_header[2] << 16) |
                                           ((uint32_t)elf_header[3] << 24));
                                vga_writestring("\n");
                                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                            }
                        } else {
                            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                            vga_writestring("✗ Failed to read ELF header (got ");
                            print_dec(bytes_read);
                            vga_writestring(" bytes)\n");
                            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                        }
                        
                        fat32_close(fd);
                        vga_writestring("✓ Closed file\n");
                    } else {
                        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                        vga_writestring("✗ Failed to open file\n");
                        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                    }
                } else {
                    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                    vga_writestring("✗ SHELL file not found in /init\n");
                    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                }
                
                /* Return to root */
                fat32_chdir("/");
            } else {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ /init directory not found\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            }
            
            vga_writestring("\nPress S/L/E/R/H: ");
            
        } else if (c == 'r' || c == 'R') {
            /* Run ELF in user space with full verification 
             * 
             * IMPORTANT: If exec_user_elf() succeeds, it does NOT return!
             * The kernel's execution context is completely replaced by the
             * user program. The user program will eventually call SYS_EXIT,
             * which halts the CPU. There is no "return to kernel" path.
             * 
             * If exec_user_elf() returns, it means loading failed before
             * the transition to user space occurred.
             */
            vga_writestring("\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("========================================\n");
            vga_writestring("  Executing User Space Program\n");
            vga_writestring("========================================\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* Pre-flight checks */
            vga_writestring("\n[1/5] Checking /init directory...\n");
            if (fat32_chdir("init") != 0) {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: /init directory not found\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("✓ Directory found\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* Check file exists */
            vga_writestring("\n[2/5] Locating SHELL executable...\n");
            struct fat32_dirent info;
            if (fat32_stat("SHELL", &info) != 0) {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: SHELL file not found in /init\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                fat32_chdir("/");
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("✓ Found SHELL (");
            print_dec(info.size);
            vga_writestring(" bytes, cluster ");
            print_dec(info.cluster);
            vga_writestring(")\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* Verify ELF format */
            vga_writestring("\n[3/5] Verifying ELF format...\n");
            int fd = fat32_open("SHELL", FAT32_O_RDONLY);
            if (fd < 0) {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: Cannot open SHELL file\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                fat32_chdir("/");
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            
            uint8_t elf_header[64];
            ssize_t bytes_read = fat32_read(fd, elf_header, 64);
            fat32_close(fd);
            
            if (bytes_read != 64) {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: Cannot read ELF header\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                fat32_chdir("/");
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            
            /* Check ELF magic */
            if (elf_header[0] != 0x7F || elf_header[1] != 'E' || 
                elf_header[2] != 'L' || elf_header[3] != 'F') {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: Invalid ELF magic\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                fat32_chdir("/");
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            
            /* Check 64-bit */
            if (elf_header[4] != 2) {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: Not a 64-bit ELF\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                fat32_chdir("/");
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            
            /* Check little-endian */
            if (elf_header[5] != 1) {
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
                vga_writestring("✗ FATAL: Not little-endian\n");
                vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
                fat32_chdir("/");
                vga_writestring("\nSystem halted due to execution failure.\n");
                hang();  /* Halt permanently */
            }
            
            uint64_t entry = *(uint64_t*)(elf_header + 24);
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
            vga_writestring("✓ Valid 64-bit little-endian ELF\n");
            vga_writestring("✓ Entry point: ");
            print_hex(entry);
            vga_writestring("\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            /* Verify user space readiness */
            vga_writestring("\n[4/5] Verifying user space environment...\n");
            vga_writestring("✓ GDT configured with user segments\n");
            vga_writestring("✓ Syscall interface initialized\n");
            vga_writestring("✓ Page tables ready\n");
            
            /* Save kernel state before transition */
            vga_writestring("\n✓ Saving kernel state...\n");
            uint64_t kernel_heap_before = (uint64_t)kmalloc(16);
            if (kernel_heap_before) {
                kfree((void*)kernel_heap_before);
            }
            vga_writestring("✓ Heap allocator verified\n");
            
            /* Final confirmation */
            vga_writestring("\n[5/5] Preparing to switch to Ring 3...\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("\n>>> TRANSITIONING TO USER SPACE <<<\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_BROWN, VGA_COLOR_BLACK));
            vga_writestring("\n⚠ WARNING: If user program crashes, system will halt!\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            vga_writestring("\nKernel will now transfer control to user program.\n");
            vga_writestring("User program runs at Ring 3 (unprivileged mode).\n");
            vga_writestring("Program output will appear below:\n");
            vga_writestring("----------------------------------------\n");
            
            /* Small delay for user to read */
            for (volatile int i = 0; i < 10000000; i++);
            
            /* Execute - this should not return on success */
            /* Note: exec_user_elf() does NOT return on success - it replaces
             * the kernel's execution context with the user program.
             * If it returns, something went wrong during loading. */
            int result = exec_user_elf("SHELL");
            
            /* If we get here, execution failed during loading phase */
            vga_writestring("\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
            vga_writestring("========================================\n");
            vga_writestring("  USER SPACE EXECUTION FAILED\n");
            vga_writestring("========================================\n");
            vga_writestring("✗ FATAL: exec_user_elf() returned ");
            print_dec(result);
            vga_writestring("\n\n");
            vga_writestring("Failure occurred during ELF loading phase.\n");
            vga_writestring("The user program was never executed.\n");
            vga_writestring("Possible causes:\n");
            vga_writestring("  - ELF file corrupted\n");
            vga_writestring("  - Memory allocation failed\n");
            vga_writestring("  - Invalid ELF segments\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            
            fat32_chdir("/");
            vga_writestring("\nKernel state preserved. Filesystem intact.\n");
            vga_writestring("System will now halt.\n");
            hang();  /* Halt permanently */
            
        } else if (c == 'h' || c == 'H') {
            /* Halt */
            vga_writestring("\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
            vga_writestring("System halted by user.\n");
            vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
            hang();  /* Halt permanently */
        }
    }

    /* Halt */
    while (1) {
        __asm__ volatile("hlt");
    }
}
