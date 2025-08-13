#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "paging.h"

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
        vga_writestring("  help      - Show this help message\n");
        vga_writestring("  clear     - Clear the screen\n");
        vga_writestring("  version   - Show system version\n");
        vga_writestring("  echo <text> - Echo back text\n");
        vga_writestring("  meminfo   - Show memory information\n");
        vga_writestring("  paging    - Show paging status\n");
        vga_writestring("  testpage  - Test page allocation\n");
        vga_writestring("  translate <addr> - Translate virtual to physical address\n");
        vga_writestring("  reboot    - Restart the system\n");
    } else if (strcmp(command, "clear") == 0) {
        vga_clear();
    } else if (strcmp(command, "version") == 0) {
        vga_writestring("NumOS Version 1.1\n");
        vga_writestring("64-bit Operating System with Paging Support\n");
        vga_writestring("Built with C and Assembly\n");
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
    } else if (strcmp(command, "paging") == 0) {
        vga_writestring("Paging System Status:\n");
        paging_enable(); // This will print status
        vga_writestring("  Page size: 4096 bytes (4 KB)\n");
        vga_writestring("  Large page size: 2097152 bytes (2 MB)\n");
        vga_writestring("  4-level paging active (PML4)\n");
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
            
            // Free the pages
            vmm_free_pages(pages, 2);
            vga_writestring("Pages freed successfully\n");
        } else {
            vga_writestring("Failed to allocate pages\n");
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