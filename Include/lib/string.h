#ifndef _STRING_H
#define _STRING_H

#include "stddef.h"
#include "stdint.h"

/* 
 * Basic memory operations for kernel use.
 * These are simple, freestanding versions with no libc dependency.
 */

static inline void* memset(void* dest, int value, size_t count) {
    unsigned char* ptr = (unsigned char*)dest;
    while (count--) *ptr++ = (unsigned char)value;
    return dest;
}

static inline void* memcpy(void* dest, const void* src, size_t count) {
    const unsigned char* s = (const unsigned char*)src;
    unsigned char* d = (unsigned char*)dest;
    while (count--) *d++ = *s++;
    return dest;
}

/* Safe overlapping copy */
static inline void* memmove(void* dest, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dest;
    const unsigned char* s = (const unsigned char*)src;

    if (d == s || n == 0) return dest;

    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n;
        s += n;
        while (n--) *--d = *--s;
    }
    return dest;
}

static inline int memcmp(const void* s1, const void* s2, size_t count) {
    const unsigned char* a = (const unsigned char*)s1;
    const unsigned char* b = (const unsigned char*)s2;
    while (count--) {
        if (*a != *b) return *a - *b;
        a++;
        b++;
    }
    return 0;
}

static inline size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static inline char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while ((*d++ = *src++));
    return dest;
}

static inline char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i]; i++) dest[i] = src[i];
    for (; i < n; i++) dest[i] = '\0';
    return dest;
}

static inline int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) { s1++; s2++; }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

#endif
