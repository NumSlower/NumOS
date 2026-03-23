#include "syscalls.h"

#define MAX_SCREEN_COLS    240
#define MIN_SCREEN_COLS     20
#define SCREEN_COLS_DEFAULT 80
#define SCREEN_ROWS_DEFAULT 25
#define PATH_BUF_SIZE     128
#define CMDLINE_BUF_SIZE  128
#define STREAM_BUF_SIZE   512
#define FB_FIELD_WIDTH      0
#define FB_FIELD_HEIGHT     1
#define FONT_W              8
#define FONT_H             16

static int SCREEN_COLS = SCREEN_COLS_DEFAULT;
static int SCREEN_ROWS = SCREEN_ROWS_DEFAULT;

static void init_screen_size(void) {
    int64_t px_w = sys_fb_info(FB_FIELD_WIDTH);
    int64_t px_h = sys_fb_info(FB_FIELD_HEIGHT);
    if (px_w > 32 && px_w < 8192) {
        int cols = (int)((uint64_t)px_w / FONT_W);
        if (cols > 10) SCREEN_COLS = cols;
    }
    if (px_h > 32 && px_h < 4096) {
        int rows = (int)((uint64_t)px_h / FONT_H);
        if (rows > 4) SCREEN_ROWS = rows;
    }
    if (SCREEN_COLS < MIN_SCREEN_COLS) SCREEN_COLS = MIN_SCREEN_COLS;
    if (SCREEN_COLS > MAX_SCREEN_COLS) SCREEN_COLS = MAX_SCREEN_COLS;
    if (SCREEN_ROWS < 5) SCREEN_ROWS = 5;
}

/* ── string helpers ─────────────────────────────────────────────────────── */
static size_t str_len(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == '\0' && *b == '\0';
}

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, str_len(s));
}

static const char *skip_spaces(const char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void read_token(const char *s, char *out, size_t cap) {
    size_t i = 0;
    while (s[i] && s[i] != ' ' && s[i] != '\t') {
        if (i + 1 < cap) out[i] = s[i];
        i++;
    }
    out[(i < cap) ? i : cap - 1] = '\0';
}

/* ── file access ────────────────────────────────────────────────────────── */
static int open_file(const char *path) {
    int fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    return fd;
}

static int print_file_streaming(const char *path) {
    char buf[STREAM_BUF_SIZE];
    int fd = open_file(path);

    if (fd < 0) return -1;

    while (1) {
        int64_t got = sys_read(fd, buf, sizeof(buf));
        if (got < 0) {
            sys_close(fd);
            return -1;
        }
        if (got == 0) break;
        sys_write(FD_STDOUT, buf, (size_t)got);
    }

    sys_close(fd);
    return 0;
}

/* ── print help ─────────────────────────────────────────────────────────── */
static void print_help(void) {
    write_str("usage:\n");
    write_str("  empty <file>\n");
    write_str("  empty -h\n\n");
    write_str("prints file contents up to screen height.\n");
}

/* ── entry point ────────────────────────────────────────────────────────── */
int main(void) {
    char cmdline[CMDLINE_BUF_SIZE];
    char path[PATH_BUF_SIZE];

    init_screen_size();

    cmdline[0] = '\0';
    sys_get_cmdline(cmdline, sizeof(cmdline));

    const char *s = skip_spaces(cmdline);
    if (s[0] != '\0') {
        read_token(s, path, sizeof(path));
        if (str_eq(path, "-h") || str_eq(path, "-help")) {
            print_help();
            return 0;
        }
    } else {
        /* No argument — prompt for filename */
        write_str("file: ");
        int pos = 0;
        for (;;) {
            char c = '\0';
            if (sys_input(&c, 1) <= 0) continue;
            if (c == '\r') c = '\n';
            if (c == '\n') { path[pos] = '\0'; write_str("\n"); break; }
            if ((c == '\b' || c == 0x7F) && pos > 0) {
                pos--; write_str("\b \b"); continue;
            }
            if (pos + 1 < (int)sizeof(path)) {
                path[pos++] = c;
                sys_write(FD_STDOUT, &c, 1);
            }
        }
    }

    if (!path[0]) { write_str("no file path\n"); return 1; }

    if (print_file_streaming(path) != 0) {
        write_str("empty: cannot open file\n");
        return 1;
    }

    return 0;
}
