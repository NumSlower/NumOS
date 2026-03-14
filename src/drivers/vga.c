/*
 * vga.c - VGA text-mode driver with scrollback buffer
 *
 * Colour model
 * ============
 * VGA text mode stores one 16-bit word per cell:
 *   bits 15-8 : attribute byte  (bg[7:4] | fg[3:0])
 *   bits  7-0 : ASCII character
 *
 * By default attribute bit 7 means "blink", limiting background colours
 * to indices 0-7.  Writing 0 to bit 3 of the Attribute Controller Mode
 * Control register (index 0x10) at port 0x3C0 switches bit 7 to
 * "background intensity", unlocking all 16 background colours for a
 * total of 256 unique colour combinations.
 *
 * vga_disable_blink() performs that one-time register write and is called
 * automatically inside vga_init().
 *
 * Scrollback buffer
 * =================
 * Every time vga_scroll() evicts the top screen line, it saves a copy
 * into a circular SCROLLBACK_LINES-line ring buffer.
 * vga_enter_scroll_mode() saves the full current screen into the same
 * buffer and enters an interactive navigation loop driven by arrow keys.
 */

#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/string.h"
#include "kernel/kernel.h"

/* =========================================================================
 * VGA Attribute Controller register addresses
 * ======================================================================= */

#define VGA_AC_INDEX       0x3C0   /* AC index/data write port      */
#define VGA_AC_READ        0x3C1   /* AC data read port             */
#define VGA_INPUT_STATUS1  0x3DA   /* Input Status 1 (resets AC FF) */
#define VGA_AC_MODE_CTRL   0x10    /* AC Mode Control register index */

/* =========================================================================
 * Driver state
 * ======================================================================= */

static size_t    vga_row;          /* current cursor row  (0..VGA_HEIGHT-1) */
static size_t    vga_column;       /* current cursor col  (0..VGA_WIDTH-1)  */
static uint8_t   vga_text_color;   /* current attribute byte                */
static uint16_t *vga_buffer;       /* pointer to VGA MMIO text buffer       */

/* ---- Scrollback ring buffer -------------------------------------------- */
#define SCROLLBACK_LINES 4096

static uint16_t scrollback_buffer[SCROLLBACK_LINES * VGA_WIDTH];
static size_t   scrollback_current_line = 0; /* total lines ever saved      */
static int      scroll_offset           = 0; /* lines above current view    */
static int      scroll_mode_active      = 0; /* non-zero while in scroll UI */

/* ---- Colour stack for nested colour changes ----------------------------- */
#define COLOR_STACK_SIZE 8

static uint8_t color_stack[COLOR_STACK_SIZE];
static int     color_stack_top = -1;

/* =========================================================================
 * Attribute helpers
 * ======================================================================= */

/*
 * vga_entry_color - build a 1-byte attribute from foreground and background
 * colour indices.  Background occupies bits [7:4], foreground bits [3:0].
 * After vga_disable_blink() all 256 combinations render correctly.
 */
uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)((uint8_t)fg | ((uint8_t)bg << 4));
}

/*
 * vga_entry - combine an ASCII character and its attribute byte into the
 * 16-bit word the VGA text buffer expects.
 */
uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

/* =========================================================================
 * Blink / background-intensity control
 * ======================================================================= */

/*
 * vga_disable_blink - switch attribute bit 7 from "blink" to "background
 * intensity", unlocking all 16 background colours.
 *
 * Protocol:
 *   1. Read 0x3DA to reset the AC flip-flop to index state.
 *   2. Write the register index 0x10 to 0x3C0.
 *   3. Read the current value from 0x3C1.
 *   4. Clear bit 3 (Blink Enable) in the read value.
 *   5. Reset the flip-flop again (read 0x3DA).
 *   6. Write index 0x10 then the new value to 0x3C0.
 *   7. Write 0x20 to 0x3C0 to re-enable video output.
 */
void vga_disable_blink(void) {
    inb(VGA_INPUT_STATUS1);              /* reset AC flip-flop */
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    uint8_t mode_ctrl = inb(VGA_AC_READ);
    mode_ctrl &= (uint8_t)~(1 << 3);    /* clear Blink Enable bit */
    inb(VGA_INPUT_STATUS1);              /* reset AC flip-flop again */
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    outb(VGA_AC_INDEX, mode_ctrl);
    outb(VGA_AC_INDEX, 0x20);            /* re-enable video output */
}

