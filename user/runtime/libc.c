/*
 * libc.c - Minimal user-space libc layer for NumOS programs.
 *
 * This file provides the small set of freestanding helpers that the userland
 * tools rely on before a fuller hosted libc exists.
 */

#include "libc.h"

extern void __numos_user_thread_entry(numos_thread_start_t start, void *arg);

/* Memory and string helpers used by every user program. */

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

/* Thin syscall-backed I/O wrappers. */

ssize_t write(int fd, const void *buf, size_t len) {
    return sys_write(fd, buf, len);
}

int puts(const char *str) {
    ssize_t wrote = write(FD_STDOUT, str, strlen(str));
    if (wrote < 0) return (int)wrote;
    write(FD_STDOUT, "\n", 1);
    return 0;
}

/* Process termination helpers never return to the caller. */

void exit(int status) {
    sys_exit(status);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

/* Thread wrappers keep the public API stable over raw syscalls. */

int thread_create(numos_thread_start_t start, void *arg) {
    if (!start) return -1;
    return (int)sys_thread_create((void *)start, arg,
                                  (void *)__numos_user_thread_entry);
}

int thread_join(int tid, intptr_t *result) {
    uint64_t value = 0;
    int rc = (int)sys_thread_join(tid, result ? &value : 0);
    if (rc != 0) return rc;
    if (result) *result = (intptr_t)value;
    return 0;
}

void thread_exit(intptr_t value) {
    sys_thread_exit((uint64_t)value);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

int thread_self(void) {
    return (int)sys_thread_self();
}
