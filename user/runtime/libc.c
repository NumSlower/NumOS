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

char *strcpy(char *dest, const char *src) {
    char *out = dest;
    while ((*dest++ = *src++) != '\0') {
    }
    return out;
}

char *strncpy(char *dest, const char *src, size_t len) {
    size_t i = 0;
    for (; i < len && src[i]; i++) dest[i] = src[i];
    for (; i < len; i++) dest[i] = '\0';
    return dest;
}

char *strchr(const char *str, int ch) {
    unsigned char want = (unsigned char)ch;

    while (*str) {
        if ((unsigned char)*str == want) return (char *)str;
        str++;
    }

    if (want == '\0') return (char *)str;
    return 0;
}

char *strrchr(const char *str, int ch) {
    const char *found = 0;
    unsigned char want = (unsigned char)ch;

    while (*str) {
        if ((unsigned char)*str == want) found = str;
        str++;
    }

    if (want == '\0') return (char *)str;
    return (char *)found;
}

int isdigit(int ch) {
    return ch >= '0' && ch <= '9';
}

int islower(int ch) {
    return ch >= 'a' && ch <= 'z';
}

int isupper(int ch) {
    return ch >= 'A' && ch <= 'Z';
}

int isalpha(int ch) {
    return islower(ch) || isupper(ch);
}

int isalnum(int ch) {
    return isalpha(ch) || isdigit(ch);
}

int isspace(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\n' ||
           ch == '\r' || ch == '\f' || ch == '\v';
}

int isxdigit(int ch) {
    return isdigit(ch) ||
           (ch >= 'a' && ch <= 'f') ||
           (ch >= 'A' && ch <= 'F');
}

int tolower(int ch) {
    if (isupper(ch)) return ch - 'A' + 'a';
    return ch;
}

int toupper(int ch) {
    if (islower(ch)) return ch - 'a' + 'A';
    return ch;
}

int abs(int value) {
    return value < 0 ? -value : value;
}

static int digit_value(int ch) {
    if (isdigit(ch)) return ch - '0';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    return -1;
}

static const char *skip_space(const char *text) {
    while (*text && isspace((unsigned char)*text)) text++;
    return text;
}

unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *start = nptr;
    const char *cursor = skip_space(nptr);
    unsigned long value = 0;
    unsigned long max_value = ~0UL;
    int negative = 0;
    int any = 0;

    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        cursor++;
    }

    if (base == 0) {
        if (cursor[0] == '0' && (cursor[1] == 'x' || cursor[1] == 'X')) {
            base = 16;
            cursor += 2;
        } else if (cursor[0] == '0') {
            base = 8;
            cursor++;
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' &&
               (cursor[1] == 'x' || cursor[1] == 'X')) {
        cursor += 2;
    }

    if (base < 2 || base > 36) {
        if (endptr) *endptr = (char *)start;
        return 0;
    }

    for (;;) {
        int digit = digit_value((unsigned char)*cursor);
        if (digit < 0 || digit >= base) break;
        any = 1;
        if (value > (max_value - (unsigned long)digit) / (unsigned long)base) {
            value = max_value;
        } else {
            value = (value * (unsigned long)base) + (unsigned long)digit;
        }
        cursor++;
    }

    if (!any) {
        if (endptr) *endptr = (char *)start;
        return 0;
    }

    if (endptr) *endptr = (char *)cursor;
    if (negative) return (unsigned long)(0UL - value);
    return value;
}

long strtol(const char *nptr, char **endptr, int base) {
    const char *start = nptr;
    const char *cursor = skip_space(nptr);
    unsigned long value = 0;
    unsigned long positive_limit = ((~0UL) >> 1);
    unsigned long negative_limit = positive_limit + 1UL;
    int negative = 0;
    int any = 0;

    if (*cursor == '+' || *cursor == '-') {
        negative = (*cursor == '-');
        cursor++;
    }

    {
        char *value_end = 0;
        value = strtoul(cursor, &value_end, base);
        if (value_end == cursor) {
            if (endptr) *endptr = (char *)start;
            return 0;
        }
        any = 1;
        cursor = value_end;
    }

    if (endptr) *endptr = (char *)cursor;
    if (!any) return 0;

    if (!negative) {
        if (value > positive_limit) return (long)positive_limit;
        return (long)value;
    }

    if (value > negative_limit) return -(long)negative_limit;
    if (value == negative_limit) return (long)(~positive_limit);
    return -(long)value;
}

int atoi(const char *nptr) {
    return (int)strtol(nptr, 0, 10);
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
    numos_user_wait_forever();
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
    numos_user_wait_forever();
}

int thread_self(void) {
    return (int)sys_thread_self();
}
