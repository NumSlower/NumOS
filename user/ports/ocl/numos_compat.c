#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "syscalls.h"
#include "libc.h"
#include "ctype.h"
#include "errno.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"
#include "strings.h"
#include "time.h"

#define OCL_HEAP_SIZE (8 * 1024 * 1024)
#define OCL_FILE_READ  0x01u
#define OCL_FILE_WRITE 0x02u
#define OCL_FILE_MEM   0x04u
#define OCL_FILE_STATIC 0x08u

typedef struct HeapBlock {
    size_t size;
    int free;
    struct HeapBlock *next;
} HeapBlock;

typedef struct FormatSink {
    char *buf;
    size_t cap;
    size_t len;
} FormatSink;

static unsigned char g_heap[OCL_HEAP_SIZE];
static HeapBlock *g_heap_head;
static unsigned int g_rand_state = 1;

static FILE g_stdin_file = { FD_STDIN, OCL_FILE_READ | OCL_FILE_STATIC, 0, 0, 0, 0, 0 };
static FILE g_stdout_file = { FD_STDOUT, OCL_FILE_WRITE | OCL_FILE_STATIC, 0, 0, 0, 0, 0 };
static FILE g_stderr_file = { FD_STDERR, OCL_FILE_WRITE | OCL_FILE_STATIC, 0, 0, 0, 0, 0 };

FILE *stdin = &g_stdin_file;
FILE *stdout = &g_stdout_file;
FILE *stderr = &g_stderr_file;

int errno = 0;

static size_t align_up(size_t value) {
    return (value + sizeof(uintptr_t) - 1) & ~(sizeof(uintptr_t) - 1);
}

static size_t umin(size_t a, size_t b) {
    return a < b ? a : b;
}

static void heap_init_once(void) {
    if (g_heap_head) return;
    g_heap_head = (HeapBlock *)g_heap;
    g_heap_head->size = OCL_HEAP_SIZE - sizeof(HeapBlock);
    g_heap_head->free = 1;
    g_heap_head->next = 0;
}

static void split_block(HeapBlock *block, size_t size) {
    if (block->size <= size + sizeof(HeapBlock) + sizeof(uintptr_t)) return;

    HeapBlock *next = (HeapBlock *)((unsigned char *)(block + 1) + size);
    next->size = block->size - size - sizeof(HeapBlock);
    next->free = 1;
    next->next = block->next;
    block->size = size;
    block->next = next;
}

static void coalesce_blocks(void) {
    HeapBlock *block = g_heap_head;
    while (block && block->next) {
        if (block->free && block->next->free) {
            block->size += sizeof(HeapBlock) + block->next->size;
            block->next = block->next->next;
            continue;
        }
        block = block->next;
    }
}

void *malloc(size_t size) {
    HeapBlock *block;

    if (size == 0) return 0;
    heap_init_once();
    size = align_up(size);

    for (block = g_heap_head; block; block = block->next) {
        if (!block->free || block->size < size) continue;
        split_block(block, size);
        block->free = 0;
        return (void *)(block + 1);
    }

    errno = ENOMEM;
    return 0;
}

void free(void *ptr) {
    HeapBlock *block;

    if (!ptr) return;
    block = ((HeapBlock *)ptr) - 1;
    block->free = 1;
    coalesce_blocks();
}

void *realloc(void *ptr, size_t size) {
    HeapBlock *block;
    void *out;

    if (!ptr) return malloc(size);
    if (size == 0) {
        free(ptr);
        return 0;
    }

    block = ((HeapBlock *)ptr) - 1;
    size = align_up(size);
    if (block->size >= size) return ptr;

    out = malloc(size);
    if (!out) return 0;
    memcpy(out, ptr, block->size);
    free(ptr);
    return out;
}

void abort(void) {
    sys_exit(134);
    numos_user_wait_forever();
}

char *getenv(const char *name) {
    (void)name;
    return 0;
}

int rand(void) {
    g_rand_state = g_rand_state * 1103515245u + 12345u;
    return (int)((g_rand_state >> 1) & RAND_MAX);
}

void srand(unsigned int seed) {
    if (seed == 0) seed = 1;
    g_rand_state = seed;
}

