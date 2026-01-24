/*
 * kernel.c - Core kernel utility functions
 * Provides essential string, memory, and I/O operations
 */

#include "kernel/kernel.h"
#include "drivers/vga.h"

/*===========================================================================
 * MEMORY OPERATIONS
 *===========================================================================*/

/**
 * memset - Fill memory with a constant byte
 * @dest: Pointer to memory area
 * @val: Value to fill (converted to unsigned char)
 * @len: Number of bytes to fill
 * Return: Pointer to dest
 */
void *memset(void *dest, int val, size_t len) {
    unsigned char *ptr = (unsigned char*)dest;
    unsigned char value = (unsigned char)val;
    
    while (len--) {
        *ptr++ = value;
    }
    
    return dest;
}

/**
 * memcpy - Copy memory area (non-overlapping)
 * @dest: Destination memory area
 * @src: Source memory area
 * @len: Number of bytes to copy
 * Return: Pointer to dest
 */
void *memcpy(void *dest, const void *src, size_t len) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    
    while (len--) {
        *d++ = *s++;
    }
    
    return dest;
}

/**
 * memmove - Copy memory area (handles overlapping)
 * @dest: Destination memory area
 * @src: Source memory area
 * @len: Number of bytes to copy
 * Return: Pointer to dest
 */
void *memmove(void *dest, const void *src, size_t len) {
    unsigned char *d = (unsigned char*)dest;
    const unsigned char *s = (const unsigned char*)src;
    
    if (d == s || len == 0) {
        return dest;
    }
    
    /* Copy forward if dest is before src, backward if after */
    if (d < s) {
        while (len--) {
            *d++ = *s++;
        }
    } else {
        d += len;
        s += len;
        while (len--) {
            *--d = *--s;
        }
    }
    
    return dest;
}

/**
 * memcmp - Compare memory areas
 * @s1: First memory area
 * @s2: Second memory area
 * @n: Number of bytes to compare
 * Return: 0 if equal, <0 if s1<s2, >0 if s1>s2
 */
int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char*)s1;
    const unsigned char *p2 = (const unsigned char*)s2;
    
    while (n--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    
    return 0;
}

/*===========================================================================
 * STRING OPERATIONS
 *===========================================================================*/

/**
 * strlen - Calculate the length of a string
 * @str: The string
 * Return: Length of string (not including null terminator)
 */
size_t strlen(const char *str) {
    size_t len = 0;
    
    while (str[len]) {
        len++;
    }
    
    return len;
}

/**
 * strcmp - Compare two strings
 * @str1: First string
 * @str2: Second string
 * Return: 0 if equal, <0 if str1<str2, >0 if str1>str2
 */
int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

/**
 * strncmp - Compare two strings up to n characters
 * @str1: First string
 * @str2: Second string
 * @n: Maximum number of characters to compare
 * Return: 0 if equal, <0 if str1<str2, >0 if str1>str2
 */
int strncmp(const char *str1, const char *str2, size_t n) {
    if (n == 0) {
        return 0;
    }
    
    while (n && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
        n--;
    }
    
    if (n == 0) {
        return 0;
    }
    
    return *(const unsigned char*)str1 - *(const unsigned char*)str2;
}

/**
 * strcpy - Copy a string
 * @dest: Destination buffer
 * @src: Source string
 * Return: Pointer to dest
 */
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    
    while ((*d++ = *src++));
    
    return dest;
}

/**
 * strncpy - Copy up to n characters of a string
 * @dest: Destination buffer
 * @src: Source string
 * @n: Maximum number of characters to copy
 * Return: Pointer to dest
 */
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    
    return dest;
}

/**
 * strcat - Concatenate two strings
 * @dest: Destination string (must have enough space)
 * @src: Source string
 * Return: Pointer to dest
 */
char *strcat(char *dest, const char *src) {
    char *d = dest;
    
    /* Find end of dest */
    while (*d) {
        d++;
    }
    
    /* Copy src */
    while ((*d++ = *src++));
    
    return dest;
}

/**
 * strstr - Find substring in string
 * @haystack: String to search in
 * @needle: Substring to find
 * Return: Pointer to first occurrence, or NULL if not found
 */
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) {
        return (char*)haystack;
    }
    
    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        
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

/*===========================================================================
 * NUMBER CONVERSION
 *===========================================================================*/

