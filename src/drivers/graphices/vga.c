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

#include "drivers/graphices/vga.h"
#include "drivers/keyboard.h"
#include "drivers/timer.h"
#include "lib/string.h"

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

/* Optional secondary output sink (for example framebuffer console). */
static vga_output_hook_t vga_output_hook = NULL;
static int cursor_updates_enabled = 1;
static int vga_fast_mode = 0;
static int vga_bulk_write = 0;
static uint16_t vga_shadow[VGA_HEIGHT * VGA_WIDTH];
static uint8_t  vga_dirty[VGA_HEIGHT];

static void vga_update_cursor_hw(int x, int y);
static void vga_flush_row(size_t row);
static void vga_flush_all(void);

static void vga_mark_dirty(size_t row) {
    if (row < VGA_HEIGHT) vga_dirty[row] = 1;
}

static void vga_write_cell(size_t row, size_t col, uint16_t entry) {
    size_t idx = row * VGA_WIDTH + col;
    if (vga_fast_mode) {
        vga_shadow[idx] = entry;
        vga_mark_dirty(row);
    } else {
        vga_buffer[idx] = entry;
    }
}

void vga_set_output_hook(vga_output_hook_t hook) {
    vga_output_hook = hook;
}

/* =========================================================================
 * Attribute helpers
 * ======================================================================= */

/*
 * vga_entry_color - build a 1-byte attribute from foreground and background
 * colour indices.  Background occupies bits [7:4], foreground bits [3:0].
 * After vga_disable_blink() all 256 combinations render correctly.
 */
uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    if (bg == VGA_COLOR_BLACK) {
        fg = VGA_COLOR_WHITE;
    } else if (bg == VGA_COLOR_WHITE) {
        fg = VGA_COLOR_BLACK;
    }
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
    vga_text_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    vga_buffer = (uint16_t *)VGA_MEMORY;
    scrollback_current_line = 0;
    scroll_offset      = 0;
    scroll_mode_active = 0;

    vga_disable_blink();

    /* Zero the scrollback ring buffer */
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t i = 0; i < SCROLLBACK_LINES * VGA_WIDTH; i++) {
        scrollback_buffer[i] = blank;
    }
    for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        vga_shadow[i] = blank;
    }
    for (size_t i = 0; i < VGA_HEIGHT; i++) vga_dirty[i] = 1;

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
        if (vga_fast_mode) vga_shadow[i] = blank;
        else vga_buffer[i] = blank;
    }
    if (vga_fast_mode) {
        for (size_t i = 0; i < VGA_HEIGHT; i++) vga_dirty[i] = 1;
        vga_flush_all();
    }
    vga_row    = 0;
    vga_column = 0;
    vga_update_cursor(0, 0);

    if (vga_output_hook) vga_output_hook('\f');
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
            if (vga_output_hook) vga_output_hook('\n');
            return;

        case '\r':
            vga_column = 0;
            vga_update_cursor((int)vga_column, (int)vga_row);
            if (vga_output_hook) vga_output_hook('\r');
            return;

        case '\b':
            if (vga_column > 0) {
                vga_column--;
                vga_write_cell(vga_row, vga_column,
                               vga_entry(' ', vga_text_color));
                vga_update_cursor((int)vga_column, (int)vga_row);
                if (vga_fast_mode) vga_flush_row(vga_row);
                if (vga_output_hook) vga_output_hook('\b');
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

    vga_putchar_at(c, vga_text_color, (int)vga_column, (int)vga_row);

    if (++vga_column == VGA_WIDTH) {
        vga_newline();
    } else {
        vga_update_cursor((int)vga_column, (int)vga_row);
    }

    if (vga_output_hook) vga_output_hook(c);
}

/*
 * vga_write - write size bytes from data to the screen.
 */
