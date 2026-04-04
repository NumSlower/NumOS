/*
 * framebuffer.c - Generic framebuffer renderer and console
 *
 * Backend specific probing and mode selection live under drivers/graphices.
 * This file only knows how to map an already chosen linear framebuffer and
 * render text and pixels into it.
 */

#include "drivers/framebuffer.h"
#include "drivers/graphices/vga.h"
#include "drivers/device.h"
#include "drivers/font.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"

/* Text rendering uses the PSF bitmap font loader in font.c. */

/* =========================================================================
 * Module state
 * ======================================================================= */
static uint8_t  *fb_mem   = NULL;
static int       fb_ready = 0;
static uint64_t  fb_phys  = 0;
static int       fb_width = FB_WIDTH;
static int       fb_height = FB_HEIGHT;
static int       fb_pitch = FB_WIDTH * 4;
static int       fb_bpp = FB_BPP;
static int       fb_bytespp = 4;
static uint8_t   fb_r_pos = 16;
static uint8_t   fb_g_pos = 8;
static uint8_t   fb_b_pos = 0;
static uint8_t   fb_r_size = 8;
static uint8_t   fb_g_size = 8;
static uint8_t   fb_b_size = 8;

static int fb_console_scale_pref(void) {
    int scale = FB_SCALE_PREFERRED;

    const struct hypervisor_info *hv = device_get_hypervisor();
    if (!hv) return scale;

    if (hv->id == HYPERVISOR_VIRTUALBOX) {
        return NUMOS_FB_SCALE_VBOX;
    }

    if (hv->id == HYPERVISOR_QEMU || hv->id == HYPERVISOR_KVM) {
        return NUMOS_FB_SCALE_QEMU;
    }

    return scale;
}

static inline uint8_t *fb_row_bytes(int y) {
    return fb_mem + (size_t)y * (size_t)fb_pitch;
}

static inline uint32_t fb_mask_for_size(uint8_t size) {
    if (size >= 32) return 0xFFFFFFFFu;
    if (size == 0) return 0;
    return (1u << size) - 1u;
}

static inline uint32_t fb_scale_channel(uint32_t v, uint8_t size) {
    if (size >= 8) return v << (size - 8);
    return v >> (8 - size);
}

static inline uint32_t fb_pack_color(uint32_t c) {
    uint32_t r = (c >> 16) & 0xFF;
    uint32_t g = (c >> 8) & 0xFF;
    uint32_t b = c & 0xFF;
    uint32_t rmask = fb_mask_for_size(fb_r_size);
    uint32_t gmask = fb_mask_for_size(fb_g_size);
    uint32_t bmask = fb_mask_for_size(fb_b_size);
    r = fb_scale_channel(r, fb_r_size) & rmask;
    g = fb_scale_channel(g, fb_g_size) & gmask;
    b = fb_scale_channel(b, fb_b_size) & bmask;
    return (r << fb_r_pos) | (g << fb_g_pos) | (b << fb_b_pos);
}

static inline int fb_native_32(void) {
    return fb_bytespp == 4 &&
           fb_r_pos == 16 && fb_g_pos == 8 && fb_b_pos == 0 &&
           fb_r_size == 8 && fb_g_size == 8 && fb_b_size == 8;
}

static inline void fb_write_pixel_raw(uint8_t *p, uint32_t c) {
    for (int i = 0; i < fb_bytespp; i++) {
        p[i] = (uint8_t)((c >> (i * 8)) & 0xFF);
    }
}

static inline void fb_console_wait_for_input(void) {
#if defined(__aarch64__)
    __asm__ volatile("wfi" ::: "memory");
#elif defined(__x86_64__)
    __asm__ volatile("sti; hlt; cli" ::: "memory");
#else
    __asm__ volatile("" ::: "memory");
#endif
}

/* Console */
static int     con_x0, con_y0, con_w, con_h;
static int     con_cx, con_cy, con_cols, con_rows, con_scale;
static uint32_t con_fg, con_bg, con_fill_bg;

/* Console scrollback */
#define FB_CON_SCROLLBACK_LINES 4096

struct fb_con_cell {
    char     ch;
    uint32_t fg;
    uint32_t bg;
};

static struct fb_con_cell *con_screen    = NULL;
static struct fb_con_cell *con_scrollback = NULL;
static size_t              con_saved_lines = 0;
static int                 con_scroll_offset = 0;
static int                 con_scroll_mode_active = 0;

/* =========================================================================
 * Initialisation
 * ======================================================================= */
