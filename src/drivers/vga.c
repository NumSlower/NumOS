/*
 * vga.c - VGA text mode driver with scrollback buffer
 *
 * Colour model
 * ============
 * VGA text mode stores a 16-bit word per cell:
 *   bits 15-8  : attribute byte  (bg[7:4] | fg[3:0], or blink|bg[6:4]|fg[3:0])
 *   bits  7-0  : ASCII character
 *
 * By default the VGA chip treats attribute bit 7 as "blink enable", limiting
 * background colours to 0-7.  Writing 0 to bit 3 of the Attribute-Controller
 * Mode-Control register (index 0x10) switches bit 7 to "background intensity"
 * instead, unlocking all 16 background colours — giving 16×16 = 256 unique
 * colour combinations (the maximum for VGA text mode).
 *
 * vga_disable_blink() performs that one-time register write and is called
 * automatically inside vga_init().
 */

#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/string.h"
#include "kernel/kernel.h"

/* ── VGA register port numbers ─────────────────────────────────────────── */
#define VGA_AC_INDEX        0x3C0   /* Attribute Controller index/data write  */
#define VGA_AC_READ         0x3C1   /* Attribute Controller data read         */
#define VGA_INPUT_STATUS1   0x3DA   /* Input Status Register 1 (also resets AC flip-flop) */
#define VGA_AC_MODE_CTRL    0x10    /* AC register index: Mode Control        */

/* ── Driver state ──────────────────────────────────────────────────────── */
static size_t   vga_row;
static size_t   vga_column;
static uint8_t  vga_text_color;
static uint16_t *vga_buffer;

/* Scrollback buffer */
#define SCROLLBACK_LINES 200
static uint16_t scrollback_buffer[SCROLLBACK_LINES * VGA_WIDTH];
static size_t   scrollback_current_line = 0;
static int      scroll_offset           = 0;
static int      scroll_mode_active      = 0;

/* Color stack for nested color changes */
#define COLOR_STACK_SIZE 8
static uint8_t color_stack[COLOR_STACK_SIZE];
static int     color_stack_top = -1;

/* ── Attribute helpers ─────────────────────────────────────────────────── */

/*
 * vga_entry_color — build attribute byte from fg/bg colour indices (0-15).
 *
 * Layout:  bits[7:4] = bg, bits[3:0] = fg
 *
 * After vga_disable_blink() all 256 combinations render their correct
 * background colour.  Before (or if blink is re-enabled) indices 8-15 in
 * the background nibble produce blinking text with bg colours 0-7.
 */
uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((uint8_t)fg | ((uint8_t)bg << 4));
}

uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

/* ── Blink / background-intensity control ─────────────────────────────── */

/*
 * vga_disable_blink — unlock all 16 background colours.
 *
 * The VGA Attribute Controller's Mode Control register (AC index 0x10)
 * has bit 3 = "Blink Enable".  Clearing it makes attribute bit 7 select
 * background intensity rather than blink, giving the full 16-colour
 * background palette.
 *
 * Protocol:
 *   1. Read 0x3DA to reset the AC flip-flop to "index" state.
 *   2. Write the register index to 0x3C0 (with bit 5 = 0 to keep display off
 *      while we update; then bit 5 = 1 to re-enable).
 *      Actually the safest sequence is: write index then immediately write data.
 *   3. Re-enable display output by writing 0x20 to 0x3C0.
 */
void vga_disable_blink(void) {
    /* Reset the AC flip-flop — any read of 0x3DA does this */
    inb(VGA_INPUT_STATUS1);

    /* Select AC register 0x10 (Mode Control), bit 5 clear = access regs */
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);

    /* Read current value of Mode Control */
    uint8_t mode_ctrl = inb(VGA_AC_READ);

    /* Clear bit 3 (Blink Enable) → background intensity mode */
    mode_ctrl &= ~(1 << 3);

    /* Reset flip-flop again before writing */
    inb(VGA_INPUT_STATUS1);

    /* Write index */
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    /* Write new data value */
    outb(VGA_AC_INDEX, mode_ctrl);

    /* Re-enable video output: write 0x20 to 0x3C0 */
    outb(VGA_AC_INDEX, 0x20);
}

