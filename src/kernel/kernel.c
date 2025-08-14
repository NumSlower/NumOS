#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "paging.h"
#include "timer.h"
#include "heap.h"

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
        vga_writestring("  reboot       - Restart the system\n");
    } else if (strcmp(command, "clear") == 0) {
        vga_clear();
    } else if (strcmp(command, "version") == 0) {
        vga_writestring("NumOS Version 2.0\n");
        vga_writestring("64-bit Operating System with Advanced Features\n");
        vga_writestring("- Enhanced paging with VM regions\n");
        vga_writestring("- Kernel heap allocator (kmalloc/kfree)\n");
        vga_writestring("- Timer driver with PIT support\n");
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