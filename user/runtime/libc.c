#include "libc.h"

void *memset(void *dest, int value, size_t len) {
    unsigned char *p = (unsigned char *)dest;
    while (len--) *p++ = (unsigned char)value;
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

int memcmp(const void *a, const void *b, size_t len) {
    const unsigned char *pa = (const unsigned char *)a;
    const unsigned char *pb = (const unsigned char *)b;
    while (len--) {
        if (*pa != *pb) return (int)(*pa - *pb);
        pa++;
        pb++;
    }
    return 0;
}

size_t strlen(const char *str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

int strcmp(const char *a, const char *b) {
    while (*a && (*a == *b)) {
        a++;
        b++;
    }
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

int strncmp(const char *a, const char *b, size_t len) {
    if (len == 0) return 0;
    while (len && *a && (*a == *b)) {
        a++;
        b++;
        len--;
    }
    if (len == 0) return 0;
    return *(const unsigned char *)a - *(const unsigned char *)b;
}

char *strncpy(char *dest, const char *src, size_t len) {
    size_t i = 0;
    for (; i < len && src[i]; i++) dest[i] = src[i];
    for (; i < len; i++) dest[i] = '\0';
    return dest;
}

ssize_t write(int fd, const void *buf, size_t len) {
    return sys_write(fd, buf, len);
}

int puts(const char *str) {
    ssize_t wrote = write(FD_STDOUT, str, strlen(str));
    if (wrote < 0) return (int)wrote;
    write(FD_STDOUT, "\n", 1);
    return 0;
}

void exit(int status) {
    sys_exit(status);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