static int digit_value(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'z') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'Z') return ch - 'A' + 10;
    return -1;
}

long long strtoll(const char *nptr, char **endptr, int base) {
    long long sign = 1;
    unsigned long long value = 0;
    const char *s = nptr;
    int digit;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') {
        sign = -1;
        s++;
    } else if (*s == '+') {
        s++;
    }

    if (base == 0) {
        base = 10;
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
            base = 16;
            s += 2;
        }
    }

    while ((digit = digit_value(*s)) >= 0 && digit < base) {
        value = value * (unsigned long long)base + (unsigned long long)digit;
        s++;
    }

    if (endptr) *endptr = (char *)s;
    return (long long)(sign * (long long)value);
}

double strtod(const char *nptr, char **endptr) {
    double sign = 1.0;
    double value = 0.0;
    double scale = 1.0;
    const char *s = nptr;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    if (*s == '-') {
        sign = -1.0;
        s++;
    } else if (*s == '+') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = value * 10.0 + (double)(*s - '0');
        s++;
    }

    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            value = value * 10.0 + (double)(*s - '0');
            scale *= 10.0;
            s++;
        }
    }

    if ((*s == 'e' || *s == 'E') &&
        ((s[1] >= '0' && s[1] <= '9') || ((s[1] == '+' || s[1] == '-') && (s[2] >= '0' && s[2] <= '9')))) {
        int exp_sign = 1;
        int exponent = 0;
        s++;
        if (*s == '-') {
            exp_sign = -1;
            s++;
        } else if (*s == '+') {
            s++;
        }
        while (*s >= '0' && *s <= '9') {
            exponent = exponent * 10 + (*s - '0');
            s++;
        }
        while (exponent-- > 0) {
            if (exp_sign > 0) value *= 10.0;
            else scale *= 10.0;
        }
    }

    if (endptr) *endptr = (char *)s;
    return sign * (value / scale);
}