/*
 * vga_enable_blink — restore hardware-default blink mode.
 * Background colours 8-15 will again cause blinking text.
 */
void vga_enable_blink(void) {
    inb(VGA_INPUT_STATUS1);
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    uint8_t mode_ctrl = inb(VGA_AC_READ);
    mode_ctrl |= (1 << 3);     /* set bit 3 = blink enable */
    inb(VGA_INPUT_STATUS1);
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    outb(VGA_AC_INDEX, mode_ctrl);
    outb(VGA_AC_INDEX, 0x20);
}

/* ── Initialisation ────────────────────────────────────────────────────── */

void vga_init(void) {
    vga_row    = 0;
    vga_column = 0;
    vga_text_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_buffer = (uint16_t *)VGA_MEMORY;
    color_stack_top = -1;
    scrollback_current_line = 0;
    scroll_offset      = 0;
    scroll_mode_active = 0;

    /* Unlock all 16 background colours by disabling blink mode */
    vga_disable_blink();

    /* Clear scrollback buffer */
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t i = 0; i < SCROLLBACK_LINES * VGA_WIDTH; i++) {
        scrollback_buffer[i] = blank;
    }

    vga_clear();
    vga_enable_cursor(14, 15);
}

/* ── Screen clear ──────────────────────────────────────────────────────── */

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    vga_row    = 0;
    vga_column = 0;
    vga_update_cursor(0, 0);
}

/* ── Colour control ────────────────────────────────────────────────────── */

void vga_setcolor(uint8_t color) {
    vga_text_color = color;
}

void vga_push_color(void) {
    if (color_stack_top < COLOR_STACK_SIZE - 1) {
        color_stack[++color_stack_top] = vga_text_color;
    }
}

void vga_pop_color(void) {
    if (color_stack_top >= 0) {
        vga_text_color = color_stack[color_stack_top--];
    }
}

/* ── Character output ──────────────────────────────────────────────────── */

void vga_putchar(char c) {
    if (c == '\n') {
        vga_newline();
        return;
    }
    if (c == '\r') {
        vga_column = 0;
        vga_update_cursor(vga_column, vga_row);
        return;
    }
    if (c == '\b') {
        if (vga_column > 0) {
            vga_column--;
            vga_buffer[vga_row * VGA_WIDTH + vga_column] = vga_entry(' ', vga_text_color);
            vga_update_cursor(vga_column, vga_row);
        }
        return;
    }
    if (c == '\t') {
        size_t spaces = 4 - (vga_column % 4);
        for (size_t i = 0; i < spaces; i++) {
            vga_putchar(' ');
        }
        return;
    }

    vga_buffer[vga_row * VGA_WIDTH + vga_column] = vga_entry(c, vga_text_color);

    if (++vga_column == VGA_WIDTH) {
        vga_newline();
    } else {
        vga_update_cursor(vga_column, vga_row);
    }
}

void vga_write(const char *data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        vga_putchar(data[i]);
    }
}

void vga_writestring(const char *data) {
    vga_write(data, strlen(data));
}

void vga_newline(void) {
    vga_column = 0;
    if (++vga_row == VGA_HEIGHT) {
        vga_scroll();
        vga_row = VGA_HEIGHT - 1;
    }
    vga_update_cursor(vga_column, vga_row);
}

/* ── Scrollback buffer ─────────────────────────────────────────────────── */

static void save_line_to_scrollback(size_t line_num) {
    size_t scrollback_line = scrollback_current_line % SCROLLBACK_LINES;
    for (size_t i = 0; i < VGA_WIDTH; i++) {
        scrollback_buffer[scrollback_line * VGA_WIDTH + i] =
            vga_buffer[line_num * VGA_WIDTH + i];
    }
    scrollback_current_line++;
}