static int fb_supported_bpp(int bpp) {
    return bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
}

static void fb_set_layout_defaults(int bpp) {
    if (bpp == 15) {
        fb_r_pos = 10; fb_r_size = 5;
        fb_g_pos = 5;  fb_g_size = 5;
        fb_b_pos = 0;  fb_b_size = 5;
    } else if (bpp == 16) {
        fb_r_pos = 11; fb_r_size = 5;
        fb_g_pos = 5;  fb_g_size = 6;
        fb_b_pos = 0;  fb_b_size = 5;
    } else {
        fb_r_pos = 16; fb_r_size = 8;
        fb_g_pos = 8;  fb_g_size = 8;
        fb_b_pos = 0;  fb_b_size = 8;
    }
}

void fb_reset(void) {
    fb_ready = 0;
    fb_mem = NULL;
    fb_phys = 0;
    fb_width = FB_WIDTH;
    fb_height = FB_HEIGHT;
    fb_pitch = FB_WIDTH * 4;
    fb_bpp = FB_BPP;
    fb_bytespp = 4;
    fb_set_layout_defaults(32);
    vga_set_output_hook(NULL);
}

static int fb_activate_mapped_mode(uint64_t phys, int width, int height,
                                   int bpp, int pitch) {
    fb_phys = phys;
    fb_width = width;
    fb_height = height;
    fb_bpp = bpp;
    fb_bytespp = (bpp + 7) / 8;
    fb_pitch = pitch;

    size_t fb_bytes = (size_t)fb_pitch * (size_t)fb_height;
    size_t pages = (fb_bytes + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        uint64_t a = (uint64_t)fb_phys + i * PAGE_SIZE;
        paging_map_page(a, a, PAGE_PRESENT | PAGE_WRITABLE);
    }

    fb_mem   = (uint8_t *)(uintptr_t)fb_phys;
    fb_ready = 1;
    fb_fill(FB_TERM_BG);

#if NUMOS_FB_TEST_PATTERN
    fb_test_pattern();
#endif

    int fw = font_char_width();
    int fh = font_char_height();
    if (fw < 1) fw = 8;
    if (fh < 1) fh = 16;

    int scale_pref = fb_console_scale_pref();
    if (scale_pref < FB_SCALE_1) scale_pref = FB_SCALE_1;
    if (scale_pref > FB_SCALE_3) scale_pref = FB_SCALE_3;

    int scale = scale_pref;
    for (int s = scale_pref; s >= FB_SCALE_1; s--) {
        int cols = fb_width  / (fw * s);
        int rows = fb_height / (fh * s);
        if (cols >= 80 && rows >= 25) { scale = s; break; }
    }

    int cw   = fw * scale;
    int ch   = fh * scale;
    int cols = fb_width  / cw;
    int rows = fb_height / ch;
    int wpx  = cols * cw;
    int hpx  = rows * ch;
    int x0   = (fb_width  - wpx) / 2;
    int y0   = (fb_height - hpx) / 2;

    fb_con_init(x0, y0, wpx, hpx, FB_TERM_FG, FB_TERM_BG, scale);
    fb_con_clear();
    vga_set_output_hook(fb_con_putchar);

    vga_writestring("FB: MB2 addr=0x");
    print_hex(fb_phys);
    vga_writestring(" pitch=");
    print_dec((uint64_t)fb_pitch);
    vga_writestring(" bpp=");
    print_dec((uint64_t)fb_bpp);
    vga_writestring(" rgb=");
    print_dec((uint64_t)fb_r_pos); vga_putchar(',');
    print_dec((uint64_t)fb_r_size); vga_putchar(' ');
    print_dec((uint64_t)fb_g_pos); vga_putchar(',');
    print_dec((uint64_t)fb_g_size); vga_putchar(' ');
    print_dec((uint64_t)fb_b_pos); vga_putchar(',');
    print_dec((uint64_t)fb_b_size); vga_putchar('\n');

    vga_writestring("FB: VBE ");
    print_dec((uint64_t)fb_width); vga_writestring("x");
    print_dec((uint64_t)fb_height); vga_writestring("x");
    print_dec((uint64_t)fb_bpp); vga_writestring(" ready\n");

    return 1;
}