int isspace(int c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

int isdigit(int c) {
    return c >= '0' && c <= '9';
}

int isalpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

int isalnum(int c) {
    return isalpha(c) || isdigit(c);
}

int isprint(int c) {
    return c >= 32 && c < 127;
}

int tolower(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A' + 'a';
    return c;
}

int toupper(int c) {
    if (c >= 'a' && c <= 'z') return c - 'a' + 'A';
    return c;
}

char *strcpy(char *dest, const char *src) {
    char *out = dest;
    while ((*dest++ = *src++) != '\0') {}
    return out;
}

char *strcat(char *dest, const char *src) {
    char *out = dest + strlen(dest);
    strcpy(out, src);
    return dest;
}

char *strstr(const char *haystack, const char *needle) {
    size_t needle_len;
    if (!haystack || !needle) return 0;
    if (*needle == '\0') return (char *)haystack;
    needle_len = strlen(needle);
    while (*haystack) {
        if (strncmp(haystack, needle, needle_len) == 0) return (char *)haystack;
        haystack++;
    }
    return 0;
}

char *strrchr(const char *s, int c) {
    const char *last = 0;
    if (!s) return 0;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if (c == '\0') return (char *)s;
    return (char *)last;
}

int strcasecmp(const char *a, const char *b) {
    int ca;
    int cb;
    if (a == b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        ca = tolower((unsigned char)*a);
        cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

char *strerror(int errnum) {
    switch (errnum) {
        case 0: return "ok";
        case ENOENT: return "no such file";
        case ENOMEM: return "out of memory";
        case EINVAL: return "invalid argument";
        case EIO: return "i/o error";
        default: return "unknown error";
    }
}

int isatty(int fd) {
    return fd >= FD_STDIN && fd <= FD_STDERR;
}

static FILE *alloc_stream(void) {
    FILE *stream = (FILE *)malloc(sizeof(FILE));
    if (!stream) return 0;
    stream->fd = -1;
    stream->flags = 0;
    stream->error = 0;
    stream->buffer = 0;
    stream->size = 0;
    stream->pos = 0;
    stream->cap = 0;
    return stream;
}

static int ensure_capacity(char **buf, size_t *cap, size_t needed) {
    size_t new_cap;
    char *new_buf;

    if (needed <= *cap) return 0;
    new_cap = *cap ? *cap : 256;
    while (new_cap < needed) {
        if (new_cap > ((size_t)-1) / 2) {
            errno = ENOMEM;
            return -1;
        }
        new_cap *= 2;
    }

    new_buf = (char *)realloc(*buf, new_cap);
    if (!new_buf) {
        errno = ENOMEM;
        return -1;
    }

    *buf = new_buf;
    *cap = new_cap;
    return 0;
}

static int read_entire_file(FILE *stream, int fd) {
    char tmp[512];
    for (;;) {
        int64_t rc = sys_read(fd, tmp, sizeof(tmp));
        if (rc < 0) {
            stream->error = 1;
            errno = (int)(-rc);
            return -1;
        }
        if (rc == 0) break;
        if (ensure_capacity(&stream->buffer, &stream->cap, stream->size + (size_t)rc + 1) < 0) {
            stream->error = 1;
            return -1;
        }
        memcpy(stream->buffer + stream->size, tmp, (size_t)rc);
        stream->size += (size_t)rc;
    }
    if (stream->buffer) stream->buffer[stream->size] = '\0';
    return 0;
}

FILE *fopen(const char *path, const char *mode) {
    FILE *stream;
    int flags = 0;
    bool read_mode = false;
    bool write_mode = false;
    bool append_mode = false;
    bool plus_mode = false;
    int64_t fd;

    if (!path || !mode) {
        errno = EINVAL;
        return 0;
    }

    for (const char *p = mode; *p; ++p) {
        if (*p == 'r') read_mode = true;
        else if (*p == 'w') write_mode = true;
        else if (*p == 'a') append_mode = true;
        else if (*p == '+') plus_mode = true;
    }

    if (!read_mode && !write_mode && !append_mode) {
        errno = EINVAL;
        return 0;
    }

    if (append_mode) flags = FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_APPEND;
    else if (write_mode) flags = FAT32_O_WRONLY | FAT32_O_CREAT | FAT32_O_TRUNC;
    else if (plus_mode) flags = FAT32_O_RDWR;
    else flags = FAT32_O_RDONLY;

    fd = sys_open(path, flags, 0);
    if (fd < 0) {
        errno = (int)(-fd);
        return 0;
    }

    stream = alloc_stream();
    if (!stream) {
        sys_close((int)fd);
        return 0;
    }

    if (read_mode && !write_mode && !append_mode && !plus_mode) {
        stream->flags = OCL_FILE_READ | OCL_FILE_MEM;
        if (read_entire_file(stream, (int)fd) < 0) {
            sys_close((int)fd);
            fclose(stream);
            return 0;
        }
        sys_close((int)fd);
        return stream;
    }

    stream->fd = (int)fd;
    if (read_mode || plus_mode) stream->flags |= OCL_FILE_READ;
    if (write_mode || append_mode || plus_mode) stream->flags |= OCL_FILE_WRITE;
    return stream;
}

int fclose(FILE *stream) {
    int rc = 0;
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }

    if (!(stream->flags & OCL_FILE_STATIC) && !(stream->flags & OCL_FILE_MEM) && stream->fd >= 0) {
        int64_t close_rc = sys_close(stream->fd);
        if (close_rc < 0) {
            stream->error = 1;
            errno = (int)(-close_rc);
            rc = EOF;
        }
    }

    if (!(stream->flags & OCL_FILE_STATIC)) {
        free(stream->buffer);
        free(stream);
    }
    return rc;
}

size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t total;
    size_t avail;
    size_t to_copy;
    int64_t rc;

    if (!ptr || !stream || size == 0 || nmemb == 0) return 0;
    total = size * nmemb;

    if (stream->flags & OCL_FILE_MEM) {
        if (stream->pos >= stream->size) return 0;
        avail = stream->size - stream->pos;
        to_copy = umin(total, avail);
        memcpy(ptr, stream->buffer + stream->pos, to_copy);
        stream->pos += to_copy;
        return to_copy / size;
    }

    rc = sys_read(stream->fd, ptr, total);
    if (rc < 0) {
        stream->error = 1;
        errno = (int)(-rc);
        return 0;
    }
    stream->pos += (size_t)rc;
    return ((size_t)rc) / size;
}

int fseek(FILE *stream, long offset, int whence) {
    size_t base;
    size_t new_pos;

    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    if (!(stream->flags & OCL_FILE_MEM)) {
        errno = EINVAL;
        stream->error = 1;
        return -1;
    }

    if (whence == SEEK_SET) base = 0;
    else if (whence == SEEK_CUR) base = stream->pos;
    else if (whence == SEEK_END) base = stream->size;
    else {
        errno = EINVAL;
        stream->error = 1;
        return -1;
    }

    if (offset < 0) {
        size_t back = (size_t)(-offset);
        if (back > base) {
            errno = EINVAL;
            stream->error = 1;
            return -1;
        }
        new_pos = base - back;
    } else {
        new_pos = base + (size_t)offset;
        if (new_pos > stream->size) {
            errno = EINVAL;
            stream->error = 1;
            return -1;
        }
    }

    stream->pos = new_pos;
    return 0;
}

long ftell(FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    return (long)stream->pos;
}

void rewind(FILE *stream) {
    if (!stream) return;
    stream->pos = 0;
    stream->error = 0;
}

int ferror(FILE *stream) {
    return stream ? stream->error : 1;
}

int fflush(FILE *stream) {
    (void)stream;
    return 0;
}

char *fgets(char *s, int size, FILE *stream) {
    int i = 0;
    if (!s || size <= 0 || !stream) return 0;
    while (i + 1 < size) {
        unsigned char ch;
        size_t got = fread(&ch, 1, 1, stream);
        if (got == 0) break;
        s[i++] = (char)ch;
        if (ch == '\n') break;
    }
    if (i == 0) return 0;
    s[i] = '\0';
    return s;
}

static int write_stream(FILE *stream, const char *buf, size_t len) {
    int64_t rc;
    if (!stream) {
        errno = EINVAL;
        return EOF;
    }
    if (!(stream->flags & OCL_FILE_WRITE)) {
        errno = EINVAL;
        stream->error = 1;
        return EOF;
    }
    rc = sys_write(stream->fd, buf, len);
    if (rc < 0) {
        stream->error = 1;
        errno = (int)(-rc);
        return EOF;
    }
    stream->pos += (size_t)rc;
    if ((size_t)rc != len) {
        stream->error = 1;
        errno = EIO;
        return EOF;
    }
    return (int)rc;
}

int fputs(const char *s, FILE *stream) {
    size_t len;
    if (!s) return EOF;
    len = strlen(s);
    return write_stream(stream, s, len) < 0 ? EOF : (int)len;
}

int fputc(int ch, FILE *stream) {
    char c = (char)ch;
    return write_stream(stream, &c, 1) < 0 ? EOF : (unsigned char)c;
}

int putchar(int ch) {
    return fputc(ch, stdout);
}

int fileno(FILE *stream) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    return stream->fd;
}