void vga_scroll(void) {
    save_line_to_scrollback(0);
    memmove(vga_buffer, vga_buffer + VGA_WIDTH,
            (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t));
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
}

void vga_save_screen_to_scrollback(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t scrollback_line = scrollback_current_line % SCROLLBACK_LINES;
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[scrollback_line * VGA_WIDTH + x] =
                vga_buffer[y * VGA_WIDTH + x];
        }
        scrollback_current_line++;
    }
}

static void vga_redraw_from_scrollback(void) {
    if (scrollback_current_line < VGA_HEIGHT) {
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            size_t scrollback_line = (y < scrollback_current_line)
                                     ? y : scrollback_current_line - 1;
            if (scrollback_line >= SCROLLBACK_LINES)
                scrollback_line = SCROLLBACK_LINES - 1;
            size_t src = (scrollback_line % SCROLLBACK_LINES) * VGA_WIDTH;
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src + x];
            }
        }
        return;
    }

    size_t total_lines   = scrollback_current_line;
    size_t display_start = (scroll_offset == 0)
                           ? total_lines - VGA_HEIGHT
                           : total_lines - VGA_HEIGHT - scroll_offset;

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line_to_show  = display_start + y;
        size_t scrollback_line = line_to_show % SCROLLBACK_LINES;
        size_t src = scrollback_line * VGA_WIDTH;
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src + x];
        }
    }

    /* Scroll position indicator (dark-grey bg, white fg — uses bright bg) */
    if (scroll_offset > 0) {
        uint8_t indicator_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);
        char indicator[24];
        const char *prefix = " SCROLL ";
        int pos = 0;
        for (int i = 0; prefix[i] && pos < 23; i++) {
            indicator[pos++] = prefix[i];
        }
        size_t current_top_line =
            total_lines - VGA_HEIGHT - scroll_offset + 1;
        char num_buf[10];
        int num_pos = 0;
        size_t temp = current_top_line;
        if (temp == 0) {
            num_buf[num_pos++] = '0';
        } else {
            char rev[10]; int rp = 0;
            while (temp > 0 && rp < 9) { rev[rp++] = '0' + (temp % 10); temp /= 10; }
            for (int i = rp - 1; i >= 0 && num_pos < 9; i--) num_buf[num_pos++] = rev[i];
        }
        for (int i = 0; i < num_pos && pos < 22; i++) indicator[pos++] = num_buf[i];
        if (pos < 23) indicator[pos++] = ' ';
        indicator[pos] = '\0';
        for (size_t i = 0; i < (size_t)pos && i < VGA_WIDTH; i++) {
            vga_buffer[VGA_WIDTH - pos + i] =
                vga_entry(indicator[i], indicator_color);
        }
    }
}

void vga_scroll_up(void) {
    int max_scroll = (int)scrollback_current_line - (int)VGA_HEIGHT;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset < max_scroll) {
        scroll_offset++;
        vga_redraw_from_scrollback();
    }
}

void vga_scroll_down(void) {
    if (scroll_offset > 0) {
        scroll_offset--;
        vga_redraw_from_scrollback();
        if (scroll_offset == 0) {
            for (size_t y = 0; y < VGA_HEIGHT; y++) {
                size_t line_num = scrollback_current_line - VGA_HEIGHT + y;
                size_t src = (line_num % SCROLLBACK_LINES) * VGA_WIDTH;
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src + x];
                }
            }
        }
    }
}