/*
 * vga_enable_blink - restore hardware-default blink mode.
 * Background colour indices 8-15 will again cause blinking text.
 */
void vga_enable_blink(void) {
    inb(VGA_INPUT_STATUS1);
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    uint8_t mode_ctrl = inb(VGA_AC_READ);
    mode_ctrl |= (uint8_t)(1 << 3);     /* set Blink Enable bit */
    inb(VGA_INPUT_STATUS1);
    outb(VGA_AC_INDEX, VGA_AC_MODE_CTRL);
    outb(VGA_AC_INDEX, mode_ctrl);
    outb(VGA_AC_INDEX, 0x20);
}

/* =========================================================================
 * Initialisation and clear
 * ======================================================================= */

/*
 * vga_init - reset all driver state, disable blink, clear the screen,
 * and enable the hardware cursor.
 */
void vga_init(void) {
    vga_row    = 0;
    vga_column = 0;
    vga_text_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_buffer = (uint16_t *)VGA_MEMORY;
    color_stack_top = -1;
    scrollback_current_line = 0;
    scroll_offset      = 0;
    scroll_mode_active = 0;

    vga_disable_blink();

    /* Zero the scrollback ring buffer */
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t i = 0; i < SCROLLBACK_LINES * VGA_WIDTH; i++) {
        scrollback_buffer[i] = blank;
    }

    vga_clear();
    vga_enable_cursor(14, 15);
}

/*
 * vga_clear - fill the entire screen with spaces in the current colour
 * and reset the cursor to the top-left.
 */
void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    vga_row    = 0;
    vga_column = 0;
    vga_update_cursor(0, 0);
}

/* =========================================================================
 * Colour management
 * ======================================================================= */

/*
 * vga_setcolor - set the current foreground/background attribute byte.
 */
void vga_setcolor(uint8_t color) {
    vga_text_color = color;
}

/*
 * vga_push_color - save the current colour on the colour stack.
 * A maximum of COLOR_STACK_SIZE levels are supported; extra pushes are
 * silently dropped.
 */
void vga_push_color(void) {
    if (color_stack_top < COLOR_STACK_SIZE - 1) {
        color_stack[++color_stack_top] = vga_text_color;
    }
}

/*
 * vga_pop_color - restore the most recently pushed colour.
 * Does nothing if the stack is empty.
 */
void vga_pop_color(void) {
    if (color_stack_top >= 0) {
        vga_text_color = color_stack[color_stack_top--];
    }
}

/* =========================================================================
 * Character output
 * ======================================================================= */

/*
 * vga_putchar - write one character to the current cursor position and
 * advance the cursor.
 *
 * Control characters handled:
 *   '\n' - move to next line (scrolls if needed)
 *   '\r' - return to column 0
 *   '\b' - erase the character to the left of the cursor
 *   '\t' - advance to the next 4-column tab stop
 */
void vga_putchar(char c) {
    switch (c) {
        case '\n':
            vga_newline();
            return;

        case '\r':
            vga_column = 0;
            vga_update_cursor((int)vga_column, (int)vga_row);
            return;

        case '\b':
            if (vga_column > 0) {
                vga_column--;
                vga_buffer[vga_row * VGA_WIDTH + vga_column] =
                    vga_entry(' ', vga_text_color);
                vga_update_cursor((int)vga_column, (int)vga_row);
            }
            return;

        case '\t': {
            size_t spaces = 4 - (vga_column % 4);
            for (size_t i = 0; i < spaces; i++) vga_putchar(' ');
            return;
        }

        default:
            break;
    }

    vga_buffer[vga_row * VGA_WIDTH + vga_column] =
        vga_entry((unsigned char)c, vga_text_color);

    if (++vga_column == VGA_WIDTH) {
        vga_newline();
    } else {
        vga_update_cursor((int)vga_column, (int)vga_row);
    }
}

/*
 * vga_write - write size bytes from data to the screen.
 */
void vga_write(const char *data, size_t size) {
    for (size_t i = 0; i < size; i++) vga_putchar(data[i]);
}