static void sink_putc(FormatSink *sink, char ch) {
    if (sink->cap != 0 && sink->len + 1 < sink->cap) sink->buf[sink->len] = ch;
    sink->len++;
}

static void sink_write(FormatSink *sink, const char *s, size_t len) {
    for (size_t i = 0; i < len; i++) sink_putc(sink, s[i]);
}

static void sink_pad(FormatSink *sink, char ch, int count) {
    while (count-- > 0) sink_putc(sink, ch);
}

static size_t utoa_rev(unsigned long long value, unsigned base, bool upper, char *tmp) {
    const char *digits = upper ? "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ" : "0123456789abcdefghijklmnopqrstuvwxyz";
    size_t n = 0;
    if (value == 0) {
        tmp[n++] = '0';
        return n;
    }
    while (value != 0) {
        tmp[n++] = digits[value % base];
        value /= base;
    }
    return n;
}

static void sink_number(FormatSink *sink, const char *prefix, const char *digits, size_t digits_len,
                        bool negative, int width, int precision, bool left, bool zero_pad) {
    size_t prefix_len = prefix ? strlen(prefix) : 0;
    size_t sign_len = negative ? 1u : 0u;
    size_t zeroes = 0;
    size_t content_len;
    int pad;

    if (precision >= 0 && (size_t)precision > digits_len) zeroes = (size_t)precision - digits_len;
    if (precision == 0 && digits_len == 1 && digits[0] == '0') digits_len = 0;
    content_len = sign_len + prefix_len + zeroes + digits_len;
    pad = width > (int)content_len ? width - (int)content_len : 0;

    if (!left && (!zero_pad || precision >= 0)) sink_pad(sink, ' ', pad);
    if (negative) sink_putc(sink, '-');
    if (prefix_len) sink_write(sink, prefix, prefix_len);
    if (!left && zero_pad && precision < 0) sink_pad(sink, '0', pad);
    sink_pad(sink, '0', (int)zeroes);
    sink_write(sink, digits, digits_len);
    if (left) sink_pad(sink, ' ', pad);
}

