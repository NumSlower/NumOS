#include "kernel/kernel.h"
#include "drivers/serial.h"

void *memset(void *dest, int val, size_t len) {
    unsigned char *ptr = (unsigned char *)dest;
    unsigned char value = (unsigned char)val;
    while (len--) *ptr++ = value;
    return dest;
}

void *memcpy(void *dest, const void *src, size_t len) {
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (len--) *d++ = *s++;
    return dest;
}

void *memmove(void *dest, const void *src, size_t len) {
    unsigned char *d = (unsigned char *)dest;
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

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char *str1, const char *str2) {
    while (*str1 && (*str1 == *str2)) {
        str1++;
        str2++;
    }
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

int strncmp(const char *str1, const char *str2, size_t n) {
    if (!n) return 0;
    while (n && *str1 && (*str1 == *str2)) {
        str1++;
        str2++;
        n--;
    }
    if (!n) return 0;
    return *(const unsigned char *)str1 - *(const unsigned char *)str2;
}

char *strncpy(char *dest, const char *src, size_t n) {
    size_t i = 0;
    for (; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

char *strstr(const char *haystack, const char *needle) {
    if (!*needle) return (char *)haystack;

    while (*haystack) {
        const char *h = haystack;
        const char *n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char *)haystack;
        haystack++;
    }

    return NULL;
}

static const char hex_chars[] = "0123456789ABCDEF";

void print_hex(uint64_t value) {
    char buffer[19];
    buffer[0] = '0';
    buffer[1] = 'x';
    for (int i = 0; i < 16; i++) {
        int shift = (15 - i) * 4;
        buffer[i + 2] = hex_chars[(value >> shift) & 0xFU];
    }
    buffer[18] = '\0';
    serial_write(buffer);
}

void print_hex32(uint32_t value) {
    print_hex((uint64_t)value);
}

void print_dec(uint64_t value) {
    char buffer[21];
    int pos = 20;
    buffer[20] = '\0';

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value > 0 && pos > 0) {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    }

    serial_write(&buffer[pos]);
}

void panic(const char *message) {
    serial_write("\nPANIC: ");
    serial_write(message ? message : "(null)");
    serial_putc('\n');
    hang();
}

void hang(void) {
    for (;;) {
        __asm__ volatile("wfe");
    }
}

void kprintf(const char *format, ...) {
    serial_write(format ? format : "");
}

uint8_t inb(uint16_t port) {
    (void)port;
    return 0;
}

void outb(uint16_t port, uint8_t val) {
    (void)port;
    (void)val;
}

uint16_t inw(uint16_t port) {
    (void)port;
    return 0;
}

void outw(uint16_t port, uint16_t val) {
    (void)port;
    (void)val;
}

uint32_t inl(uint16_t port) {
    (void)port;
    return 0;
}

void outl(uint16_t port, uint32_t val) {
    (void)port;
    (void)val;
}