/*
 * vga_writestring - write a null-terminated string to the screen.
 */
void vga_writestring(const char *data) {
    vga_write(data, strlen(data));
}

/*
 * vga_newline - move the cursor to the start of the next row.
 * Scrolls the screen up by one line and saves the evicted row to the
 * scrollback buffer when the bottom row is reached.
 */
void vga_newline(void) {
    vga_column = 0;
    if (++vga_row == VGA_HEIGHT) {
        vga_scroll();
        vga_row = VGA_HEIGHT - 1;
    }
    vga_update_cursor((int)vga_column, (int)vga_row);
}

/* =========================================================================
 * Scrollback buffer management
 * ======================================================================= */

/*
 * save_line_to_scrollback - copy one screen line into the ring buffer.
 * scrollback_current_line is incremented after each save so the ring
 * wraps correctly.
 */
static void save_line_to_scrollback(size_t line_num) {
    size_t dest = (scrollback_current_line % SCROLLBACK_LINES) * VGA_WIDTH;
    for (size_t i = 0; i < VGA_WIDTH; i++) {
        scrollback_buffer[dest + i] = vga_buffer[line_num * VGA_WIDTH + i];
    }
    scrollback_current_line++;
}

/*
 * vga_scroll - scroll the screen up by one line.
 * The top row is saved to the scrollback buffer, the remaining rows are
 * shifted up, and the bottom row is cleared.
 */
void vga_scroll(void) {
    save_line_to_scrollback(0);

    memmove(vga_buffer,
            vga_buffer + VGA_WIDTH,
            (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t));

    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
}

/*
 * vga_save_screen_to_scrollback - copy every row of the current screen into
 * the scrollback buffer.  Called before entering scroll mode so the current
 * content is preserved and navigable.
 */
void vga_save_screen_to_scrollback(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t dest = (scrollback_current_line % SCROLLBACK_LINES) * VGA_WIDTH;
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[dest + x] = vga_buffer[y * VGA_WIDTH + x];
        }
        scrollback_current_line++;
    }
}

/*
 * vga_redraw_from_scrollback - repaint the visible screen from the ring
 * buffer at the current scroll_offset.
 *
 * When scroll_offset == 0 the most recent SCROLLBACK_LINES lines are shown.
 * Positive scroll_offset scrolls backward into history.
 *
 * A "SCROLL <line>" indicator is rendered in the top-right corner when
 * scroll_offset > 0 to show the user's current position.
 */
static void vga_redraw_from_scrollback(void) {
    size_t total_lines = scrollback_current_line;

    /* Not enough lines yet to fill the screen */
    if (total_lines < VGA_HEIGHT) {
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            size_t line = (y < total_lines) ? y : total_lines - 1;
            size_t src  = (line % SCROLLBACK_LINES) * VGA_WIDTH;
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src + x];
            }
        }
        return;
    }

    size_t display_start = total_lines - VGA_HEIGHT - (size_t)scroll_offset;

    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line_to_show  = display_start + y;
        size_t src = (line_to_show % SCROLLBACK_LINES) * VGA_WIDTH;
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src + x];
        }
    }

    /* Render position indicator in the top-right when scrolled back */
    if (scroll_offset > 0) {
        uint8_t indicator_color =
            vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_DARK_GREY);

        /* Build " SCROLL <line> " string */
        char   indicator[24];
        int    pos = 0;
        const char *prefix = " SCROLL ";
        for (int i = 0; prefix[i] && pos < 23; i++) {
            indicator[pos++] = prefix[i];
        }

        size_t current_top = total_lines - VGA_HEIGHT - (size_t)scroll_offset + 1;
        char   rev[10];
        int    rp = 0;
        size_t temp = current_top;
        if (temp == 0) {
            rev[rp++] = '0';
        } else {
            while (temp > 0 && rp < 9) { rev[rp++] = (char)('0' + temp % 10); temp /= 10; }
        }
        for (int i = rp - 1; i >= 0 && pos < 22; i--) {
            indicator[pos++] = rev[i];
        }
        if (pos < 23) indicator[pos++] = ' ';
        indicator[pos] = '\0';

        for (int i = 0; i < pos && (size_t)i < VGA_WIDTH; i++) {
            vga_buffer[VGA_WIDTH - pos + i] =
                vga_entry((unsigned char)indicator[i], indicator_color);
        }
    }
}