int fb_init_from_mode(const struct fb_mode_info *mode) {
    if (!mode) return 0;
    if (!fb_supported_bpp(mode->bpp)) return 0;
    if (!mode->phys_base || mode->width <= 0 || mode->height <= 0) return 0;
    if (mode->pitch < mode->width * ((mode->bpp + 7) / 8)) return 0;

    if (mode->red_size == 0 || mode->green_size == 0 || mode->blue_size == 0) {
        fb_set_layout_defaults(mode->bpp);
    } else {
        fb_r_pos = mode->red_pos;
        fb_r_size = mode->red_size;
        fb_g_pos = mode->green_pos;
        fb_g_size = mode->green_size;
        fb_b_pos = mode->blue_pos;
        fb_b_size = mode->blue_size;
    }

    if ((fb_r_pos + fb_r_size) > mode->bpp) return 0;
    if ((fb_g_pos + fb_g_size) > mode->bpp) return 0;
    if ((fb_b_pos + fb_b_size) > mode->bpp) return 0;

    return fb_activate_mapped_mode(mode->phys_base,
                                   mode->width,
                                   mode->height,
                                   mode->bpp,
                                   mode->pitch);
}

int fb_is_available(void) { return fb_ready; }
int fb_get_width(void)    { return fb_width;  }
int fb_get_height(void)   { return fb_height; }
int fb_get_bpp(void)      { return fb_bpp;    }

void fb_con_set_color(uint32_t fg, uint32_t bg) {
    con_fg = fg;
    con_bg = bg;
    if (bg != FB_TRANSPARENT) con_fill_bg = bg;
}

/* =========================================================================
 * Pixel / fill
 * ======================================================================= */
void fb_set_pixel(int x, int y, uint32_t c) {
    if (!fb_ready || x<0 || x>=fb_width || y<0 || y>=fb_height) return;
    uint8_t *p = fb_row_bytes(y) + (size_t)x * (size_t)fb_bytespp;
    uint32_t raw = fb_pack_color(c);
    if (fb_native_32()) {
        *(uint32_t *)p = raw;
    } else {
        fb_write_pixel_raw(p, raw);
    }
}
void fb_fill(uint32_t c) {
    if (!fb_ready) return;
    uint32_t raw = fb_pack_color(c);
    if (fb_native_32()) {
        for (int y = 0; y < fb_height; y++) {
            uint32_t *row = (uint32_t *)fb_row_bytes(y);
            for (int x = 0; x < fb_width; x++) row[x] = raw;
        }
        return;
    }
    for (int y = 0; y < fb_height; y++) {
        uint8_t *row = fb_row_bytes(y);
        for (int x = 0; x < fb_width; x++) {
            fb_write_pixel_raw(row + (size_t)x * (size_t)fb_bytespp, raw);
        }
    }
}
void fb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (!fb_ready) return;
    if (x<0){w+=x;x=0;} if (y<0){h+=y;y=0;}
    if (x+w>fb_width)  w=fb_width-x;
    if (y+h>fb_height) h=fb_height-y;
    if (w<=0||h<=0) return;
    uint32_t raw = fb_pack_color(c);
    if (fb_native_32()) {
        for (int dy = 0; dy < h; dy++) {
            uint32_t *row = (uint32_t *)fb_row_bytes(y + dy) + x;
            for (int dx = 0; dx < w; dx++) row[dx] = raw;
        }
        return;
    }
    for (int dy = 0; dy < h; dy++) {
        uint8_t *row = fb_row_bytes(y + dy) + (size_t)x * (size_t)fb_bytespp;
        for (int dx = 0; dx < w; dx++) {
            fb_write_pixel_raw(row + (size_t)dx * (size_t)fb_bytespp, raw);
        }
    }
}
/* =========================================================================
 * Shapes
 * ======================================================================= */
/* =========================================================================
 * Text
 * ======================================================================= */
void fb_draw_char(char ch,int x,int y,uint32_t fg,uint32_t bg,int scale){
    font_draw_char(ch, x, y, fg, bg, scale);
}
void fb_draw_string(const char *s,int x,int y,
                    uint32_t fg,uint32_t bg,int scale){
    font_draw_string(s, x, y, fg, bg, scale);
}
int fb_string_width(const char *s,int scale){
    return font_string_width(s, scale);
}

/* =========================================================================
 * Console
 * ======================================================================= */
static void con_free_buffers(void) {
    if (con_screen) {
        kfree(con_screen);
        con_screen = NULL;
    }
    if (con_scrollback) {
        kfree(con_scrollback);
        con_scrollback = NULL;
    }
}

static struct fb_con_cell con_blank_cell(void) {
    struct fb_con_cell cell;
    cell.ch = ' ';
    cell.fg = con_fg;
    cell.bg = con_fill_bg;
    return cell;
}

