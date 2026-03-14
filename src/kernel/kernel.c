/*
 * kernel.c - Core kernel utility functions
 *
 * Provides the freestanding implementations of:
 *   - Memory operations  (memset, memcpy, memmove, memcmp)
 *   - String operations  (strlen, strcmp, strncmp, strcpy, strncpy, strcat, strstr)
 *   - Number conversion  (strtol)
 *   - Printing utilities (print_hex, print_hex32, print_dec, print_dec32, print_memory)
 *   - System control     (panic, hang)
 *   - Port I/O           (inb, outb, inw, outw, inl, outl)
 *   - Checksum           (calculate_checksum)
 *
 * No standard library is linked; all implementations are self-contained.
 */

#include "kernel/kernel.h"
#include "drivers/vga.h"

/* =========================================================================
 * Memory operations
 * ======================================================================= */

/*
 * memset - fill len bytes starting at dest with the byte value val.
 * Returns dest.
 */
void *memset(void *dest, int val, size_t len) {
    unsigned char *ptr   = (unsigned char *)dest;
    unsigned char  value = (unsigned char)val;
    while (len--) *ptr++ = value;
    return dest;
}

/*
 * memcpy - copy len bytes from src to dest (regions must not overlap).
 * Returns dest.
 */
void *memcpy(void *dest, const void *src, size_t len) {
    unsigned char       *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) *d++ = *s++;
    return dest;
}

/*
 * memmove - copy len bytes from src to dest, handling overlap correctly.
 * Returns dest.
 */
void *memmove(void *dest, const void *src, size_t len) {
    unsigned char       *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || len == 0) return dest;

    if (d < s) {
        while (len--) *d++ = *s++;
    } else {
        d += len;
        s += len;
        while (len--) *--d = *--s;
    }

    return dest;
}

/*
 * memcmp - compare n bytes of s1 and s2.
 * Returns 0 if equal, negative if s1 < s2, positive if s1 > s2.
 */
int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;
    while (n--) {
        if (*p1 != *p2) return *p1 - *p2;
        p1++;
        p2++;
    }
    return 0;
}

/* =========================================================================
 * String operations
 * ======================================================================= */

/*
 * strlen - return the number of bytes in str before the null terminator.
 */
size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

/*
 * strcmp - compare two null-terminated strings.
 * Returns 0 if equal, negative if str1 < str2, positive if str1 > str2.
 */
int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) { str1++; str2++; }
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

/*
 * strncmp - compare at most n bytes of two strings.
 */
int strncmp(const char *str1, const char *str2, size_t n) {
    if (!n) return 0;
    while (n && *str1 && (*str1 == *str2)) { str1++; str2++; n--; }
    if (!n) return 0;
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

/*
 * strcpy - copy src (including null terminator) to dest.
 * Returns dest.
 */
char *strcpy(char *dest, const char *src) {
    char *d = dest;
    while ((*d++ = *src++));
    return dest;
}

/*
 * strncpy - copy at most n bytes of src to dest; zero-pads if src is shorter.
 * Returns dest.
 */
char *strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

/*
 * strcat - append src to dest.
 * Returns dest. Dest must have enough space for both strings.
 */
char *strcat(char *dest, const char *src) {
    char *d = dest;
    while (*d) d++;
    while ((*d++ = *src++));
    return dest;
}

/*
 * strstr - locate the first occurrence of needle in haystack.
 * Returns a pointer to the match, or NULL if not found.
 */
char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;

    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) { h++; n++; }
        if (!*n) return (char *)haystack;
        haystack++;
    }

    return NULL;
}

/* =========================================================================
 * Number conversion
 * ======================================================================= */

/*
 * strtol - convert a string to a long integer.
 * base may be 2-36 or 0 for auto-detection (0x = hex, 0 = octal, else decimal).
 * If endptr is non-NULL, stores the address of the first unconverted character.
 */
long strtol(const char *str, char **endptr, int base) {
    long        result = 0;
    int         sign   = 1;
    const char *p      = str;

    /* Skip leading whitespace */
    while (*p == ' ' || *p == '\t' || *p == '\n' ||
           *p == '\r' || *p == '\f' || *p == '\v') {
        p++;
    }

    /* Optional sign */
    if (*p == '-')      { sign = -1; p++; }
    else if (*p == '+') {             p++; }

    /* Auto-detect or validate base */
    if (base == 0) {
        if (*p == '0') {
            if (p[1] == 'x' || p[1] == 'X') { base = 16; p += 2; }
            else                              { base = 8;  p++;    }
        } else {
            base = 10;
        }
    } else if (base == 16) {
        if (*p == '0' && (p[1] == 'x' || p[1] == 'X')) p += 2;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)str;
        return 0;
    }

    /* Convert digits */
    while (*p) {
        int digit;
        if      (*p >= '0' && *p <= '9') digit = *p - '0';
        else if (*p >= 'a' && *p <= 'z') digit = *p - 'a' + 10;
        else if (*p >= 'A' && *p <= 'Z') digit = *p - 'A' + 10;
        else break;

        if (digit >= base) break;
        result = result * base + digit;
        p++;
    }

    if (endptr) *endptr = (char *)p;
    return result * sign;
}