void vga_enter_scroll_mode(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t scrollback_line = scrollback_current_line % SCROLLBACK_LINES;
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[scrollback_line * VGA_WIDTH + x] =
                vga_buffer[y * VGA_WIDTH + x];
        }
        scrollback_current_line++;
    }
    scroll_offset      = 0;
    scroll_mode_active = 1;
    vga_disable_cursor();

    /* Help bar — blue background (now a full bright blue, not just dark) */
    uint8_t help_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    const char *help_text = " UP/DOWN Arrows: Scroll | Q: Quit ";
    size_t help_len = strlen(help_text);
    size_t start_x  = (VGA_WIDTH - help_len) / 2;
    for (size_t i = 0; i < help_len && (start_x + i) < VGA_WIDTH; i++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + start_x + i] =
            vga_entry(help_text[i], help_color);
    }

    while (scroll_mode_active) {
        uint8_t scan_code = keyboard_read_scan_code();
        if (scan_code == 0xE0) {
            scan_code = keyboard_read_scan_code();
            if (scan_code == 0x48) vga_scroll_up();
            else if (scan_code == 0x50) vga_scroll_down();
            continue;
        }
        char c = scan_code_to_ascii(scan_code);
        if (c == 0) continue;
        if (c == 'q' || c == 'Q') { scroll_mode_active = 0; break; }
        if (c == 'w' || c == 'W') vga_scroll_up();
        else if (c == 's' || c == 'S') vga_scroll_down();
    }

    scroll_offset = 0;
    vga_enable_cursor(14, 15);
    vga_clear();
    vga_writestring("Exited scroll mode\n");
}

/* ── Cursor ────────────────────────────────────────────────────────────── */

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | cursor_start);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | cursor_end);
}

void vga_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

void vga_update_cursor(int x, int y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_get_cursor(int *x, int *y) {
    if (x) *x = (int)vga_column;
    if (y) *y = (int)vga_row;
}

void vga_set_cursor(int x, int y) {
    if (x >= 0 && x < (int)VGA_WIDTH && y >= 0 && y < (int)VGA_HEIGHT) {
        vga_column = (size_t)x;
        vga_row    = (size_t)y;
        vga_update_cursor(x, y);
    }
}

/* ── Positioned writes ─────────────────────────────────────────────────── */

void vga_putchar_at(char c, uint8_t color, int x, int y) {
    if (x >= 0 && x < (int)VGA_WIDTH && y >= 0 && y < (int)VGA_HEIGHT) {
        vga_buffer[y * VGA_WIDTH + x] = vga_entry(c, color);
    }
}

void vga_write_at(const char *str, uint8_t color, int x, int y) {
    int orig_x = x;
    while (*str && y < (int)VGA_HEIGHT) {
        if (*str == '\n') { y++; x = orig_x; }
        else {
            vga_putchar_at(*str, color, x++, y);
            if (x >= (int)VGA_WIDTH) { x = orig_x; y++; }
        }
        str++;
    }
}

void vga_fill_rect(char c, uint8_t color, int x, int y, int width, int height) {
    for (int dy = 0; dy < height && y + dy < (int)VGA_HEIGHT; dy++)
        for (int dx = 0; dx < width && x + dx < (int)VGA_WIDTH; dx++)
            vga_putchar_at(c, color, x + dx, y + dy);
}

void vga_draw_box(int x, int y, int width, int height, uint8_t color) {
    vga_putchar_at('+', color, x,             y);
    vga_putchar_at('+', color, x + width - 1, y);
    vga_putchar_at('+', color, x,             y + height - 1);
    vga_putchar_at('+', color, x + width - 1, y + height - 1);
    for (int i = 1; i < width  - 1; i++) {
        vga_putchar_at('-', color, x + i, y);
        vga_putchar_at('-', color, x + i, y + height - 1);
    }
    for (int i = 1; i < height - 1; i++) {
        vga_putchar_at('|', color, x,             y + i);
        vga_putchar_at('|', color, x + width - 1, y + i);
    }
}

void vga_print_progress(int percentage, int x, int y, int width) {
    /* Uses bright-green on black fill and dark-grey on black empty */
    uint8_t bar_color   = vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t empty_color = vga_entry_color(VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK);
    int filled = (percentage * width) / 100;
    vga_putchar_at('[', VGA_COLOR_WHITE, x, y);
    for (int i = 0; i < width; i++) {
        char    ch    = (i < filled) ? '=' : ' ';
        uint8_t col   = (i < filled) ? bar_color : empty_color;
        vga_putchar_at(ch, col, x + 1 + i, y);
    }
    vga_putchar_at(']', VGA_COLOR_WHITE, x + width + 1, y);
}