static void con_fill_cells(struct fb_con_cell *buf, size_t count,
                           struct fb_con_cell cell) {
    if (!buf) return;
    for (size_t i = 0; i < count; i++) buf[i] = cell;
}

static void con_alloc_buffers(void) {
    con_free_buffers();

    con_saved_lines       = 0;
    con_scroll_offset     = 0;
    con_scroll_mode_active = 0;

    if (con_cols < 1 || con_rows < 1) return;

    size_t screen_cells = (size_t)con_cols * (size_t)con_rows;
    size_t scroll_cells = (size_t)con_cols * (size_t)FB_CON_SCROLLBACK_LINES;

    con_screen = (struct fb_con_cell *)kmalloc(screen_cells * sizeof(struct fb_con_cell));
    con_scrollback =
        (struct fb_con_cell *)kmalloc(scroll_cells * sizeof(struct fb_con_cell));

    if (!con_screen || !con_scrollback) {
        con_free_buffers();
        return;
    }

    struct fb_con_cell blank = con_blank_cell();
    con_fill_cells(con_screen, screen_cells, blank);
    con_fill_cells(con_scrollback, scroll_cells, blank);
}

static void con_clear_row_cells(int row) {
    if (!con_screen || row < 0 || row >= con_rows) return;
    struct fb_con_cell blank = con_blank_cell();
    size_t start = (size_t)row * (size_t)con_cols;
    for (int col = 0; col < con_cols; col++) con_screen[start + (size_t)col] = blank;
}

static void con_save_row_to_scrollback(int row) {
    if (!con_scrollback || !con_screen || row < 0 || row >= con_rows) return;

    size_t dest = (con_saved_lines % FB_CON_SCROLLBACK_LINES) * (size_t)con_cols;
    size_t src  = (size_t)row * (size_t)con_cols;
    for (int col = 0; col < con_cols; col++) {
        con_scrollback[dest + (size_t)col] = con_screen[src + (size_t)col];
    }
    con_saved_lines++;
}

static void con_draw_cell(int col, int row, struct fb_con_cell cell) {
    if (!fb_ready) return;
    int cw = font_char_width() * con_scale;
    int ch = font_char_height() * con_scale;
    int px = con_x0 + col * cw;
    int py = con_y0 + row * ch;
    fb_draw_char(cell.ch, px, py, cell.fg, cell.bg, con_scale);
}

static void con_redraw_from_scrollback(void) {
    if (!fb_ready || !con_scrollback || !con_screen) return;

    size_t total_lines = con_saved_lines;
    if (total_lines == 0) {
        fb_fill_rect(con_x0, con_y0, con_w, con_h, con_fill_bg);
        con_fill_cells(con_screen, (size_t)con_cols * (size_t)con_rows, con_blank_cell());
        return;
    }

    size_t max_scroll = 0;
    if (total_lines > (size_t)con_rows) max_scroll = total_lines - (size_t)con_rows;

    if (con_scroll_offset < 0) con_scroll_offset = 0;
    if ((size_t)con_scroll_offset > max_scroll) con_scroll_offset = (int)max_scroll;

    size_t display_start = 0;
    if (total_lines > (size_t)con_rows + (size_t)con_scroll_offset) {
        display_start = total_lines - (size_t)con_rows - (size_t)con_scroll_offset;
    }

    for (int row = 0; row < con_rows; row++) {
        size_t line = display_start + (size_t)row;
        size_t src  = (line % FB_CON_SCROLLBACK_LINES) * (size_t)con_cols;
        for (int col = 0; col < con_cols; col++) {
            struct fb_con_cell cell = con_scrollback[src + (size_t)col];
            con_screen[(size_t)row * (size_t)con_cols + (size_t)col] = cell;
            con_draw_cell(col, row, cell);
        }
    }
}

static void con_scroll_pixels(void) {
    /* Redraw from the cell buffer to avoid slow framebuffer readback. */
    if (!fb_ready || !con_screen) return;

    int cw = font_char_width() * con_scale;
    int ch = font_char_height() * con_scale;
    for (int row = 0; row < con_rows - 1; row++) {
        for (int col = 0; col < con_cols; col++) {
            struct fb_con_cell cell =
                con_screen[(size_t)row * (size_t)con_cols + (size_t)col];
            int px = con_x0 + col * cw;
            int py = con_y0 + row * ch;
            fb_draw_char(cell.ch, px, py, cell.fg, cell.bg, con_scale);
        }
    }
    fb_fill_rect(con_x0, con_y0 + (con_rows - 1) * ch, con_w, ch, con_fill_bg);
}