/* =========================================================================
 * System control
 * ======================================================================= */

/*
 * panic - display a fatal error message and halt the CPU.
 * Disables interrupts so no further IRQs can fire.
 */
void panic(const char *message) {
    __asm__ volatile("cli");
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\n===== KERNEL PANIC =====\n");
    vga_writestring(message);
    vga_writestring("\n========================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("System halted.\n");
    hang();
}

/*
 * hang - disable interrupts and loop forever on HLT.
 * Used as the final halt for both panic and intentional shutdown.
 */
void hang(void) {
    __asm__ volatile("cli");
    while (1) __asm__ volatile("hlt");
}

/* =========================================================================
 * Printing utilities
 * ======================================================================= */

static const char hex_chars[] = "0123456789ABCDEF";

/*
 * print_hex - write value as a 16-digit (64-bit) hex number to VGA.
 */
void print_hex(uint64_t value) {
    char buffer[17];
    buffer[16] = '\0';
    for (int i = 15; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    vga_writestring("0x");
    vga_writestring(buffer);
}

/*
 * print_hex32 - write value as an 8-digit (32-bit) hex number to VGA.
 */
void print_hex32(uint32_t value) {
    char buffer[9];
    buffer[8] = '\0';
    for (int i = 7; i >= 0; i--) {
        buffer[i] = hex_chars[value & 0xF];
        value >>= 4;
    }
    vga_writestring("0x");
    vga_writestring(buffer);
}

/*
 * print_dec - write value as a decimal number to VGA.
 */
void print_dec(uint64_t value) {
    char buffer[21];
    int  pos = 20;
    buffer[20] = '\0';

    if (value == 0) { vga_putchar('0'); return; }

    while (value > 0 && pos > 0) {
        buffer[--pos] = '0' + (value % 10);
        value /= 10;
    }
    vga_writestring(&buffer[pos]);
}

/*
 * print_dec32 - write a 32-bit decimal value to VGA.
 */
void print_dec32(uint32_t value) {
    char buffer[11];
    int  pos = 10;
    buffer[10] = '\0';

    if (value == 0) { vga_putchar('0'); return; }

    while (value > 0 && pos > 0) {
        buffer[--pos] = '0' + (value % 10);
        value /= 10;
    }
    vga_writestring(&buffer[pos]);
}

/*
 * print_memory - write a hex + ASCII dump of size bytes starting at ptr.
 * Each row shows 16 bytes with address, hex values, and printable characters.
 */
void print_memory(const void *ptr, size_t size) {
    const uint8_t *data = (const uint8_t *)ptr;

    vga_writestring("Memory at 0x");
    print_hex((uint64_t)ptr);
    vga_writestring(" (");
    print_dec(size);
    vga_writestring(" bytes):\n");

    for (size_t i = 0; i < size; i += 16) {
        print_hex((uint64_t)(data + i));
        vga_writestring(":  ");

        for (size_t j = 0; j < 16; j++) {
            if (i + j < size) {
                uint8_t byte = data[i + j];
                vga_putchar(hex_chars[(byte >> 4) & 0xF]);
                vga_putchar(hex_chars[byte & 0xF]);
            } else {
                vga_writestring("  ");
            }
            vga_putchar(' ');
            if (j == 7) vga_putchar(' ');
        }

        vga_writestring(" |");
        for (size_t j = 0; j < 16 && (i + j) < size; j++) {
            char c = (char)data[i + j];
            vga_putchar((c >= 32 && c <= 126) ? c : '.');
        }
        vga_writestring("|\n");
    }
}

/* =========================================================================
 * Checksum
 * ======================================================================= */

/*
 * calculate_checksum - compute a simple rotating-XOR checksum over data.
 * Used by the heap allocator to detect block header corruption.
 */
uint32_t calculate_checksum(const void *data, size_t size) {
    const uint8_t *bytes    = (const uint8_t *)data;
    uint32_t       checksum = 0;

    for (size_t i = 0; i < size; i++) {
        checksum += bytes[i];
        checksum  = (checksum << 1) | (checksum >> 31);  /* rotate left 1 bit */
    }

    return checksum;
}

/* =========================================================================
 * Port I/O
 * ======================================================================= */

/*
 * inb - read one byte from I/O port.
 */
uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/*
 * outb - write one byte to I/O port.
 */
void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/*
 * inw - read one word (16 bits) from I/O port.
 */
uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/*
 * outw - write one word (16 bits) to I/O port.
 */
void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/*
 * inl - read one dword (32 bits) from I/O port.
 */
uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/*
 * outl - write one dword (32 bits) to I/O port.
 */
void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}