/*
 * vga_scroll_up - scroll the view one line further into history.
 * Does nothing if already at the oldest available line.
 */
void vga_scroll_up(void) {
    int max_scroll = (int)scrollback_current_line - (int)VGA_HEIGHT;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_offset < max_scroll) {
        scroll_offset++;
        vga_redraw_from_scrollback();
    }
}

/*
 * vga_scroll_down - scroll the view one line toward the present.
 * When scroll_offset reaches 0 the live screen content is restored from
 * the scrollback buffer.
 */
void vga_scroll_down(void) {
    if (scroll_offset > 0) {
        scroll_offset--;
        vga_redraw_from_scrollback();

        if (scroll_offset == 0) {
            /* Restore the most recent VGA_HEIGHT lines */
            for (size_t y = 0; y < VGA_HEIGHT; y++) {
                size_t line = scrollback_current_line - VGA_HEIGHT + y;
                size_t src  = (line % SCROLLBACK_LINES) * VGA_WIDTH;
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src + x];
                }
            }
        }
    }
}

/*
 * vga_enter_scroll_mode - save the current screen to the scrollback buffer
 * and enter an interactive navigation loop.
 *
 * Navigation:
 *   Up / W   - scroll toward older output
 *   Down / S - scroll toward newer output
 *   Q        - exit scroll mode
 *
 * The bottom row shows a help bar while scroll mode is active.
 */
void vga_enter_scroll_mode(void) {
    /* Save the live screen before switching to scroll view */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t dest = (scrollback_current_line % SCROLLBACK_LINES) * VGA_WIDTH;
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[dest + x] = vga_buffer[y * VGA_WIDTH + x];
        }
        scrollback_current_line++;
    }

    scroll_offset      = 0;
    scroll_mode_active = 1;
    vga_disable_cursor();

    /* Draw help bar on the bottom row */
    uint8_t    help_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    const char *help_text = " UP/DOWN Arrows: Scroll | Q: Quit ";
    size_t      help_len  = strlen(help_text);
    size_t      start_x   = (VGA_WIDTH - help_len) / 2;

    for (size_t i = 0; i < help_len && (start_x + i) < VGA_WIDTH; i++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + start_x + i] =
            vga_entry((unsigned char)help_text[i], help_color);
    }

    /* Interactive navigation loop */
    while (scroll_mode_active) {
        uint8_t scan_code = keyboard_read_scan_code();

        if (scan_code == 0xE0) {
            /* Extended key prefix: read the actual key code */
            uint8_t ext = keyboard_read_scan_code();
            if (ext == 0x48) vga_scroll_up();
            else if (ext == 0x50) vga_scroll_down();
            continue;
        }

        char c = scan_code_to_ascii(scan_code);
        if (c == 0) continue;

        if (c == 'q' || c == 'Q') {
            scroll_mode_active = 0;
        } else if (c == 'w' || c == 'W') {
            vga_scroll_up();
        } else if (c == 's' || c == 'S') {
            vga_scroll_down();
        }
    }

    /* Restore normal operation */
    scroll_offset = 0;
    vga_enable_cursor(14, 15);
    vga_clear();
    vga_writestring("Exited scroll mode\n");
}

/* =========================================================================
 * Cursor control
 * ======================================================================= */

/*
 * vga_enable_cursor - program the CRT controller to show the cursor as a
 * block between scan lines cursor_start and cursor_end.
 */
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (uint8_t)((inb(0x3D5) & 0xC0) | cursor_start));
    outb(0x3D4, 0x0B);
    outb(0x3D5, (uint8_t)((inb(0x3D5) & 0xE0) | cursor_end));
}

/*
 * vga_disable_cursor - hide the hardware cursor by setting bit 5 of the
 * Cursor Start register.
 */
void vga_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

/*
 * vga_update_cursor - move the hardware cursor to column x, row y.
 * The position is written as a 16-bit linear offset to the CRT controller.
 */