static void con_scroll(void) {
    if (!fb_ready) return;

    if (con_screen) {
        con_save_row_to_scrollback(0);
        memmove(con_screen,
                con_screen + con_cols,
                (size_t)(con_rows - 1) * (size_t)con_cols * sizeof(struct fb_con_cell));
        con_clear_row_cells(con_rows - 1);
    }

    con_scroll_pixels();
}

static void con_draw_scroll_help_bar(void) {
    int ch = font_char_height() * con_scale;
    int y  = con_y0 + (con_rows - 1) * ch;

    uint32_t bar_bg = FB_BLACK;
    uint32_t bar_fg = FB_WHITE;

    fb_fill_rect(con_x0, y, con_w, ch, bar_bg);
    fb_draw_string(" UP DOWN or W S scroll. Q exit ",
                   con_x0 + 4, y, bar_fg, bar_bg, con_scale);

    if (con_scroll_offset > 0) {
        char buf[32];
        int  p = 0;
        const char *pre = "SCROLL ";
        for (int i = 0; pre[i] && p < 24; i++) buf[p++] = pre[i];

        uint64_t v = (uint64_t)con_scroll_offset;
        char rev[20];
        int  rp = 0;
        if (v == 0) rev[rp++] = '0';
        while (v > 0 && rp < (int)sizeof(rev)) {
            rev[rp++] = (char)('0' + (v % 10));
            v /= 10;
        }
        for (int i = rp - 1; i >= 0 && p < 31; i--) buf[p++] = rev[i];
        buf[p] = '\0';

        int w = fb_string_width(buf, con_scale);
        fb_draw_string(buf, con_x0 + con_w - w - 4, y, bar_fg, bar_bg, con_scale);
    }
}

static void con_scroll_up(void) {
    int max_scroll = (int)con_saved_lines - con_rows;
    if (max_scroll < 0) max_scroll = 0;
    if (con_scroll_offset < max_scroll) {
        con_scroll_offset++;
        con_redraw_from_scrollback();
        con_draw_scroll_help_bar();
    }
}

static void con_scroll_down(void) {
    if (con_scroll_offset > 0) {
        con_scroll_offset--;
        con_redraw_from_scrollback();
        con_draw_scroll_help_bar();
    }
}

void fb_con_init(int x, int y, int w, int h,
                 uint32_t fg, uint32_t bg, int scale) {
    if (scale < 1) scale = 1;

    con_x0 = x;
    con_y0 = y;
    con_w  = w;
    con_h  = h;
    con_fg = fg;
    con_bg = bg;
    con_scale = scale;
    con_fill_bg = (bg == FB_TRANSPARENT) ? FB_TERM_BG : bg;

    con_cx = 0;
    con_cy = 0;

    int cw = font_char_width() * scale;
    int ch = font_char_height() * scale;
    con_cols = cw ? (w / cw) : 1;
    con_rows = ch ? (h / ch) : 1;
    if (con_cols < 1) con_cols = 1;
    if (con_rows < 1) con_rows = 1;

    con_alloc_buffers();
}

void fb_con_clear(void) {
    fb_fill_rect(con_x0, con_y0, con_w, con_h, con_fill_bg);
    con_cx = 0;
    con_cy = 0;
    con_scroll_offset = 0;
    con_scroll_mode_active = 0;

    if (con_screen) {
        con_fill_cells(con_screen,
                       (size_t)con_cols * (size_t)con_rows,
                       con_blank_cell());
    }
}

