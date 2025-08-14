#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "paging.h"
#include "timer.h"
#include "heap.h"
#include "fat32.h"

void *memset(void *dest, int val, size_t len) {
    unsigned char *ptr = (unsigned char*)dest;
    while (len-- > 0) {
        *ptr++ = val;
    }
    return dest;
}

void *memcpy(void *dest, const void *src, size_t len) {
    char *d = (char*)dest;
    const char *s = (const char*)src;
    while (len--) {
        *d++ = *s++;
    }
    return dest;
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) {
        len++;
    }
    return len;
}

int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

void panic(const char *message) {
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("KERNEL PANIC: ");
    vga_writestring(message);
    hang();
}

void hang(void) {
    __asm__ volatile("cli");
    while (1) {
        __asm__ volatile("hlt");
    }
}

void print_prompt(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("NumOS");
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_putchar(':');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE, VGA_COLOR_BLACK));
    vga_putchar('>');
    vga_putchar(' ');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}

int strncmp(const char *str1, const char *str2, size_t n) {
    while (n && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
        n--;
    }
    if (n == 0) {
        return 0;
    }
    return *(unsigned char*)str1 - *(unsigned char*)str2;
}

void print_hex(uint64_t value) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[17];
    buffer[16] = '\0';
    
    for (int i = 15; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    
    vga_writestring("0x");
    vga_writestring(buffer);
}

void print_dec(uint64_t value) {
    char buffer[21]; // Enough for 64-bit number
    int pos = 20;
    buffer[pos] = '\0';
    
    if (value == 0) {
        vga_putchar('0');
        return;
    }
    
    while (value > 0 && pos > 0) {
        buffer[--pos] = '0' + (value % 10);
        value /= 10;
    }
    
    vga_writestring(&buffer[pos]);
}

// Helper function to parse filename and mode for file operations
static void parse_file_command(const char *command, char *filename, char *mode, int max_filename_len) {
    const char *space = command;
    while (*space && *space != ' ') space++; // Find first space
    if (!*space) return;
    
    space++; // Skip the space
    int i = 0;
    while (*space && *space != ' ' && i < max_filename_len - 1) {
        filename[i++] = *space++;
    }
    filename[i] = '\0';
    
    if (*space == ' ') {
        space++;
        i = 0;
        while (*space && i < 3) {
            mode[i++] = *space++;
        }
        mode[i] = '\0';
    } else {
        strcpy(mode, "r"); // Default to read mode
    }
}

