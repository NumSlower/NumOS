#include "syscalls.h"
#include "program_version.h"

/* ═══════════════════════════════════════════════════════════════════════════
 *  Constants
 * ═════════════════════════════════════════════════════════════════════════ */
#define MAX_FILE_SIZE   16384
#define MAX_LINES         512
#define LINE_CAP          256
#define PATH_BUF_SIZE     128
#define CMDLINE_BUF_SIZE  128
#define MAX_SCREEN_COLS    240
#define MIN_SCREEN_COLS     20
#define SCREEN_COLS_DEFAULT 80
#define GUTTER_WIDTH        5   /* "  42 " 4 digits plus trailing space */
/* SYS_FB_INFO field indices.
 * Field 0 returns framebuffer width in pixels.
 * Field 1 returns framebuffer height in pixels.
 * FONT_W and FONT_H are the character cell size used by the OS text renderer. */
#define FB_FIELD_WIDTH  0
#define FB_FIELD_HEIGHT 1
#define FONT_W          8
#define FONT_H         16
/* Filled in once at startup by init_screen_size().
 * Safe fallback of 30 is used if sys_fb_info returns a bad value.          */
static int SCREEN_COLS = SCREEN_COLS_DEFAULT;
static int SCREEN_ROWS = 30;
static int EDIT_ROWS   = 28;
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
    EDIT_ROWS = SCREEN_ROWS - 2;
    if (EDIT_ROWS < 1) EDIT_ROWS = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Color helpers
 * ═════════════════════════════════════════════════════════════════════════ */
#define RGB(r,g,b) \
    ((uint32_t)(((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b)))

/* ═══════════════════════════════════════════════════════════════════════════
 *  Color theme  (mirrors SlowerText's EditorConfig color fields)
 * ═════════════════════════════════════════════════════════════════════════ */
struct theme {
    uint32_t text_fg;       /* normal text foreground        */
    uint32_t text_bg;       /* editor background             */
    uint32_t linenum_fg;    /* gutter line-number text       */
    uint32_t curline_bg;    /* current-line highlight bg     */
    uint32_t status_fg;     /* status bar text               */
    uint32_t status_bg;     /* status bar background         */
    uint32_t comment_fg;    /* comment / # line foreground   */
};

/* Built-in themes — same palettes as SlowerText */
static const struct theme BUILTIN_THEMES[5] = {
    /* dark */
    { RGB(0xCD,0xD6,0xF4), RGB(0x1E,0x1E,0x2E),
      RGB(0x45,0x47,0x5A), RGB(0x31,0x32,0x44),
      RGB(0xCD,0xD6,0xF4), RGB(0x31,0x32,0x44), RGB(0x6C,0x70,0x86) },
    /* light */
    { RGB(0x1E,0x1E,0x2E), RGB(0xEF,0xF1,0xF5),
      RGB(0x9C,0xA0,0xB0), RGB(0xE6,0xE9,0xF0),
      RGB(0x1E,0x1E,0x2E), RGB(0xCC,0xD0,0xDA), RGB(0x6C,0x6F,0x85) },
    /* ocean */
    { RGB(0xCD,0xD6,0xF4), RGB(0x1E,0x1E,0x2E),
      RGB(0x45,0x47,0x5A), RGB(0x31,0x32,0x44),
      RGB(0x89,0xB4,0xFA), RGB(0x31,0x32,0x44), RGB(0x6C,0x70,0x86) },
    /* forest */
    { RGB(0xD4,0xBE,0x98), RGB(0x28,0x28,0x28),
      RGB(0x50,0x49,0x45), RGB(0x32,0x30,0x2F),
      RGB(0xA9,0xB6,0x65), RGB(0x3C,0x38,0x36), RGB(0x92,0x83,0x74) },
    /* neon */
    { RGB(0xF8,0xF8,0xF2), RGB(0x0D,0x0D,0x0D),
      RGB(0x44,0x47,0x5A), RGB(0x16,0x21,0x3E),
      RGB(0xFF,0x79,0xC6), RGB(0x1A,0x1A,0x2E), RGB(0x62,0x72,0xA4) },
};
static const char *THEME_NAMES[5] = { "dark", "light", "ocean", "forest", "neon" };

/* ═══════════════════════════════════════════════════════════════════════════
 *  Per-session settings  (mirrors SlowerText's EditorConfig booleans)
 * ═════════════════════════════════════════════════════════════════════════ */
struct settings {
    int line_numbers;   /* show gutter with line numbers     */
    int syntax_hl;      /* highlight comment lines           */
    int curline_hl;     /* highlight the cursor's row        */
    int auto_indent;    /* copy leading whitespace on Enter  */
    int tab_width;      /* spaces inserted for Tab key       */
};

/* ═══════════════════════════════════════════════════════════════════════════
 *  Line buffer
 * ═════════════════════════════════════════════════════════════════════════ */
struct line {
    char data[LINE_CAP];
    int  len;
};

/* ═══════════════════════════════════════════════════════════════════════════
 *  Global editor state
 * ═════════════════════════════════════════════════════════════════════════ */
static struct line     lines[MAX_LINES];
static int             line_count  = 1;
static int             cursor_row  = 0;
static int             cursor_col  = 0;
static int             top_line    = 0;
static int             insert_mode = 1;
static int             cmd_active  = 0;
static char            cmd_buf[64];
static int             cmd_len     = 0;
static char            status_msg[MAX_SCREEN_COLS + 1];
static int             file_modified = 0;

static struct theme    cur_theme;
static struct settings cfg = { 1, 1, 1, 1, 4 };

/* ═══════════════════════════════════════════════════════════════════════════
 *  String / memory utilities
 * ═════════════════════════════════════════════════════════════════════════ */
static size_t str_len(const char *s) {
    size_t n = 0; while (s[n]) n++; return n;
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b) { if (*a++ != *b++) return 0; }
    return *a == '\0' && *b == '\0';
}