void fb_con_putchar(char c) {
    if (!fb_ready || con_scroll_mode_active) return;

    int cw = font_char_width() * con_scale;
    int ch = font_char_height() * con_scale;

    if (c == '\f') { fb_con_clear(); return; }

    if (c == '\n') {
        con_cx = 0;
        con_cy++;
    } else if (c == '\r') {
        con_cx = 0;
    } else if (c == '\t') {
        int spaces = 4 - (con_cx % 4);
        for (int i = 0; i < spaces; i++) fb_con_putchar(' ');
        return;
    } else if (c == '\b') {
        if (con_cx > 0) {
            con_cx--;
            fb_fill_rect(con_x0 + con_cx * cw, con_y0 + con_cy * ch, cw, ch, con_fill_bg);

            if (con_screen && con_cx < con_cols && con_cy < con_rows) {
                con_screen[(size_t)con_cy * (size_t)con_cols + (size_t)con_cx] =
                    con_blank_cell();
            }
        }
    } else {
        if (con_cx >= con_cols) {
            con_cx = 0;
            con_cy++;
        }
        if (con_cy >= con_rows) {
            con_scroll();
            con_cy = con_rows - 1;
        }

        fb_draw_char(c,
                     con_x0 + con_cx * cw,
                     con_y0 + con_cy * ch,
                     con_fg, con_bg, con_scale);

        if (con_screen && con_cx < con_cols && con_cy < con_rows) {
            struct fb_con_cell cell;
            cell.ch = c;
            cell.fg = con_fg;
            cell.bg = (con_bg == FB_TRANSPARENT) ? con_fill_bg : con_bg;
            con_screen[(size_t)con_cy * (size_t)con_cols + (size_t)con_cx] = cell;
        }

        con_cx++;
    }

    if (con_cy >= con_rows) {
        con_scroll();
        con_cy = con_rows - 1;
    }
}

void fb_con_write(const char *buf, size_t len) {
    if (!buf) return;
    for (size_t i = 0; i < len; i++) fb_con_putchar(buf[i]);
}

void fb_con_print(const char *s) {
    if (!s) return;
    fb_con_write(s, strlen(s));
}

void fb_con_enter_scroll_mode(void) {
    if (!fb_ready || !con_screen || !con_scrollback) return;

    int live_cx = con_cx;
    int live_cy = con_cy;
    uint64_t next_repeat_ms = 0;
    char repeat_key = 0;
    const uint64_t first_repeat_delay_ms = 180;
    const uint64_t repeat_period_ms = 28;

    for (int row = 0; row < con_rows; row++) {
        con_save_row_to_scrollback(row);
    }

    con_scroll_offset = 0;
    con_scroll_mode_active = 1;

    con_redraw_from_scrollback();
    con_draw_scroll_help_bar();
    keyboard_flush_buffer();

    while (con_scroll_mode_active) {
        int had_input = 0;
        int did_repeat = 0;
        char c = 0;

        while (keyboard_try_getchar(&c)) {
            uint64_t now = timer_get_uptime_ms();
            had_input = 1;
            switch (c) {
                case KEY_SPECIAL_UP:
                    con_scroll_up();
                    repeat_key = KEY_SPECIAL_UP;
                    next_repeat_ms = now + first_repeat_delay_ms;
                    break;
                case KEY_SPECIAL_DOWN:
                    con_scroll_down();
                    repeat_key = KEY_SPECIAL_DOWN;
                    next_repeat_ms = now + first_repeat_delay_ms;
                    break;
                case 'w': case 'W':
                    con_scroll_up();
                    repeat_key = 0;
                    break;
                case 's': case 'S':
                    con_scroll_down();
                    repeat_key = 0;
                    break;
                case 'q': case 'Q':
                    con_scroll_mode_active = 0;
                    break;
                default:
                    break;
            }
            if (!con_scroll_mode_active) break;
        }

        if (!con_scroll_mode_active) break;

        uint64_t now = timer_get_uptime_ms();
        int up_pressed = keyboard_is_special_pressed(KEY_SPECIAL_UP);
        int down_pressed = keyboard_is_special_pressed(KEY_SPECIAL_DOWN);

        if (up_pressed && !down_pressed) {
            if (repeat_key != KEY_SPECIAL_UP) {
                repeat_key = KEY_SPECIAL_UP;
                next_repeat_ms = now + first_repeat_delay_ms;
            } else if (now >= next_repeat_ms) {
                con_scroll_up();
                next_repeat_ms = now + repeat_period_ms;
                did_repeat = 1;
            }
        } else if (down_pressed && !up_pressed) {
            if (repeat_key != KEY_SPECIAL_DOWN) {
                repeat_key = KEY_SPECIAL_DOWN;
                next_repeat_ms = now + first_repeat_delay_ms;
            } else if (now >= next_repeat_ms) {
                con_scroll_down();
                next_repeat_ms = now + repeat_period_ms;
                did_repeat = 1;
            }
        } else {
            repeat_key = 0;
            next_repeat_ms = 0;
        }

        if (!had_input && !did_repeat) fb_console_wait_for_input();
    }

    con_scroll_offset = 0;
    con_scroll_mode_active = 0;
    con_redraw_from_scrollback();
    con_cx = live_cx;
    con_cy = live_cy;
}