static double dabs_local(double value) {
    return value < 0.0 ? -value : value;
}

static size_t format_fixed(double value, int precision, char *out, size_t cap) {
    size_t len = 0;
    unsigned long long whole;
    char tmp[64];
    size_t tmp_len;

    if (precision < 0) precision = 6;
    whole = (unsigned long long)value;
    tmp_len = utoa_rev(whole, 10, false, tmp);
    while (tmp_len > 0 && len + 1 < cap) out[len++] = tmp[--tmp_len];

    if (precision > 0 && len + 1 < cap) out[len++] = '.';

    value -= (double)whole;
    for (int i = 0; i < precision && len + 1 < cap; i++) {
        int digit;
        value *= 10.0;
        digit = (int)value;
        if (digit < 0) digit = 0;
        if (digit > 9) digit = 9;
        out[len++] = (char)('0' + digit);
        value -= (double)digit;
    }

    out[len] = '\0';
    return len;
}

static void trim_fraction(char *buf) {
    size_t len = strlen(buf);
    while (len > 0 && buf[len - 1] == '0') buf[--len] = '\0';
    if (len > 0 && buf[len - 1] == '.') buf[--len] = '\0';
}

static size_t format_scientific(double value, int precision, char *out, size_t cap) {
    int exponent = 0;
    size_t len;
    size_t exp_len = 0;
    char exp_tmp[16];

    if (precision < 0) precision = 6;
    if (value == 0.0) {
        len = format_fixed(0.0, precision > 0 ? precision - 1 : 0, out, cap);
        trim_fraction(out);
        len = strlen(out);
        if (len + 4 < cap) {
            out[len++] = 'e';
            out[len++] = '+';
            out[len++] = '0';
            out[len++] = '0';
            out[len] = '\0';
        }
        return len;
    }

    while (value >= 10.0) {
        value /= 10.0;
        exponent++;
    }
    while (value > 0.0 && value < 1.0) {
        value *= 10.0;
        exponent--;
    }

    len = format_fixed(value, precision > 0 ? precision - 1 : 0, out, cap);
    trim_fraction(out);
    len = strlen(out);
    if (len + 2 >= cap) return len;
    out[len++] = 'e';
    out[len++] = exponent < 0 ? '-' : '+';

    exp_len = utoa_rev((unsigned long long)(exponent < 0 ? -exponent : exponent), 10, false, exp_tmp);
    if (exp_len < 2 && len + 1 < cap) out[len++] = '0';
    while (exp_len > 0 && len + 1 < cap) out[len++] = exp_tmp[--exp_len];
    out[len] = '\0';
    return len;
}

static size_t format_general(double value, int precision, char *out, size_t cap) {
    double abs_value = dabs_local(value);
    int exponent = 0;
    int frac_precision;

    if (precision <= 0) precision = 6;
    if (abs_value != 0.0) {
        while (abs_value >= 10.0) {
            abs_value /= 10.0;
            exponent++;
        }
        while (abs_value < 1.0) {
            abs_value *= 10.0;
            exponent--;
        }
    }

    if (value != 0.0 && (exponent < -4 || exponent >= precision)) {
        return format_scientific(dabs_local(value), precision, out, cap);
    }

    frac_precision = precision - (exponent + 1);
    if (frac_precision < 0) frac_precision = 0;
    format_fixed(dabs_local(value), frac_precision, out, cap);
    trim_fraction(out);
    return strlen(out);
}

