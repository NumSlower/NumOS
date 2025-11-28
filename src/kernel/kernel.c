#include "kernel/kernel.h"
#include "drivers/vga.h"

/* Basic memory and string functions */
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

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = s1;
    const unsigned char *p2 = s2;
    
    while (n-- > 0) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
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

char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

char *strncpy(char *dest, const char *src, size_t n) {
    char *d = dest;
    while (n && (*d++ = *src++)) {
        n--;
    }
    while (n--) {
        *d++ = '\0';
    }
    return dest;
}

char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++; /* Find end of dest */
    while ((*d++ = *src++)); /* Copy src */
    return dest;
}

char *strstr(const char *haystack, const char *needle) {
    const char *h, *n;
    
    if (!*needle) {
        return (char*)haystack;
    }
    
    while (*haystack) {
        h = haystack;
        n = needle;
        
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        
        if (!*n) {
            return (char*)haystack;
        }
        
        haystack++;
    }
    
    return NULL;
}

/* System functions */
void panic(const char *message) {
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("KERNEL PANIC: ");
    vga_writestring(message);
    vga_putchar('\n');
    vga_writestring("System halted.\n");
    hang();
}

void hang(void) {
    __asm__ volatile("cli"); /* Disable interrupts */
    while (1) {
        __asm__ volatile("hlt"); /* Halt until next interrupt */
    }
}

/* Utility functions for printing numbers */
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

void print_hex32(uint32_t value) {
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[9];
    buffer[8] = '\0';
    
    for (int i = 7; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    
    vga_writestring("0x");
    vga_writestring(buffer);
}

void print_dec(uint64_t value) {
    char buffer[21]; /* Enough for 64-bit number */
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

void print_dec32(uint32_t value) {
    char buffer[11]; /* Enough for 32-bit number */
    int pos = 10;
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

/* Debug function to print memory contents */
void print_memory(const void *ptr, size_t size) {
    const uint8_t *data = (const uint8_t*)ptr;
    
    vga_writestring("Memory dump at ");
    print_hex((uint64_t)ptr);
    vga_writestring(" (");
    print_dec(size);
    vga_writestring(" bytes):\n");
    
    for (size_t i = 0; i < size; i++) {
        if (i % 16 == 0) {
            print_hex((uint64_t)(data + i));
            vga_writestring(": ");
        }
        
        /* Print hex value */
        if (data[i] < 0x10) {
            vga_putchar('0');
        }
        print_hex32(data[i]);
        vga_putchar(' ');
        
        if (i % 16 == 15 || i == size - 1) {
            /* Print ASCII representation */
            size_t ascii_start = (i / 16) * 16;
            size_t ascii_end = i;
            
            /* Pad with spaces if needed */
            for (size_t j = i % 16; j < 15; j++) {
                vga_writestring("   ");
            }
            
            vga_writestring(" |");
            for (size_t j = ascii_start; j <= ascii_end; j++) {
                char c = (char)data[j];
                if (c >= 32 && c <= 126) {
                    vga_putchar(c);
                } else {
                    vga_putchar('.');
                }
            }
            vga_writestring("|\n");
        }
    }
}

/* Simple checksum calculation */
uint32_t calculate_checksum(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t*)data;
    uint32_t checksum = 0;
    
    for (size_t i = 0; i < size; i++) {
        checksum += bytes[i];
        checksum = (checksum << 1) | (checksum >> 31); /* Rotate left */
    }
    
    return checksum;
}

/* String to number conversion functions */
long strtol(const char *str, char **endptr, int base) {
    long result = 0;
    int sign = 1;
    const char *p = str;
    
    /* Skip whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\n' || 
           *p == '\r' || *p == '\f' || *p == '\v') {
        p++;
    }
    
    /* Handle sign */
    if (*p == '-') {
        sign = -1;
        p++;
    } else if (*p == '+') {
        p++;
    }
    
    /* Auto-detect base if base is 0 */
    if (base == 0) {
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') {
                base = 16;
                p += 2;
            } else {
                base = 8;
                p++;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && *p == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
    }
    
    /* Convert digits */
    while (*p) {
        int digit;
        
        if (*p >= '0' && *p <= '9') {
            digit = *p - '0';
        } else if (*p >= 'a' && *p <= 'z') {
            digit = *p - 'a' + 10;
        } else if (*p >= 'A' && *p <= 'Z') {
            digit = *p - 'A' + 10;
        } else {
            break;
        }
        
        if (digit >= base) {
            break;
        }
        
        result = result * base + digit;
        p++;
    }
    
    if (endptr) {
        *endptr = (char*)p;
    }
    
    return result * sign;
}

/* Simple printf-like function for kernel debugging */
void kprintf(const char *format, ...) {
    /* This is a very basic implementation */
    /* In a full OS, you'd want a proper printf implementation */
    const char *p = format;
    
    while (*p) {
        if (*p == '%' && *(p + 1)) {
            p++;
            switch (*p) {
                case 'd':
                case 'i':
                    /* Would need va_list implementation for this */
                    vga_writestring("<int>");
                    break;
                case 'x':
                    vga_writestring("<hex>");
                    break;
                case 's':
                    vga_writestring("<str>");
                    break;
                case 'c':
                    vga_writestring("<char>");
                    break;
                case '%':
                    vga_putchar('%');
                    break;
                default:
                    vga_putchar('%');
                    vga_putchar(*p);
                    break;
            }
        } else {
            vga_putchar(*p);
        }
        p++;
    }
}

/* Port I/O functions for hardware access */
uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}