void process_command(const char *command) {
    if (strlen(command) == 0) {
        return;
    }
    
    if (strcmp(command, "help") == 0) {
        vga_writestring("NumOS Commands:\n");
        vga_writestring("  help         - Show this help message\n");
        vga_writestring("  clear        - Clear the screen\n");
        vga_writestring("  version      - Show system version\n");
        vga_writestring("  echo <text>  - Echo back text\n");
        vga_writestring("  meminfo      - Show memory information\n");
        vga_writestring("  heapinfo     - Show heap allocator statistics\n");
        vga_writestring("  paging       - Show paging status\n");
        vga_writestring("  pagingstats  - Show paging statistics\n");
        vga_writestring("  vmregions    - Show virtual memory regions\n");
        vga_writestring("  testpage     - Test page allocation\n");
        vga_writestring("  testheap     - Test heap allocation\n");
        vga_writestring("  translate <addr> - Translate virtual to physical address\n");
        vga_writestring("  uptime       - Show system uptime\n");
        vga_writestring("  timer        - Show timer information\n");
        vga_writestring("  sleep <ms>   - Sleep for specified milliseconds\n");
        vga_writestring("  benchmark    - Run memory allocation benchmark\n");
        vga_writestring("\n--- FAT32 File System Commands ---\n");
        vga_writestring("  fat32init    - Initialize FAT32 filesystem\n");
        vga_writestring("  fat32mount   - Mount FAT32 filesystem\n");
        vga_writestring("  fat32unmount - Unmount FAT32 filesystem\n");
        vga_writestring("  ls           - List files in directory\n");
        vga_writestring("  dir          - List files in directory (alias for ls)\n");
        vga_writestring("  cat <file>   - Display file contents\n");
        vga_writestring("  create <file> - Create a new file\n");
        vga_writestring("  write <file> <text> - Write text to file\n");
        vga_writestring("  fileinfo <file> - Show file information\n");
        vga_writestring("  exists <file> - Check if file exists\n");
        vga_writestring("  bootinfo     - Show FAT32 boot sector info\n");
        vga_writestring("  fsinfo       - Show FAT32 filesystem info\n");
        vga_writestring("  testfat32    - Run FAT32 filesystem tests\n");
        vga_writestring("  reboot       - Restart the system\n");
    } else if (strcmp(command, "clear") == 0) {
        vga_clear();
    } else if (strcmp(command, "version") == 0) {
        vga_writestring("NumOS Version 2.1\n");
        vga_writestring("64-bit Operating System with Advanced Features\n");
        vga_writestring("- Enhanced paging with VM regions\n");
        vga_writestring("- Kernel heap allocator (kmalloc/kfree)\n");
        vga_writestring("- Timer driver with PIT support\n");
        vga_writestring("- FAT32 filesystem support\n");
        vga_writestring("- Built with C and Assembly\n");
    } else if (strncmp(command, "echo ", 5) == 0) {
        vga_writestring(command + 5);
        vga_putchar('\n');
    } else if (strcmp(command, "meminfo") == 0) {
        vga_writestring("Memory Information:\n");
        vga_writestring("  Total frames: ");
        print_dec(pmm_get_total_frames());
        vga_writestring("\n  Used frames:  ");
        print_dec(pmm_get_used_frames());
        vga_writestring("\n  Free frames:  ");
        print_dec(pmm_get_free_frames());
        vga_writestring("\n  Frame size:   4096 bytes\n");
    } else if (strcmp(command, "heapinfo") == 0) {
        heap_print_stats();
    } else if (strcmp(command, "paging") == 0) {
        vga_writestring("Paging System Status:\n");
        paging_enable(); // This will print status
        vga_writestring("  Page size: 4096 bytes (4 KB)\n");
        vga_writestring("  Large page size: 2097152 bytes (2 MB)\n");
        vga_writestring("  4-level paging active (PML4)\n");
        vga_writestring("  Enhanced features: VM regions, bulk operations\n");
    } else if (strcmp(command, "pagingstats") == 0) {
        paging_print_stats();
    } else if (strcmp(command, "vmregions") == 0) {
        paging_print_vm_regions();
    } else if (strcmp(command, "testpage") == 0) {
        vga_writestring("Testing page allocation...\n");
        
        // Allocate some pages
        void *pages = vmm_alloc_pages(2, PAGE_PRESENT | PAGE_WRITABLE);
        if (pages) {
            vga_writestring("Allocated 2 pages at virtual address: ");
            print_hex((uint64_t)pages);
            
            // Test writing to the pages
            char *test_ptr = (char*)pages;
            *test_ptr = 'A';
            *(test_ptr + 4096) = 'B'; // Second page
            
            vga_writestring("\nWrote test data successfully\n");
            vga_writestring("First page data: ");
            vga_putchar(*test_ptr);
            vga_writestring("\nSecond page data: ");
            vga_putchar(*(test_ptr + 4096));
            vga_putchar('\n');
            
            // Test mapping validation
            if (paging_validate_range((uint64_t)pages, 2)) {
                vga_writestring("Page mapping validation: PASSED\n");
            } else {
                vga_writestring("Page mapping validation: FAILED\n");
            }
            
            // Free the pages
            vmm_free_pages(pages, 2);
            vga_writestring("Pages freed successfully\n");
        } else {
            vga_writestring("Failed to allocate pages\n");
        }
    } else if (strcmp(command, "testheap") == 0) {
        vga_writestring("Testing heap allocation...\n");
        
        // Test basic allocation
        void *ptr1 = kmalloc(100);
        void *ptr2 = kzalloc(200);
        char *str = kstrdup("Hello, NumOS!");
        
        if (ptr1 && ptr2 && str) {
            vga_writestring("Basic allocation test: PASSED\n");
            vga_writestring("Duplicated string: ");
            vga_writestring(str);
            vga_putchar('\n');
            
            // Test reallocation
            ptr1 = krealloc(ptr1, 500);
            if (ptr1) {
                vga_writestring("Reallocation test: PASSED\n");
            } else {
                vga_writestring("Reallocation test: FAILED\n");
            }
            
            // Free everything
            kfree(ptr1);
            kfree(ptr2);
            kfree(str);
            vga_writestring("Memory freed successfully\n");
        } else {
            vga_writestring("Basic allocation test: FAILED\n");
        }
        
        // Validate heap
        if (heap_validate()) {
            vga_writestring("Heap validation: PASSED\n");
        } else {
            vga_writestring("Heap validation: FAILED\n");
        }
    } else if (strncmp(command, "translate ", 10) == 0) {
        // Simple hex parser for virtual address
        const char *hex_str = command + 10;
        uint64_t virtual_addr = 0;
        
        if (hex_str[0] == '0' && hex_str[1] == 'x') {
            hex_str += 2; // Skip "0x"
        }
        
        // Parse hex string
        while (*hex_str) {
            char c = *hex_str++;
            virtual_addr <<= 4;
            if (c >= '0' && c <= '9') {
                virtual_addr += c - '0';
            } else if (c >= 'a' && c <= 'f') {
                virtual_addr += c - 'a' + 10;
            } else if (c >= 'A' && c <= 'F') {
                virtual_addr += c - 'A' + 10;
            } else {
                vga_writestring("Invalid hex address\n");
                return;
            }
        }
        
        uint64_t physical_addr = paging_get_physical_address(virtual_addr);
        
        vga_writestring("Virtual address:  ");
        print_hex(virtual_addr);
        vga_writestring("\nPhysical address: ");
        if (physical_addr) {
            print_hex(physical_addr);
        } else {
            vga_writestring("Not mapped");
        }
        vga_putchar('\n');
    } else if (strcmp(command, "uptime") == 0) {
        uint64_t uptime_ms = timer_get_uptime_ms();
        uint64_t seconds = uptime_ms / 1000;
        uint64_t minutes = seconds / 60;
        uint64_t hours = minutes / 60;
        
        vga_writestring("System uptime: ");
        if (hours > 0) {
            print_dec(hours);
            vga_writestring("h ");
        }
        if (minutes > 0) {
            print_dec(minutes % 60);
            vga_writestring("m ");
        }
        print_dec(seconds % 60);
        vga_writestring("s (");
        print_dec(uptime_ms);
        vga_writestring(" ms)\n");
    } else if (strcmp(command, "timer") == 0) {
        struct timer_stats stats = timer_get_stats();
        vga_writestring("Timer Information:\n");
        vga_writestring("  Frequency:    ");
        print_dec(stats.frequency);
        vga_writestring(" Hz\n");
        vga_writestring("  Total ticks:  ");
        print_dec(stats.ticks);
        vga_writestring("\n  Uptime:       ");
        print_dec(stats.seconds);
        vga_writestring(" seconds\n");
    } else if (strncmp(command, "sleep ", 6) == 0) {
        // Parse sleep duration
        const char *ms_str = command + 6;
        uint32_t ms = 0;
        
        while (*ms_str >= '0' && *ms_str <= '9') {
            ms = ms * 10 + (*ms_str - '0');
            ms_str++;
        }
        
        if (ms > 0 && ms <= 10000) { // Max 10 seconds
            vga_writestring("Sleeping for ");
            print_dec(ms);
            vga_writestring(" ms...\n");
            
            uint64_t start_time = timer_get_uptime_ms();
            timer_sleep(ms);
            uint64_t end_time = timer_get_uptime_ms();
            
            vga_writestring("Woke up after ");
            print_dec(end_time - start_time);
            vga_writestring(" ms\n");
        } else {
            vga_writestring("Invalid sleep duration (1-10000 ms)\n");
        }
    } else if (strcmp(command, "benchmark") == 0) {
        vga_writestring("Running memory allocation benchmark...\n");
        
        uint64_t start_bench = timer_benchmark_start();
        
        // Allocate and free many small blocks
        void *ptrs[100];
        for (int i = 0; i < 100; i++) {
            ptrs[i] = kmalloc(64);
            if (!ptrs[i]) {
                vga_writestring("Allocation failed at iteration ");
                print_dec(i);
                vga_putchar('\n');
                break;
            }
        }
        
        // Free all blocks
        for (int i = 0; i < 100; i++) {
            if (ptrs[i]) {
                kfree(ptrs[i]);
            }
        }
        
        uint64_t elapsed_ms = timer_benchmark_end(start_bench);
        
        vga_writestring("Benchmark completed in ");
        print_dec(elapsed_ms);
        vga_writestring(" ms\n");
        
        // Test large allocation
        start_bench = timer_benchmark_start();
        void *large_ptr = kmalloc(1024 * 1024); // 1MB
        if (large_ptr) {
            // Write pattern to test
            char *test_ptr = (char*)large_ptr;
            for (int i = 0; i < 1000; i++) {
                test_ptr[i] = (char)(i % 256);
            }
            
            // Verify pattern
            int errors = 0;
            for (int i = 0; i < 1000; i++) {
                if (test_ptr[i] != (char)(i % 256)) {
                    errors++;
                }
            }
            
            kfree(large_ptr);
            elapsed_ms = timer_benchmark_end(start_bench);
            
            vga_writestring("Large allocation test (1MB): ");
            if (errors == 0) {
                vga_writestring("PASSED");
            } else {
                vga_writestring("FAILED (");
                print_dec(errors);
                vga_writestring(" errors)");
            }
            vga_writestring(" in ");
            print_dec(elapsed_ms);
            vga_writestring(" ms\n");
        } else {
            vga_writestring("Large allocation test: FAILED (out of memory)\n");
        }
    } 
    // FAT32 Commands
    else if (strcmp(command, "fat32init") == 0) {
        vga_writestring("Initializing FAT32 filesystem...\n");
        int result = fat32_init();
        if (result == FAT32_SUCCESS) {
            vga_writestring("FAT32 initialization successful\n");
        } else {
            vga_writestring("FAT32 initialization failed with code ");
            print_dec(result);
            vga_putchar('\n');
        }
    } else if (strcmp(command, "fat32mount") == 0) {
        vga_writestring("Mounting FAT32 filesystem...\n");
        int result = fat32_mount();
        if (result == FAT32_SUCCESS) {
            vga_writestring("FAT32 filesystem mounted successfully\n");
        } else {
            vga_writestring("FAT32 mount failed with code ");
            print_dec(result);
            vga_putchar('\n');
        }
    } else if (strcmp(command, "fat32unmount") == 0) {
        fat32_unmount();
    } else if (strcmp(command, "ls") == 0 || strcmp(command, "dir") == 0) {
        fat32_list_files();
    } else if (strncmp(command, "cat ", 4) == 0) {
        const char *filename = command + 4;
        if (strlen(filename) == 0) {
            vga_writestring("Usage: cat <filename>\n");
            return;
        }
        
        struct fat32_file *file = fat32_fopen(filename, "r");
        if (!file) {
            vga_writestring("Failed to open file: ");
            vga_writestring(filename);
            vga_putchar('\n');
            return;
        }
        
        char buffer[256];
        size_t bytes_read;
        vga_writestring("File contents:\n");
        vga_writestring("--- ");
        vga_writestring(filename);
        vga_writestring(" ---\n");
        
        while ((bytes_read = fat32_fread(buffer, 1, sizeof(buffer) - 1, file)) > 0) {
            buffer[bytes_read] = '\0';
            vga_writestring(buffer);
        }
        
        vga_writestring("\n--- End of file ---\n");
        fat32_fclose(file);
    } else if (strncmp(command, "create ", 7) == 0) {
        const char *filename = command + 7;
        if (strlen(filename) == 0) {
            vga_writestring("Usage: create <filename>\n");
            return;
        }
        
        struct fat32_file *file = fat32_fopen(filename, "w");
        if (!file) {
            vga_writestring("Failed to create file: ");
            vga_writestring(filename);
            vga_putchar('\n');
            return;
        }
        
        fat32_fclose(file);
        vga_writestring("File created: ");
        vga_writestring(filename);
        vga_putchar('\n');
    } else if (strncmp(command, "write ", 6) == 0) {
        char filename[256];
        const char *text_start = command + 6;
        
        // Find the space separating filename from text
        const char *space = text_start;
        while (*space && *space != ' ') space++;
        
        if (!*space) {
            vga_writestring("Usage: write <filename> <text>\n");
            return;
        }
        
        // Copy filename
        int len = space - text_start;
        if (len >= sizeof(filename)) len = sizeof(filename) - 1;
        memcpy(filename, text_start, len);
        filename[len] = '\0';
        
        // Skip to text content
        const char *text = space + 1;
        
        struct fat32_file *file = fat32_fopen(filename, "w");
        if (!file) {
            vga_writestring("Failed to open file for writing: ");
            vga_writestring(filename);
            vga_putchar('\n');
            return;
        }
        
        size_t text_len = strlen(text);
        size_t written = fat32_fwrite(text, 1, text_len, file);
        fat32_fclose(file);
        
        vga_writestring("Wrote ");
        print_dec(written);
        vga_writestring(" bytes to ");
        vga_writestring(filename);
        vga_putchar('\n');
    } else if (strncmp(command, "fileinfo ", 9) == 0) {
        const char *filename = command + 9;
        if (strlen(filename) == 0) {
            vga_writestring("Usage: fileinfo <filename>\n");
            return;
        }
        fat32_print_file_info(filename);
    } else if (strncmp(command, "exists ", 7) == 0) {
        const char *filename = command + 7;
        if (strlen(filename) == 0) {
            vga_writestring("Usage: exists <filename>\n");
            return;
        }
        
        if (fat32_exists(filename)) {
            vga_writestring("File exists: ");
            vga_writestring(filename);
            vga_writestring(" (");
            print_dec(fat32_get_file_size(filename));
            vga_writestring(" bytes)\n");
        } else {
            vga_writestring("File does not exist: ");
            vga_writestring(filename);
            vga_putchar('\n');
        }
    } else if (strcmp(command, "bootinfo") == 0) {
        fat32_print_boot_sector();
    } else if (strcmp(command, "fsinfo") == 0) {
        fat32_print_fs_info();
    } else if (strcmp(command, "testfat32") == 0) {
        vga_writestring("Running FAT32 filesystem tests...\n");
        
        // Test 1: Initialize and mount
        vga_writestring("Test 1: Initialize and mount filesystem...\n");
        if (fat32_init() != FAT32_SUCCESS) {
            vga_writestring("  FAILED: Could not initialize FAT32\n");
            return;
        }
        if (fat32_mount() != FAT32_SUCCESS) {
            vga_writestring("  FAILED: Could not mount FAT32\n");
            return;
        }
        vga_writestring("  PASSED: Filesystem initialized and mounted\n");
        
        // Test 2: Create and write to a file
        vga_writestring("Test 2: Create and write to file...\n");
        struct fat32_file *file = fat32_fopen("test.txt", "w");
        if (!file) {
            vga_writestring("  FAILED: Could not create test file\n");
            return;
        }
        
        const char *test_data = "Hello, FAT32 World!\nThis is a test file.";
        size_t written = fat32_fwrite(test_data, 1, strlen(test_data), file);
        fat32_fclose(file);
        
        if (written == strlen(test_data)) {
            vga_writestring("  PASSED: File written successfully (");
            print_dec(written);
            vga_writestring(" bytes)\n");
        } else {
            vga_writestring("  FAILED: File write incomplete\n");
            return;
        }
        
        // Test 3: Read back the file
        vga_writestring("Test 3: Read back file contents...\n");
        file = fat32_fopen("test.txt", "r");
        if (!file) {
            vga_writestring("  FAILED: Could not open test file for reading\n");
            return;
        }
        
        char read_buffer[256];
        size_t read_bytes = fat32_fread(read_buffer, 1, sizeof(read_buffer) - 1, file);
        read_buffer[read_bytes] = '\0';
        fat32_fclose(file);
        
        if (read_bytes == strlen(test_data) && strcmp(read_buffer, test_data) == 0) {
            vga_writestring("  PASSED: File read successfully\n");
            vga_writestring("  Content: ");
            vga_writestring(read_buffer);
            vga_putchar('\n');
        } else {
            vga_writestring("  FAILED: File read mismatch\n");
            vga_writestring("  Expected: ");
            vga_writestring(test_data);
            vga_writestring("\n  Got:      ");
            vga_writestring(read_buffer);
            vga_putchar('\n');
            return;
        }
        
        // Test 4: File existence check
        vga_writestring("Test 4: File existence check...\n");
        if (fat32_exists("test.txt") && fat32_get_file_size("test.txt") == strlen(test_data)) {
            vga_writestring("  PASSED: File exists with correct size\n");
        } else {
            vga_writestring("  FAILED: File existence/size check failed\n");
            return;
        }
        
        vga_writestring("All FAT32 tests PASSED!\n");
    } else if (strcmp(command, "reboot") == 0) {
        vga_writestring("Rebooting system...\n");
        // Simple reboot via keyboard controller
        outb(0x64, 0xFE);
        hang();
    } else {
        vga_writestring("Unknown command: ");
        vga_writestring(command);
        vga_writestring("\nType 'help' for available commands.\n");
    }
}