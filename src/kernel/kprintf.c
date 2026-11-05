/* NumOS — kprintf.c — Kernel formatted printing */
#include "libs/kprintf.h"
#include "libs/string.h"
#include "libs/io.h"

#define MAX_BACKENDS 4
static kprintf_putchar_t backends[MAX_BACKENDS];
static int num_backends = 0;

static void early_vga_putchar(char c) {
    static uint16_t* vga_buffer = (uint16_t*)0xB8000;
    static int pos = 0;
    if (c == '\n') {
        pos += 80 - (pos % 80);
    } else {
        vga_buffer[pos++] = (uint16_t)c | (0x0F << 8);
    }
    if (pos >= 80 * 25) {
        // Simple scroll or just reset
        pos = 0;
    }
}

void kprintf_register_putchar(kprintf_putchar_t fn) {
    if (num_backends < MAX_BACKENDS) {
        backends[num_backends++] = fn;
    }
}

static void print_char(char** buf, size_t* size, size_t* count, char c) {
    if (buf && *buf && size && *size > 1) {
        *(*buf)++ = c;
        (*size)--;
    } else if (!buf) {
        // Output to backends
        if (num_backends == 0) {
            early_vga_putchar(c);
        } else {
            for (int i = 0; i < num_backends; i++) {
                backends[i](c);
            }
        }
    }
    if (count) (*count)++;
}

static void print_string(char** buf, size_t* size, size_t* count, const char* s) {
    if (!s) s = "(null)";
    while (*s) {
        print_char(buf, size, count, *s++);
    }
}

static void print_number(char** buf, size_t* size, size_t* count, uint64_t num, int base, bool sign, bool uppercase, int width, char pad_char) {
    char tmp[64];
    int i = 0;
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    bool is_neg = false;

    if (sign && (int64_t)num < 0) {
        is_neg = true;
        num = (uint64_t)(-(int64_t)num);
    }

    if (num == 0) {
        tmp[i++] = '0';
    } else {
        while (num != 0) {
            tmp[i++] = digits[num % base];
            num /= base;
        }
    }

    if (is_neg) {
        tmp[i++] = '-';
    }

    while (i < width) {
        tmp[i++] = pad_char;
    }

    while (i > 0) {
        print_char(buf, size, count, tmp[--i]);
    }
}

int kvsnprintf(char* buf, size_t size, const char* fmt, va_list args) {
    size_t count = 0;
    char* b = buf;
    size_t s = size;

    while (*fmt) {
        if (*fmt != '%') {
            print_char(buf ? &b : NULL, buf ? &s : NULL, &count, *fmt++);
            continue;
        }
        
        fmt++;
        if (*fmt == '\0') break;

        char pad_char = ' ';
        int width = 0;
        bool is_long = false;
        
        if (*fmt == '0') {
            pad_char = '0';
            fmt++;
        }
        while (*fmt >= '0' && *fmt <= '9') {
            width = width * 10 + (*fmt - '0');
            fmt++;
        }
        
        if (*fmt == 'l') {
            is_long = true;
            fmt++;
            if (*fmt == 'l') fmt++;   /* accept %ll (long long) */
        } else if (*fmt == 'z') {     /* size_t */
            is_long = true;
            fmt++;
        } else if (*fmt == 'h') {     /* short / char — promoted to int anyway */
            fmt++;
            if (*fmt == 'h') fmt++;
        }

        switch (*fmt) {
            case 'c':
                print_char(buf ? &b : NULL, buf ? &s : NULL, &count, (char)va_arg(args, int));
                break;
            case 's':
                print_string(buf ? &b : NULL, buf ? &s : NULL, &count, va_arg(args, const char*));
                break;
            case 'd':
            case 'i': {
                int64_t num = is_long ? va_arg(args, int64_t) : va_arg(args, int32_t);
                print_number(buf ? &b : NULL, buf ? &s : NULL, &count, (uint64_t)num, 10, true, false, width, pad_char);
                break;
            }
            case 'u': {
                uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                print_number(buf ? &b : NULL, buf ? &s : NULL, &count, num, 10, false, false, width, pad_char);
                break;
            }
            case 'x':
            case 'X': {
                uint64_t num = is_long ? va_arg(args, uint64_t) : va_arg(args, uint32_t);
                print_number(buf ? &b : NULL, buf ? &s : NULL, &count, num, 16, false, (*fmt == 'X'), width, pad_char);
                break;
            }
            case 'p': {
                uint64_t num = (uint64_t)va_arg(args, void*);
                print_string(buf ? &b : NULL, buf ? &s : NULL, &count, "0x");
                print_number(buf ? &b : NULL, buf ? &s : NULL, &count, num, 16, false, false, 16, '0');
                break;
            }
            case '%':
                print_char(buf ? &b : NULL, buf ? &s : NULL, &count, '%');
                break;
            default:
                print_char(buf ? &b : NULL, buf ? &s : NULL, &count, '%');
                print_char(buf ? &b : NULL, buf ? &s : NULL, &count, *fmt);
                break;
        }
        fmt++;
    }

    if (buf && s > 0) {
        *b = '\0';
    }

    return (int)count;
}

int ksprintf(char* buf, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = kvsnprintf(buf, (size_t)-1, fmt, args);
    va_end(args);
    return ret;
}

int kprintf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int ret = kvsnprintf(NULL, 0, fmt, args);
    va_end(args);
    return ret;
}