static int parse_unsigned(const char **fmt) {
    int value = 0;
    while (**fmt >= '0' && **fmt <= '9') {
        value = value * 10 + (**fmt - '0');
        (*fmt)++;
    }
    return value;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    FormatSink sink;
    va_list args;

    sink.buf = buf;
    sink.cap = size;
    sink.len = 0;
    va_copy(args, ap);

    while (*fmt) {
        char numbuf[128];
        char rev[64];
        size_t num_len = 0;
        bool negative = false;
        bool left = false;
        bool zero_pad = false;
        bool upper = false;
        int width = 0;
        int precision = -1;
        int length = 0;

        if (*fmt != '%') {
            sink_putc(&sink, *fmt++);
            continue;
        }

        fmt++;
        if (*fmt == '%') {
            sink_putc(&sink, *fmt++);
            continue;
        }

        for (;;) {
            if (*fmt == '-') {
                left = true;
                fmt++;
            } else if (*fmt == '0') {
                zero_pad = true;
                fmt++;
            } else {
                break;
            }
        }

        if (*fmt == '*') {
            width = va_arg(args, int);
            if (width < 0) {
                left = true;
                width = -width;
            }
            fmt++;
        } else if (isdigit((unsigned char)*fmt)) {
            width = parse_unsigned(&fmt);
        }

        if (*fmt == '.') {
            fmt++;
            if (*fmt == '*') {
                precision = va_arg(args, int);
                fmt++;
            } else {
                precision = parse_unsigned(&fmt);
            }
            if (precision < 0) precision = 0;
            zero_pad = false;
        }

        if (*fmt == 'z') {
            length = 3;
            fmt++;
        } else if (*fmt == 'l') {
            length = 1;
            fmt++;
            if (*fmt == 'l') {
                length = 2;
                fmt++;
            }
        }

        switch (*fmt) {
            case 'c': {
                char ch = (char)va_arg(args, int);
                if (!left) sink_pad(&sink, ' ', width > 1 ? width - 1 : 0);
                sink_putc(&sink, ch);
                if (left) sink_pad(&sink, ' ', width > 1 ? width - 1 : 0);
                break;
            }
            case 's': {
                const char *str = va_arg(args, const char *);
                size_t len;
                if (!str) str = "(null)";
                len = strlen(str);
                if (precision >= 0 && (size_t)precision < len) len = (size_t)precision;
                if (!left) sink_pad(&sink, ' ', width > (int)len ? width - (int)len : 0);
                sink_write(&sink, str, len);
                if (left) sink_pad(&sink, ' ', width > (int)len ? width - (int)len : 0);
                break;
            }
            case 'd':
            case 'i': {
                long long value;
                unsigned long long mag;
                if (length == 2) value = va_arg(args, long long);
                else if (length == 1) value = va_arg(args, long);
                else if (length == 3) value = (long long)va_arg(args, ssize_t);
                else value = va_arg(args, int);
                negative = value < 0;
                mag = negative ? (unsigned long long)(-(value + 1)) + 1u : (unsigned long long)value;
                num_len = utoa_rev(mag, 10, false, rev);
                for (size_t i = 0; i < num_len; i++) numbuf[i] = rev[num_len - 1 - i];
                sink_number(&sink, "", numbuf, num_len, negative, width, precision, left, zero_pad);
                break;
            }
            case 'u':
            case 'x':
            case 'X':
            case 'o': {
                unsigned long long value;
                unsigned base = 10;
                if (*fmt == 'x' || *fmt == 'X') base = 16;
                else if (*fmt == 'o') base = 8;
                upper = (*fmt == 'X');
                if (length == 2) value = va_arg(args, unsigned long long);
                else if (length == 1) value = va_arg(args, unsigned long);
                else if (length == 3) value = (unsigned long long)va_arg(args, size_t);
                else value = va_arg(args, unsigned int);
                num_len = utoa_rev(value, base, upper, rev);
                for (size_t i = 0; i < num_len; i++) numbuf[i] = rev[num_len - 1 - i];
                sink_number(&sink, "", numbuf, num_len, false, width, precision, left, zero_pad);
                break;
            }
            case 'p': {
                uintptr_t value = (uintptr_t)va_arg(args, void *);
                num_len = utoa_rev((unsigned long long)value, 16, false, rev);
                for (size_t i = 0; i < num_len; i++) numbuf[i] = rev[num_len - 1 - i];
                sink_number(&sink, "0x", numbuf, num_len, false, width, precision, left, zero_pad);
                break;
            }
            case 'f':
            case 'g': {
                double value = va_arg(args, double);
                if (value < 0.0) {
                    negative = true;
                    value = -value;
                }
                if (*fmt == 'f') num_len = format_fixed(value, precision, numbuf, sizeof(numbuf));
                else num_len = format_general(value, precision, numbuf, sizeof(numbuf));
                sink_number(&sink, "", numbuf, num_len, negative, width, -1, left, zero_pad);
                break;
            }
            default:
                sink_putc(&sink, '%');
                sink_putc(&sink, *fmt);
                break;
        }

        if (*fmt) fmt++;
    }

    if (sink.cap != 0) {
        size_t term = sink.len < sink.cap ? sink.len : sink.cap - 1;
        sink.buf[term] = '\0';
    }

    va_end(args);
    return (int)sink.len;
}

int snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return rc;
}

int vfprintf(FILE *stream, const char *fmt, va_list ap) {
    va_list copy;
    int needed;
    char stack_buf[512];
    char *dyn = 0;
    char *out = stack_buf;

    va_copy(copy, ap);
    needed = vsnprintf(0, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) return needed;

    if ((size_t)needed + 1 > sizeof(stack_buf)) {
        dyn = (char *)malloc((size_t)needed + 1);
        if (!dyn) return EOF;
        out = dyn;
    }

    vsnprintf(out, (size_t)needed + 1, fmt, ap);
    if (write_stream(stream, out, (size_t)needed) < 0) {
        free(dyn);
        return EOF;
    }
    free(dyn);
    return needed;
}

int fprintf(FILE *stream, const char *fmt, ...) {
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = vfprintf(stream, fmt, ap);
    va_end(ap);
    return rc;
}

int printf(const char *fmt, ...) {
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = vfprintf(stdout, fmt, ap);
    va_end(ap);
    return rc;
}

static int is_leap_year(int year) {
    if ((year % 4) != 0) return 0;
    if ((year % 100) != 0) return 1;
    return (year % 400) == 0;
}

static long days_before_month(int year, int month) {
    static const int days[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    long total = 0;
    for (int i = 1; i < month; i++) {
        total += days[i - 1];
        if (i == 2 && is_leap_year(year)) total++;
    }
    return total;
}

static time_t unix_from_calendar(const struct numos_calendar_time *ct) {
    long days = 0;
    int year;
    if (!ct || !ct->valid) return (time_t)(sys_uptime_ms() / 1000);
    for (year = 1970; year < (int)ct->year; year++) days += is_leap_year(year) ? 366 : 365;
    days += days_before_month((int)ct->year, (int)ct->month);
    days += (long)ct->day - 1;
    return (time_t)(days * 86400L + (long)ct->hour * 3600L + (long)ct->minute * 60L + (long)ct->second);
}

time_t time(time_t *out) {
    struct numos_calendar_time ct;
    time_t value;
    if (sys_time_read(&ct) < 0 || !ct.valid) value = (time_t)(sys_uptime_ms() / 1000);
    else value = unix_from_calendar(&ct);
    if (out) *out = value;
    return value;
}

int clock_gettime(int clock_id, struct timespec *ts) {
    uint64_t ms;
    if (!ts || clock_id != CLOCK_MONOTONIC) {
        errno = EINVAL;
        return -1;
    }
    ms = (uint64_t)sys_uptime_ms();
    ts->tv_sec = (long)(ms / 1000);
    ts->tv_nsec = (long)((ms % 1000) * 1000000UL);
    return 0;
}
