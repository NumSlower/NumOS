#include "kernel.h"
#include "vga.h"
#include <keyboard.h>
#include <memory.h>
#include <idt.h>

// Function prototypes to resolve implicit declaration errors
void interrupts_disable();
void print_system_info();
void print_memory_info();
uint64_t timer_get_ticks();
void timer_wait(uint64_t ticks);

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
    interrupts_disable();
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("KERNEL PANIC: ");
    vga_writestring(message);
    vga_writestring("\nSystem halted.");
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
    vga_writestring(">");
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

void test_memory_allocation(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_BLUE, VGA_COLOR_BLACK));
    vga_writestring("Testing Memory Allocation...\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    
    /* Test small allocations */
    void *ptr1 = kmalloc(64);
    void *ptr2 = kmalloc(128);
    void *ptr3 = kmalloc(256);
    
    if (ptr1 && ptr2 && ptr3) {
        vga_writestring("Small allocations: SUCCESS\n");
        
        /* Test writing to allocated memory */
        memset(ptr1, 0xAA, 64);
        memset(ptr2, 0xBB, 128);
        memset(ptr3, 0xCC, 256);
        vga_writestring("Memory writes: SUCCESS\n");
        
        /* Free memory */
        kfree(ptr1);
        kfree(ptr2);
        kfree(ptr3);
        vga_writestring("Memory free: SUCCESS\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("Small allocations: FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    /* Test large allocation */
    void *large_ptr = kmalloc(4096);
    if (large_ptr) {
        vga_writestring("Large allocation (4KB): SUCCESS\n");
        memset(large_ptr, 0xDD, 4096);
        vga_writestring("Large memory write: SUCCESS\n");
        kfree(large_ptr);
        vga_writestring("Large memory free: SUCCESS\n");
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("Large allocation: FAILED\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    }
    
    vga_writestring("Memory test completed.\n");
}

void process_command(const char *command) {
    if (strlen(command) == 0) {
        return;
    }
    
    if (strcmp(command, "help") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
        vga_writestring("NumOS Enhanced Commands:\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("  help      - Show this help message\n");
        vga_writestring("  clear     - Clear the screen\n");
        vga_writestring("  version   - Show system version\n");
        vga_writestring("  sysinfo   - Show system information\n");
        vga_writestring("  meminfo   - Show memory information\n");
        vga_writestring("  memtest   - Test memory allocation\n");
        vga_writestring("  echo      - Echo back text\n");
        vga_writestring("  uptime    - Show system uptime\n");
        vga_writestring("  reboot    - Restart the system\n");
        vga_writestring("  halt      - Halt the system\n");
    } else if (strcmp(command, "clear") == 0) {
        vga_clear();
    } else if (strcmp(command, "version") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK));
        vga_writestring("NumOS Enhanced Edition Version 1.0\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("64-bit Operating System\n");
        vga_writestring("Features: GDT, IDT, Paging, Memory Management\n");
        vga_writestring("Built with C and Assembly\n");
    } else if (strcmp(command, "sysinfo") == 0) {
        print_system_info();
    } else if (strcmp(command, "meminfo") == 0) {
        print_memory_info();
    } else if (strcmp(command, "memtest") == 0) {
        test_memory_allocation();
    } else if (strncmp(command, "echo ", 5) == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
        vga_writestring(command + 5);
        vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else if (strcmp(command, "uptime") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring("System Uptime: ");
        uint64_t seconds = timer_get_ticks() / 100;
        uint64_t minutes = seconds / 60;
        uint64_t hours = minutes / 60;
        
        /* Display hours */
        if (hours > 0) {
            vga_putchar('0' + (hours / 10) % 10);
            vga_putchar('0' + hours % 10);
            vga_putchar('h');
            vga_putchar(' ');
        }
        
        /* Display minutes */
        minutes %= 60;
        vga_putchar('0' + (minutes / 10) % 10);
        vga_putchar('0' + minutes % 10);
        vga_putchar('m');
        vga_putchar(' ');
        
        /* Display seconds */
        seconds %= 60;
        vga_putchar('0' + (seconds / 10) % 10);
        vga_putchar('0' + seconds % 10);
        vga_putchar('s');
        vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    } else if (strcmp(command, "reboot") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("Rebooting system...\n");
        timer_wait(100); /* Wait 1 second */
        /* Triple fault method */
        interrupts_disable();
        uint64_t *invalid = (uint64_t*)0xFFFFFFFFFFFFFFFFUL;
        *invalid = 0;
        /* Alternative: keyboard controller reset */
        outb(0x64, 0xFE);
        hang();
    } else if (strcmp(command, "halt") == 0) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("System halted. You may power off the computer.\n");
        hang();
    } else {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("Unknown command: ");
        vga_writestring(command);
        vga_putchar('\n');
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("Type 'help' for available commands.\n");
    }
}