void vga_update_cursor(int x, int y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

/*
 * vga_get_cursor - store the current cursor column in *x and row in *y.
 * Either pointer may be NULL.
 */
void vga_get_cursor(int *x, int *y) {
    if (x) *x = (int)vga_column;
    if (y) *y = (int)vga_row;
}

/*
 * vga_set_cursor - move the software cursor to (x, y) and update the
 * hardware cursor position.  Out-of-bounds coordinates are ignored.
 */
void vga_set_cursor(int x, int y) {
    if (x >= 0 && x < (int)VGA_WIDTH &&
        y >= 0 && y < (int)VGA_HEIGHT) {
        vga_column = (size_t)x;
        vga_row    = (size_t)y;
        vga_update_cursor(x, y);
    }
}

/* =========================================================================
 * Positioned and region writes
 * ======================================================================= */

/*
 * vga_putchar_at - write character c with attribute color at screen position
 * (x, y) without affecting the cursor.
 */
void vga_putchar_at(char c, uint8_t color, int x, int y) {
    if (x >= 0 && x < (int)VGA_WIDTH &&
        y >= 0 && y < (int)VGA_HEIGHT) {
        vga_buffer[y * VGA_WIDTH + x] = vga_entry((unsigned char)c, color);
    }
}

/*
 * vga_write_at - write a null-terminated string starting at (x, y) with
 * the given color.  Newlines advance the row; characters that exceed
 * VGA_WIDTH wrap to orig_x on the next row.
 */
void vga_write_at(const char *str, uint8_t color, int x, int y) {
    int orig_x = x;
    while (*str && y < (int)VGA_HEIGHT) {
        if (*str == '\n') {
            y++;
            x = orig_x;
        } else {
            vga_putchar_at(*str, color, x++, y);
            if (x >= (int)VGA_WIDTH) { x = orig_x; y++; }
        }
        str++;
    }
}

/*
 * vga_fill_rect - fill a width x height rectangle of cells with character c
 * and attribute color.  Clips to screen boundaries.
 */
void vga_fill_rect(char c, uint8_t color, int x, int y,
                   int width, int height) {
    for (int dy = 0; dy < height && y + dy < (int)VGA_HEIGHT; dy++) {
        for (int dx = 0; dx < width && x + dx < (int)VGA_WIDTH; dx++) {
            vga_putchar_at(c, color, x + dx, y + dy);
        }
    }
}

/*
 * vga_draw_box - draw an ASCII-art box with corners '+', horizontal edges
 * '-', and vertical edges '|' at position (x, y) with the given dimensions.
 */
void vga_draw_box(int x, int y, int width, int height, uint8_t color) {
    /* Corners */
    vga_putchar_at('+', color, x,             y);
    vga_putchar_at('+', color, x + width - 1, y);
    vga_putchar_at('+', color, x,             y + height - 1);
    vga_putchar_at('+', color, x + width - 1, y + height - 1);

    /* Top and bottom edges */
    for (int i = 1; i < width - 1; i++) {
        vga_putchar_at('-', color, x + i, y);
        vga_putchar_at('-', color, x + i, y + height - 1);
    }

    /* Left and right edges */
    for (int i = 1; i < height - 1; i++) {
        vga_putchar_at('|', color, x,             y + i);
        vga_putchar_at('|', color, x + width - 1, y + i);
    }
}

/*
 * vga_print_progress - draw a progress bar of the given width at (x, y).
 *
 * Format: [====      ]
 *   percentage: 0-100
 *   Filled cells use LIGHT_GREEN; empty cells use DARK_GREY.
 */
void vga_print_progress(int percentage, int x, int y, int width) {
    uint8_t bar_color   = vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK);
    uint8_t empty_color = vga_entry_color(VGA_COLOR_DARK_GREY,   VGA_COLOR_BLACK);
    int     filled      = (percentage * width) / 100;

    vga_putchar_at('[', VGA_COLOR_WHITE, x, y);
    for (int i = 0; i < width; i++) {
        char    ch  = (i < filled) ? '=' : ' ';
        uint8_t col = (i < filled) ? bar_color : empty_color;
        vga_putchar_at(ch, col, x + 1 + i, y);
    }
    vga_putchar_at(']', VGA_COLOR_WHITE, x + width + 1, y);
}