/**
 * strtol - Convert string to long integer
 * @str: String to convert
 * @endptr: If not NULL, stores address of first invalid character
 * @base: Number base (2-36, or 0 for auto-detect)
 * Return: Converted value
 */
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
    
    /* Auto-detect base */
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
    } else if (base == 16) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) {
            p += 2;
        }
    }
    
    /* Validate base */
    if (base < 2 || base > 36) {
        if (endptr) {
            *endptr = (char*)str;
        }
        return 0;
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

/*===========================================================================
 * SYSTEM FUNCTIONS
 *===========================================================================*/

/**
 * panic - Kernel panic (unrecoverable error)
 * @message: Error message to display
 */
void panic(const char *message) {
    __asm__ volatile("cli");  /* Disable interrupts */
    
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\n===== KERNEL PANIC =====\n");
    vga_writestring(message);
    vga_writestring("\n========================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("System halted.\n");
    
    hang();
}

/**
 * hang - Halt the system indefinitely
 */
void hang(void) {
    __asm__ volatile("cli");  /* Disable interrupts */
    
    while (1) {
        __asm__ volatile("hlt");
    }
}

/*===========================================================================
 * PRINTING UTILITIES
 *===========================================================================*/

/**
 * print_hex - Print 64-bit value in hexadecimal
 * @value: Value to print
 */
void print_hex(uint64_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[17];
    int i;
    
    buffer[16] = '\0';
    
    for (i = 15; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    
    vga_writestring("0x");
    vga_writestring(buffer);
}

/**
 * print_hex32 - Print 32-bit value in hexadecimal
 * @value: Value to print
 */
void print_hex32(uint32_t value) {
    static const char hex_chars[] = "0123456789ABCDEF";
    char buffer[9];
    int i;
    
    buffer[8] = '\0';
    
    for (i = 7; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    
    vga_writestring("0x");
    vga_writestring(buffer);
}

/**
 * print_dec - Print 64-bit value in decimal
 * @value: Value to print
 */
void print_dec(uint64_t value) {
    char buffer[21];  /* Max for 64-bit: 18446744073709551615 */
    int pos = 20;
    
    buffer[20] = '\0';
    
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

/**
 * print_dec32 - Print 32-bit value in decimal
 * @value: Value to print
 */
void print_dec32(uint32_t value) {
    char buffer[11];  /* Max for 32-bit: 4294967295 */
    int pos = 10;
    
    buffer[10] = '\0';
    
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

/**
 * print_memory - Dump memory contents (hex dump)
 * @ptr: Pointer to memory
 * @size: Number of bytes to dump
 */
void print_memory(const void *ptr, size_t size) {
    const uint8_t *data = (const uint8_t*)ptr;
    size_t i, j;
    
    vga_writestring("Memory at 0x");
    print_hex((uint64_t)ptr);
    vga_writestring(" (");
    print_dec(size);
    vga_writestring(" bytes):\n");
    
    for (i = 0; i < size; i += 16) {
        /* Print address */
        print_hex((uint64_t)(data + i));
        vga_writestring(":  ");
        
        /* Print hex bytes */
        for (j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t byte = data[i + j];
                static const char hex_chars[] = "0123456789ABCDEF";
                vga_putchar(hex_chars[(byte >> 4) & 0xF]);
                vga_putchar(hex_chars[byte & 0xF]);
            } else {
                vga_writestring("  ");
            }
            vga_putchar(' ');
            
            if (j == 7) {
                vga_putchar(' ');
            }
        }
        
        /* Print ASCII */
        vga_writestring(" |");
        for (j = 0; j < 16 && (i + j) < size; j++) {
            char c = (char)data[i + j];
            if (c >= 32 && c <= 126) {
                vga_putchar(c);
            } else {
                vga_putchar('.');
            }
        }
        vga_writestring("|\n");
    }
}

/**
 * calculate_checksum - Simple checksum calculation
 * @data: Data to checksum
 * @size: Size of data
 * Return: Checksum value
 */
uint32_t calculate_checksum(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t*)data;
    uint32_t checksum = 0;
    size_t i;
    
    for (i = 0; i < size; i++) {
        checksum += bytes[i];
        checksum = (checksum << 1) | (checksum >> 31);  /* Rotate left */
    }
    
    return checksum;
}

/*===========================================================================
 * PORT I/O OPERATIONS
 *===========================================================================*/

/**
 * inb - Read byte from I/O port
 * @port: Port number
 * Return: Byte read from port
 */
uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/**
 * outb - Write byte to I/O port
 * @port: Port number
 * @val: Byte to write
 */
void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * inw - Read word from I/O port
 * @port: Port number
 * Return: Word read from port
 */
uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/**
 * outw - Write word to I/O port
 * @port: Port number
 * @val: Word to write
 */
void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * inl - Read long from I/O port
 * @port: Port number
 * Return: Long read from port
 */
uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/**
 * outl - Write long to I/O port
 * @port: Port number
 * @val: Long to write
 */
void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/**
 * io_wait - Short delay for I/O operations
 * Uses port 0x80 (POST port) for timing
 */
void io_wait(void) {
    outb(0x80, 0);
}