static int str_starts(const char *s, const char *pfx) {
    while (*pfx) { if (*s++ != *pfx++) return 0; }
    return 1;
}

static void mem_copy(char *d, const char *s, size_t n) {
    while (n--) *d++ = *s++;
}

static void mem_move(char *dst, const char *src, size_t n) {
    if (!n || dst == src) return;
    if (dst < src) { while (n--) *dst++ = *src++; return; }
    dst += n; src += n; while (n--) *--dst = *--src;
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

/* Write `n` right-justified into a field of `width` chars. */
static void fmt_right(int n, char *buf, int width) {
    for (int i = width - 1; i >= 0; i--) {
        if (n > 0 || i == width - 1) { buf[i] = '0' + n % 10; n /= 10; }
        else buf[i] = ' ';
    }
}

/* Convert int → NUL-terminated string; returns char count. */
static int int_to_str(int n, char *buf) {
    if (n <= 0) { buf[0] = '0'; buf[1] = '\0'; return 1; }
    char tmp[12]; int len = 0, m = n;
    while (m > 0) { tmp[len++] = '0' + m % 10; m /= 10; }
    for (int i = 0; i < len; i++) buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Color parsing  (#rrggbb  or  named)
 * ═════════════════════════════════════════════════════════════════════════ */
static int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int parse_hex_color(const char *s, uint32_t *out) {
    if (s[0] != '#' || str_len(s) != 7) return -1;
    int r0 = hex_digit(s[1]), r1 = hex_digit(s[2]);
    int g0 = hex_digit(s[3]), g1 = hex_digit(s[4]);
    int b0 = hex_digit(s[5]), b1 = hex_digit(s[6]);
    if (r0<0||r1<0||g0<0||g1<0||b0<0||b1<0) return -1;
    *out = RGB((r0<<4)|r1, (g0<<4)|g1, (b0<<4)|b1);
    return 0;
}

static int parse_named_color(const char *s, uint32_t *out) {
    if (str_eq(s,"black"))   { *out = RGB(0x00,0x00,0x00); return 0; }
    if (str_eq(s,"red"))     { *out = RGB(0xCC,0x00,0x00); return 0; }
    if (str_eq(s,"green"))   { *out = RGB(0x00,0xAA,0x00); return 0; }
    if (str_eq(s,"yellow"))  { *out = RGB(0xCC,0xCC,0x00); return 0; }
    if (str_eq(s,"blue"))    { *out = RGB(0x00,0x00,0xCC); return 0; }
    if (str_eq(s,"magenta")) { *out = RGB(0xCC,0x00,0xCC); return 0; }
    if (str_eq(s,"cyan"))    { *out = RGB(0x00,0xCC,0xCC); return 0; }
    if (str_eq(s,"white"))   { *out = RGB(0xCC,0xCC,0xCC); return 0; }
    return -1;
}

static int parse_color(const char *s, uint32_t *out) {
    if (s[0] == '#') return parse_hex_color(s, out);
    return parse_named_color(s, out);
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  File I/O
 * ═════════════════════════════════════════════════════════════════════════ */
static int load_file(const char *path, char *buf, size_t cap, size_t *out_len) {
    int fd = (int)sys_open(path, FAT32_O_RDONLY, 0);
    if (fd < 0) return -1;
    size_t len = 0;
    while (len < cap) {
        int64_t got = sys_read(fd, buf + len, cap - len);
        if (got <= 0) break;
        len += (size_t)got;
    }
    if (len == cap) {
        char extra = 0;
        if (sys_read(fd, &extra, 1) > 0) { sys_close(fd); return -2; }
    }
    sys_close(fd);
    *out_len = len;
    return 0;
}

static int save_file(const char *path, const char *buf, size_t len) {
    int fd = (int)sys_open(path, FAT32_O_WRONLY | FAT32_O_TRUNC | FAT32_O_CREAT, 0);
    if (fd < 0) return -1;
    size_t written = 0;
    while (written < len) {
        int64_t got = sys_write(fd, buf + written, len - written);
        if (got <= 0) { sys_close(fd); return -1; }
        written += (size_t)got;
    }
    sys_close(fd);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Line buffer operations
 * ═════════════════════════════════════════════════════════════════════════ */
static void clear_lines(void) {
    for (int i = 0; i < MAX_LINES; i++) lines[i].len = 0;
    line_count = 1; cursor_row = 0; cursor_col = 0; top_line = 0;
}

static void load_into_lines(const char *buf, size_t len) {
    clear_lines();
    int row = 0;
    for (size_t i = 0; i < len; i++) {
        char c = buf[i];
        if (c == '\n') { if (row + 1 < MAX_LINES) row++; continue; }
        if (lines[row].len + 1 < LINE_CAP) lines[row].data[lines[row].len++] = c;
    }
    line_count = row + 1;
}

static int build_file_buf(char *out, size_t cap, size_t *out_len) {
    size_t pos = 0;
    for (int i = 0; i < line_count; i++) {
        if (pos + (size_t)lines[i].len > cap) return -1;
        mem_copy(out + pos, lines[i].data, (size_t)lines[i].len);
        pos += (size_t)lines[i].len;
        if (i + 1 < line_count) {
            if (pos + 1 > cap) return -1;
            out[pos++] = '\n';
        }
    }
    *out_len = pos;
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Cursor and scroll
 * ═════════════════════════════════════════════════════════════════════════ */
static void clamp_cursor(void) {
    if (cursor_row < 0) cursor_row = 0;
    if (cursor_row >= line_count) cursor_row = line_count - 1;
    int len = lines[cursor_row].len;
    if (cursor_col < 0) cursor_col = 0;
    if (cursor_col > len) cursor_col = len;
}

static void update_scroll(void) {
    if (cursor_row < top_line) top_line = cursor_row;
    if (cursor_row >= top_line + EDIT_ROWS) top_line = cursor_row - (EDIT_ROWS - 1);
    if (top_line < 0) top_line = 0;
}

static int visual_col(const struct line *ln, int idx) {
    int col = 0;
    for (int i = 0; i < idx && i < ln->len; i++) {
        col += (ln->data[i] == '\t') ? (cfg.tab_width - (col % cfg.tab_width)) : 1;
        if (col >= SCREEN_COLS) break;
    }
    return col < 0 ? 0 : col;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Rendering
 * ═════════════════════════════════════════════════════════════════════════ */

/* Draw one content row using a SINGLE sys_write call so that the background
 * color set by sys_fb_setcolor covers every character — including the spaces
 * that pad the line to the full SCREEN_COLS width.  Splitting into two writes
 * (gutter + content) caused the OS renderer to only paint the background for
 * the first segment, leaving the row background uncovered past it.         */
static void render_line(int row_index) {
    int file_row = top_line + row_index;
    int is_cur   = cfg.curline_hl && (file_row == cursor_row);
    uint32_t bg  = is_cur ? cur_theme.curline_bg : cur_theme.text_bg;
    uint32_t fg  = cur_theme.text_fg;

    /* Build one complete SCREEN_COLS-wide row: [gutter][content][\n] */
    char out[MAX_SCREEN_COLS + 2];
    for (int i = 0; i < SCREEN_COLS; i++) out[i] = ' ';
    out[SCREEN_COLS]     = '\n';
    out[SCREEN_COLS + 1] = '\0';

    int show_gutter = cfg.line_numbers && (SCREEN_COLS > (GUTTER_WIDTH + 1));

    /* ── gutter ─────────────────────────────────────────────────────────── */
    if (show_gutter) {
        if (file_row < line_count) {
            /* right-justify line number into columns 0-3, column 4 = space */
            fmt_right(file_row + 1, out, 4);
            out[4] = ' ';
        }
        /* else: gutter stays as 5 spaces (already filled above) */
    }

    int content_start = show_gutter ? GUTTER_WIDTH : 0;
    int content_end   = SCREEN_COLS;   /* exclusive */
    int content_w     = content_end - content_start;

    if (content_w <= 0) {
        sys_fb_setcolor(fg, bg);
        sys_write(FD_STDOUT, out, (size_t)(SCREEN_COLS + 1));
        return;
    }

    /* ── content ────────────────────────────────────────────────────────── */
    if (file_row < line_count) {
        struct line *ln = &lines[file_row];

        /* Syntax highlight: lines that start with # or // */
        if (cfg.syntax_hl && ln->len > 0 &&
            (ln->data[0] == '#' ||
             (ln->len > 1 && ln->data[0] == '/' && ln->data[1] == '/'))) {
            fg = cur_theme.comment_fg;
        }

        /* Expand line content (tab → spaces) into content area */
        int col = 0;
        for (int i = 0; i < ln->len && col < content_w; i++) {
            char c = ln->data[i];
            if (c == '\t') {
                int sp = cfg.tab_width - (col % cfg.tab_width);
                while (sp-- > 0 && col < content_w) out[content_start + col++] = ' ';
            } else {
                out[content_start + col++] = c;
            }
        }

        /* Cursor marker */
        if (file_row == cursor_row) {
            int vis = visual_col(ln, cursor_col);
            if (vis >= content_w) vis = content_w - 1;
            out[content_start + vis] = '_';
        }
    } else {
        /* Past EOF: keep the row blank */
    }

    /* One color set, one write — the entire row including spaces gets the bg */
    sys_fb_setcolor(fg, bg);
    sys_write(FD_STDOUT, out, (size_t)(SCREEN_COLS + 1));
}

/* Status bar (row SCREEN_ROWS-2):
 *   left  — mode  *filename  [:command-being-typed]
 *   right — row/total                                */
static void render_status_bar(const char *path) {
    char out[MAX_SCREEN_COLS + 2];
    for (int i = 0; i < SCREEN_COLS; i++) out[i] = ' ';
    out[SCREEN_COLS]     = '\n';
    out[SCREEN_COLS + 1] = '\0';

    sys_fb_setcolor(cur_theme.status_fg, cur_theme.status_bg);

    int pos = 0;

    /* mode badge */
    const char *mode = insert_mode ? "INS" : "CMD";
    while (*mode && pos < SCREEN_COLS) out[pos++] = *mode++;
    if (pos < SCREEN_COLS) out[pos++] = ' ';

    /* modified flag + filename */
    if (file_modified && pos < SCREEN_COLS) out[pos++] = '*';
    for (int i = 0; path[i] && pos < SCREEN_COLS; i++) out[pos++] = path[i];

    /* in-progress command prompt */
    if (cmd_active && pos + 2 < SCREEN_COLS) {
        out[pos++] = ' '; out[pos++] = ':';
        for (int i = 0; i < cmd_len && pos < SCREEN_COLS; i++)
            out[pos++] = cmd_buf[i];
    }

    /* right-aligned cursor position: "row/total " */
    char rpart[32]; int rlen = 0;
    rlen += int_to_str(cursor_row + 1, rpart + rlen);
    rpart[rlen++] = '/';
    rlen += int_to_str(line_count, rpart + rlen);
    rpart[rlen++] = ' ';
    rpart[rlen]   = '\0';

    int rstart = SCREEN_COLS - rlen;
    if (rstart > pos)
        for (int i = 0; i < rlen; i++) out[rstart + i] = rpart[i];

    sys_write(FD_STDOUT, out, (size_t)(SCREEN_COLS + 1));
}

/* Message bar (last row): shows status_msg, plain text on editor bg. */
static void render_msg_bar(void) {
    char out[MAX_SCREEN_COLS + 1];
    for (int i = 0; i < SCREEN_COLS; i++) out[i] = ' ';
    out[SCREEN_COLS] = '\0';

    sys_fb_setcolor(cur_theme.text_fg, cur_theme.text_bg);

    if (status_msg[0]) {
        int n = (int)str_len(status_msg);
        if (n > SCREEN_COLS) n = SCREEN_COLS;
        for (int i = 0; i < n; i++) out[i] = status_msg[i];
    }

    sys_write(FD_STDOUT, out, (size_t)SCREEN_COLS);
}

static void render_screen(const char *path) {
    sys_fb_setcolor(cur_theme.text_fg, cur_theme.text_bg);
    sys_fb_clear();
    for (int r = 0; r < EDIT_ROWS; r++) render_line(r);
    render_status_bar(path);
    render_msg_bar();
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Help text
 * ═════════════════════════════════════════════════════════════════════════ */
static void print_help(void) {
    write_str("usage:\n");
    write_str("  edit <file>\n");
    write_str("  edit -h\n\n");
    write_str("keys:\n");
    write_str("  ESC          command mode\n");
    write_str("  I            insert mode\n");
    write_str("  h j k l      move in command mode\n");
    write_str("  x            delete character at cursor\n");
    write_str("  Tab          insert spaces (tab_width)\n\n");
    write_str("commands:\n");
    write_str("  :w           save\n");
    write_str("  :q           quit (warns if unsaved)\n");
    write_str("  :q!          force quit\n");
    write_str("  :wq          save and quit\n");
    write_str("  :theme <n>   dark  light  ocean  forest  neon\n");
    write_str("  :color <target> <#rrggbb | name>\n");
    write_str("    targets:   text  bg  curline  linenum\n");
    write_str("               status  statusfg  comment\n");
    write_str("  :set linenumbers on|off\n");
    write_str("  :set syntax     on|off\n");
    write_str("  :set curline    on|off\n");
    write_str("  :set autoindent on|off\n");
    write_str("  :set tabwidth   <1-16>\n");
    write_str("  :h             show this help\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Editing operations
 * ═════════════════════════════════════════════════════════════════════════ */
static void insert_char(char c) {
    struct line *ln = &lines[cursor_row];
    if (ln->len + 1 >= LINE_CAP) return;
    if (cursor_col > ln->len) cursor_col = ln->len;
    mem_move(ln->data + cursor_col + 1, ln->data + cursor_col,
             (size_t)(ln->len - cursor_col));
    ln->data[cursor_col] = c;
    ln->len++;
    cursor_col++;
    file_modified = 1;
}

static void delete_char_at_cursor(void) {
    struct line *ln = &lines[cursor_row];
    if (cursor_col >= ln->len) return;
    mem_move(ln->data + cursor_col, ln->data + cursor_col + 1,
             (size_t)(ln->len - cursor_col - 1));
    ln->len--;
    file_modified = 1;
}

static void backspace_char(void) {
    struct line *ln = &lines[cursor_row];
    if (cursor_col > 0) {
        mem_move(ln->data + cursor_col - 1, ln->data + cursor_col,
                 (size_t)(ln->len - cursor_col));
        ln->len--; cursor_col--;
        file_modified = 1;
        return;
    }
    if (cursor_row == 0) return;
    struct line *prev = &lines[cursor_row - 1];
    if (prev->len + ln->len >= LINE_CAP) return;
    int old_len = prev->len;
    mem_copy(prev->data + prev->len, ln->data, (size_t)ln->len);
    prev->len += ln->len;
    for (int i = cursor_row; i < line_count - 1; i++) lines[i] = lines[i + 1];
    line_count--;
    cursor_row--;
    cursor_col = old_len;
    file_modified = 1;
}

static void insert_newline(void) {
    if (line_count + 1 > MAX_LINES) return;
    struct line *ln = &lines[cursor_row];
    struct line nl; nl.len = 0;

    if (cursor_col < ln->len) {
        int tail = ln->len - cursor_col;
        if (tail >= LINE_CAP) tail = LINE_CAP - 1;
        mem_copy(nl.data, ln->data + cursor_col, (size_t)tail);
        nl.len = tail; ln->len = cursor_col;
    }

    for (int i = line_count; i > cursor_row + 1; i--) lines[i] = lines[i - 1];
    lines[cursor_row + 1] = nl;
    line_count++;
    cursor_row++; cursor_col = 0;

    /* Auto-indent: copy leading whitespace from the previous line */
    if (cfg.auto_indent) {
        struct line *prev = &lines[cursor_row - 1];
        int indent = 0;
        while (indent < prev->len &&
               (prev->data[indent] == ' ' || prev->data[indent] == '\t'))
            indent++;
        if (indent > 0) {
            struct line *cur = &lines[cursor_row];
            if (indent + cur->len < LINE_CAP) {
                mem_move(cur->data + indent, cur->data, (size_t)cur->len);
                mem_copy(cur->data, prev->data, (size_t)indent);
                cur->len += indent;
                cursor_col = indent;
            }
        }
    }
    file_modified = 1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Command processing  (mirrors SlowerText's InputHandler::process_command)
 * ═════════════════════════════════════════════════════════════════════════ */
static void set_status(const char *s) {
    size_t n = str_len(s);
    if (n >= sizeof(status_msg)) n = sizeof(status_msg) - 1;
    mem_copy(status_msg, s, n);
    status_msg[n] = '\0';
}

static void do_save(const char *path) {
    static char file_buf[MAX_FILE_SIZE];
    size_t file_len = 0;
    if (build_file_buf(file_buf, sizeof(file_buf), &file_len) != 0) {
        set_status("save failed: buffer overflow");
        return;
    }
    if (save_file(path, file_buf, file_len) != 0) {
        set_status("save failed: write error");
        return;
    }
    file_modified = 0;
    set_status("saved");
}

/* :theme <name> */
static void exec_theme_cmd(const char *arg) {
    for (int i = 0; i < 5; i++) {
        if (str_eq(arg, THEME_NAMES[i])) {
            cur_theme = BUILTIN_THEMES[i];
            set_status("theme applied");
            return;
        }
    }
    set_status("unknown theme — try: dark  light  ocean  forest  neon");
}

/* :color <target> <#rrggbb | name> */
static void exec_color_cmd(const char *args) {
    char target[32], value[32];
    read_token(args, target, sizeof(target));
    const char *rest = skip_spaces(args + str_len(target));
    read_token(rest, value, sizeof(value));

    if (!value[0]) {
        set_status("usage: :color <target> <#rrggbb | name>");
        return;
    }
    uint32_t col = 0;
    if (parse_color(value, &col) != 0) {
        set_status("invalid color — use #rrggbb or: black red green yellow blue magenta cyan white");
        return;
    }

    if      (str_eq(target,"text")      || str_eq(target,"fg"))         cur_theme.text_fg    = col;
    else if (str_eq(target,"bg")        || str_eq(target,"background"))  cur_theme.text_bg    = col;
    else if (str_eq(target,"curline")   || str_eq(target,"current_line"))cur_theme.curline_bg = col;
    else if (str_eq(target,"linenum")   || str_eq(target,"line_number")) cur_theme.linenum_fg = col;
    else if (str_eq(target,"status")    || str_eq(target,"statusbar"))   cur_theme.status_bg  = col;
    else if (str_eq(target,"statusfg")  || str_eq(target,"status_text")) cur_theme.status_fg  = col;
    else if (str_eq(target,"comment"))                                    cur_theme.comment_fg = col;
    else {
        set_status("unknown target — try: text  bg  curline  linenum  status  statusfg  comment");
        return;
    }
    set_status("color updated");
}

/* :set <key> <value> */
static void exec_set_cmd(const char *args) {
    char key[32], val[32];
    read_token(args, key, sizeof(key));
    const char *rest = skip_spaces(args + str_len(key));
    read_token(rest, val, sizeof(val));

    int bval = str_eq(val,"on") || str_eq(val,"1") || str_eq(val,"true");

    if (str_eq(key,"linenumbers") || str_eq(key,"line_numbers")) {
        cfg.line_numbers = bval;
        set_status(bval ? "line numbers on" : "line numbers off");
    } else if (str_eq(key,"syntax")) {
        cfg.syntax_hl = bval;
        set_status(bval ? "syntax highlighting on" : "syntax highlighting off");
    } else if (str_eq(key,"curline") || str_eq(key,"highlight_current_line")) {
        cfg.curline_hl = bval;
        set_status(bval ? "current line highlight on" : "current line highlight off");
    } else if (str_eq(key,"autoindent") || str_eq(key,"auto_indent")) {
        cfg.auto_indent = bval;
        set_status(bval ? "auto-indent on" : "auto-indent off");
    } else if (str_eq(key,"tabwidth") || str_eq(key,"tab_width")) {
        int n = 0;
        for (int i = 0; val[i] >= '0' && val[i] <= '9'; i++) n = n*10 + (val[i]-'0');
        if (n >= 1 && n <= 16) { cfg.tab_width = n; set_status("tab width updated"); }
        else set_status("tabwidth must be 1-16");
    } else {
        set_status("unknown setting — try: linenumbers  syntax  curline  autoindent  tabwidth");
    }
}

static void exec_command(const char *cmd, const char *path, int *quit) {
    /* ── File operations ── */
    if (str_eq(cmd,"w"))  { do_save(path); return; }

    if (str_eq(cmd,"q")) {
        if (file_modified) {
            set_status("unsaved changes — :q! to force, :w to save");
            return;
        }
        *quit = 1; return;
    }

    if (str_eq(cmd,"q!")) { *quit = 1; return; }

    if (str_eq(cmd,"wq")) {
        do_save(path);
        if (!file_modified) *quit = 1;
        return;
    }

    /* ── Theme ── */
    if (str_starts(cmd,"theme")) {
        const char *arg = skip_spaces(cmd + 5);
        if (!arg[0]) { set_status("themes: dark  light  ocean  forest  neon"); return; }
        exec_theme_cmd(arg); return;
    }

    /* ── Per-color override ── */
    if (str_starts(cmd,"color ")) {
        exec_color_cmd(skip_spaces(cmd + 6)); return;
    }

    /* ── Settings ── */
    if (str_starts(cmd,"set ")) {
        exec_set_cmd(skip_spaces(cmd + 4)); return;
    }

    /* ── Palette info ── */
    if (str_eq(cmd,"colors")) {
        set_status("use :color <target> <#rrggbb> to change colors");
        return;
    }

    /* ── Help ── */
    if (str_eq(cmd,"h") || str_eq(cmd,"help")) {
        set_status("ESC=cmd  I=ins  hjkl=move  :w :q :wq :theme :color :set :h");
        return;
    }

    set_status("unknown command — :h for help");
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Key handlers
 * ═════════════════════════════════════════════════════════════════════════ */
static void handle_insert_key(char c) {
    if (c == 0x1B) {
        insert_mode = 0;
        set_status("-- COMMAND --");
        return;
    }
    if (c == '\r' || c == '\n') { insert_newline(); return; }
    if (c == '\b' || c == 0x7F) { backspace_char(); return; }
    if (c == '\t') {
        /* Expand tab to the next tab-stop (like SlowerText's do_tab) */
        int vis = visual_col(&lines[cursor_row], cursor_col);
        int sp  = cfg.tab_width - (vis % cfg.tab_width);
        while (sp-- > 0) insert_char(' ');
        return;
    }
    if (c >= 32) insert_char(c);
}

static void handle_command_key(char c, const char *path, int *quit) {
    /* ── Typing into an active : command ── */
    if (cmd_active) {
        if (c == 0x1B) {
            cmd_active = 0; cmd_len = 0; cmd_buf[0] = '\0';
            set_status("-- COMMAND --");
            return;
        }
        if (c == '\r' || c == '\n') {
            cmd_buf[cmd_len] = '\0';
            cmd_active = 0; cmd_len = 0;
            exec_command(cmd_buf, path, quit);
            return;
        }
        if ((c == '\b' || c == 0x7F) && cmd_len > 0) {
            cmd_buf[--cmd_len] = '\0';
            return;
        }
        if (c >= 32 && cmd_len + 1 < (int)sizeof(cmd_buf)) {
            cmd_buf[cmd_len++] = c; cmd_buf[cmd_len] = '\0';
        }
        return;
    }

    if (c == 0x1B) return;

    if (c == 'I' || c == 'i') { insert_mode = 1; set_status("-- INSERT --"); return; }
    if (c == ':') { cmd_active = 1; cmd_len = 0; cmd_buf[0] = '\0'; return; }

    /* Vim-style hjkl navigation */
    if (c == 'h') { if (cursor_col > 0) cursor_col--;                        return; }
    if (c == 'l') { if (cursor_col < lines[cursor_row].len) cursor_col++;     return; }
    if (c == 'k') { if (cursor_row > 0) { cursor_row--; clamp_cursor(); }     return; }
    if (c == 'j') { if (cursor_row+1 < line_count){cursor_row++;clamp_cursor();} return;}
    if (c == 'x') { delete_char_at_cursor(); return; }
}

/* ═══════════════════════════════════════════════════════════════════════════
 *  Entry point
 * ═════════════════════════════════════════════════════════════════════════ */
int main(void) {
    char cmdline[CMDLINE_BUF_SIZE];
    char path[PATH_BUF_SIZE];
    static char file_buf[MAX_FILE_SIZE];
    size_t file_len = 0;

    /* Query the real framebuffer height so SCREEN_ROWS fills the display. */
    init_screen_size();

    /* Default theme: dark (same as SlowerText's default) */
    cur_theme = BUILTIN_THEMES[0];

    cmdline[0] = '\0';
    sys_get_cmdline(cmdline, sizeof(cmdline));

    const char *s = skip_spaces(cmdline);
    if (s[0] != '\0') {
        read_token(s, path, sizeof(path));
        if (numos_is_version_flag(path)) { numos_print_program_version("edit"); return 0; }
        if (str_eq(path,"-h") || str_eq(path,"-help")) { print_help(); return 0; }
    } else {
        /* Interactive filename prompt */
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
            if (pos + 1 < (int)sizeof(path)) { path[pos++] = c; sys_write(FD_STDOUT, &c, 1); }
        }
    }

    if (!path[0]) { write_str("no file path\n"); return 1; }

    int rc = load_file(path, file_buf, sizeof(file_buf), &file_len);
    if (rc == -1) {
        /* New file */
        int fd = (int)sys_open(path, FAT32_O_CREAT | FAT32_O_RDWR, 0);
        if (fd < 0) { write_str("open failed\n"); return 1; }
        sys_close(fd);
        file_len = 0;
        set_status("New file — :theme dark|light|ocean|forest|neon  :h for help");
    } else if (rc == -2) {
        write_str("file too large\n"); return 1;
    } else {
        set_status(":theme dark|light|ocean|forest|neon   :color <target> <#rgb>   :h help");
    }

    load_into_lines(file_buf, file_len);

    int quit = 0;
    while (!quit) {
        clamp_cursor();
        update_scroll();
        render_screen(path);

        char c = '\0';
        if (sys_input(&c, 1) <= 0) continue;

        if (insert_mode) handle_insert_key(c);
        else             handle_command_key(c, path, &quit);
    }

    sys_fb_clear();
    return 0;
}
