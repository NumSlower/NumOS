#include "kernel.h"
#include "vga.h"
#include "keyboard.h"

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

void show_gdt_info(void) {
    uint16_t gdt_limit;
    uint64_t gdt_base;
    
    // Get current GDT info using SGDT instruction
    __asm__ volatile (
        "sgdt %0"
        : "=m" (gdt_limit), "=m" (gdt_base)
        :
        : "memory"
    );
    
    vga_writestring("GDT Information:\n");
    vga_writestring("  Base Address: ");
    print_hex(gdt_base);
    vga_writestring("\n  Limit: ");
    print_hex(gdt_limit);
    vga_writestring("\n  Entries: ");
    
    // Calculate number of entries
    int entries = (gdt_limit + 1) / 8;
    char num_str[10];
    int i = 0;
    int temp = entries;
    
    if (temp == 0) {
        num_str[i++] = '0';
    } else {
        while (temp > 0) {
            num_str[i++] = '0' + (temp % 10);
            temp /= 10;
        }
    }
    
    // Reverse the string
    for (int j = 0; j < i / 2; j++) {
        char temp_char = num_str[j];
        num_str[j] = num_str[i - 1 - j];
        num_str[i - 1 - j] = temp_char;
    }
    num_str[i] = '\0';
    
    vga_writestring(num_str);
    vga_putchar('\n');
}

void process_command(const char *command) {
    if (strlen(command) == 0) {
        return;
    }
    
    if (strcmp(command, "help") == 0) {
        vga_writestring("NumOS Commands:\n");
        vga_writestring("  help    - Show this help message\n");
        vga_writestring("  clear   - Clear the screen\n");
        vga_writestring("  version - Show system version\n");
        vga_writestring("  echo    - Echo back text\n");
        vga_writestring("  gdt     - Show GDT information\n");
        vga_writestring("  reboot  - Restart the system\n");
    } else if (strcmp(command, "clear") == 0) {
        vga_clear();
    } else if (strcmp(command, "version") == 0) {
        vga_writestring("NumOS Version 1.0\n");
        vga_writestring("64-bit Operating System\n");
        vga_writestring("Built with C and Assembly!\n");
    } else if (strcmp(command, "gdt") == 0) {
        show_gdt_info();
    } else if (strncmp(command, "echo ", 5) == 0) {
        vga_writestring(command + 5);
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