void vga_write(const char *data, size_t size) {
    int prev = cursor_updates_enabled;
    cursor_updates_enabled = 0;
    int prev_bulk = vga_bulk_write;
    vga_bulk_write = 1;
    for (size_t i = 0; i < size; i++) vga_putchar(data[i]);
    vga_bulk_write = prev_bulk;
    cursor_updates_enabled = prev;
    if (prev) vga_update_cursor((int)vga_column, (int)vga_row);
    if (vga_fast_mode) vga_flush_row(vga_row);
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
    if (vga_fast_mode) vga_flush_row(vga_row);
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
    if (vga_fast_mode) {
        memmove(vga_shadow,
                vga_shadow + VGA_WIDTH,
                (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t));
        uint16_t blank = vga_entry(' ', vga_text_color);
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_shadow[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
        }
        for (size_t i = 0; i < VGA_HEIGHT; i++) vga_dirty[i] = 1;
        vga_flush_all();
        return;
    }

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
            vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE);

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
    if (vga_fast_mode) {
        vga_writestring("\nScroll mode disabled in fast mode\n");
        return;
    }
    size_t live_row = vga_row;
    size_t live_column = vga_column;
    uint64_t next_repeat_ms = 0;
    char repeat_key = 0;
    const uint64_t first_repeat_delay_ms = 180;
    const uint64_t repeat_period_ms = 28;

    /* Save the live screen into the scrollback ring */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t dest = (scrollback_current_line % SCROLLBACK_LINES) * VGA_WIDTH;
        for (size_t x = 0; x < VGA_WIDTH; x++)
            scrollback_buffer[dest + x] = vga_buffer[y * VGA_WIDTH + x];
        scrollback_current_line++;
    }
 
    scroll_offset      = 0;
    scroll_mode_active = 1;
    vga_disable_cursor();
    keyboard_flush_buffer();
 
    /* Draw help bar at the bottom */
    uint8_t    help_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_WHITE);
    const char *help_text = " UP/DOWN Arrows or W/S: Scroll | Q: Quit ";
    size_t      help_len  = strlen(help_text);
    size_t      start_x   = (VGA_WIDTH > help_len) ? (VGA_WIDTH - help_len) / 2 : 0;
 
    for (size_t i = 0; i < help_len && (start_x + i) < VGA_WIDTH; i++)
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + start_x + i] =
            vga_entry((unsigned char)help_text[i], help_color);
 
    /* ---------------------------------------------------------------
     * Interactive navigation loop.
     *
     * keyboard_getchar_buffered() waits on the IRQ ring buffer.
     * The IRQ handler converts the two-byte arrow sequence
     * (0xE0, 0x48/0x50) into KEY_SPECIAL_UP / KEY_SPECIAL_DOWN before
     * placing it in the buffer, so we receive a single char here with
     * no need to handle the raw multi-byte protocol ourselves.
     * --------------------------------------------------------------- */
    while (scroll_mode_active) {
        int had_input = 0;
        int did_repeat = 0;
        char c = 0;

        while (keyboard_try_getchar(&c)) {
            uint64_t now = timer_get_uptime_ms();
            had_input = 1;
            switch (c) {
                case KEY_SPECIAL_UP:
                    vga_scroll_up();
                    repeat_key = KEY_SPECIAL_UP;
                    next_repeat_ms = now + first_repeat_delay_ms;
                    break;
                case KEY_SPECIAL_DOWN:
                    vga_scroll_down();
                    repeat_key = KEY_SPECIAL_DOWN;
                    next_repeat_ms = now + first_repeat_delay_ms;
                    break;
                case 'w': case 'W':
                    vga_scroll_up();
                    repeat_key = 0;
                    break;
                case 's': case 'S':
                    vga_scroll_down();
                    repeat_key = 0;
                    break;
                case 'q': case 'Q':
                    scroll_mode_active = 0;
                    break;
                default:
                    break;
            }
            if (!scroll_mode_active) break;
        }

        if (!scroll_mode_active) break;

        uint64_t now = timer_get_uptime_ms();
        int up_pressed = keyboard_is_special_pressed(KEY_SPECIAL_UP);
        int down_pressed = keyboard_is_special_pressed(KEY_SPECIAL_DOWN);

        if (up_pressed && !down_pressed) {
            if (repeat_key != KEY_SPECIAL_UP) {
                repeat_key = KEY_SPECIAL_UP;
                next_repeat_ms = now + first_repeat_delay_ms;
            } else if (now >= next_repeat_ms) {
                vga_scroll_up();
                next_repeat_ms = now + repeat_period_ms;
                did_repeat = 1;
            }
        } else if (down_pressed && !up_pressed) {
            if (repeat_key != KEY_SPECIAL_DOWN) {
                repeat_key = KEY_SPECIAL_DOWN;
                next_repeat_ms = now + first_repeat_delay_ms;
            } else if (now >= next_repeat_ms) {
                vga_scroll_down();
                next_repeat_ms = now + repeat_period_ms;
                did_repeat = 1;
            }
        } else {
            repeat_key = 0;
            next_repeat_ms = 0;
        }

        if (!had_input && !did_repeat) {
            __asm__ volatile("sti; hlt; cli" ::: "memory");
        }
    }
 
    /* Restore normal operation */
    scroll_offset = 0;
    vga_redraw_from_scrollback();
    vga_row = live_row;
    vga_column = live_column;
    vga_enable_cursor(14, 15);
    vga_update_cursor((int)vga_column, (int)vga_row);
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
static void vga_update_cursor_hw(int x, int y) {
    uint16_t pos = (uint16_t)(y * VGA_WIDTH + x);
    outb(0x3D4, 0x0F); outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E); outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_update_cursor(int x, int y) {
    if (!cursor_updates_enabled || vga_fast_mode) return;
    vga_update_cursor_hw(x, y);
}

void vga_set_fast_mode(int enabled) {
    vga_fast_mode = enabled ? 1 : 0;
    if (vga_fast_mode) {
        for (size_t i = 0; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            vga_shadow[i] = vga_buffer[i];
        }
        for (size_t i = 0; i < VGA_HEIGHT; i++) vga_dirty[i] = 0;
        vga_disable_cursor();
    } else {
        vga_enable_cursor(14, 15);
    }
}

static void vga_flush_row(size_t row) {
    if (!vga_fast_mode) return;
    if (row >= VGA_HEIGHT) return;
    if (!vga_dirty[row]) return;
    size_t off = row * VGA_WIDTH;
    memcpy(&vga_buffer[off], &vga_shadow[off], VGA_WIDTH * sizeof(uint16_t));
    vga_dirty[row] = 0;
}

static void vga_flush_all(void) {
    if (!vga_fast_mode) return;
    for (size_t row = 0; row < VGA_HEIGHT; row++) vga_flush_row(row);
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
        vga_write_cell((size_t)y, (size_t)x, vga_entry((unsigned char)c, color));
        if (vga_fast_mode && !vga_bulk_write) vga_flush_row((size_t)y);
    }
}
