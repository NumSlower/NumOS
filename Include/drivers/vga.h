#ifndef VGA_H
#define VGA_H

#include "lib/base.h"

/* VGA text mode constants */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

/*
 * VGA text-mode colour model
 * ===========================
 * The hardware attribute byte is split into two nibbles:
 *
 *   Bit 7        : Background-intensity / blink (hardware default = blink)
 *   Bits 6-4     : Background colour index (0-7)
 *   Bits 3-0     : Foreground colour index (0-15)
 *
 * By disabling the blink bit in the VGA sequencer (done in vga_init via
 * vga_disable_blink()) the MSB of the attribute byte becomes a 4th
 * background-colour bit, giving 16 foreground × 16 background = 256
 * unique colour combinations — the maximum text-mode supports.
 *
 * Without that register write you are limited to 16 fg × 8 bg = 128
 * combinations (the top 8 bg colours cause characters to blink instead).
 *
 * Call vga_disable_blink() once (already called inside vga_init()) to
 * unlock all 16 background colours.  If you ever need blinking text back,
 * call vga_enable_blink() and remember that background indices 8-15 will
 * again cause blinking rather than showing the bright colour.
 */

/* All 16 VGA palette entries (foreground always, background after blink disable) */
typedef enum {
    /* Dark colours  (index 0-7) */
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,

    /* Bright colours (index 8-15) */
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN   = 14,   /* also called "yellow" */
    VGA_COLOR_WHITE         = 15,
} vga_color_t;

/* Handy alias */
#define VGA_COLOR_YELLOW  VGA_COLOR_LIGHT_BROWN
#define VGA_COLOR_ORANGE  VGA_COLOR_BROWN        /* closest VGA approximation */

/* ── Colour attribute helpers ─────────────────────────────────────────────
 *
 * vga_entry_color(fg, bg)
 *   Build a 1-byte attribute from any of the 16 fg and 16 bg colours.
 *   After vga_disable_blink() all 256 combinations render correctly.
 *
 * vga_entry(char, color)
 *   Combine a character and its attribute into the 16-bit word the VGA
 *   buffer expects.
 */
uint8_t  vga_entry_color(vga_color_t fg, vga_color_t bg);
uint16_t vga_entry(unsigned char uc, uint8_t color);

/* ── Blink / background-intensity control ─────────────────────────────────
 *
 * vga_disable_blink()  — switch bit 7 of the attribute byte from "blink"
 *                         to "background intensity" (enables 16 bg colours).
 *                         Called automatically by vga_init().
 *
 * vga_enable_blink()   — restore the hardware default (blink mode).
 *                         Background colours 8-15 will cause blinking again.
 */
void vga_disable_blink(void);
void vga_enable_blink(void);

/* ── Core VGA functions ───────────────────────────────────────────────── */
void vga_init(void);
void vga_clear(void);
void vga_setcolor(uint8_t color);
void vga_putchar(char c);
void vga_write(const char *data, size_t size);
void vga_writestring(const char *data);
void vga_scroll(void);
void vga_newline(void);

/* ── Scrolling functions ──────────────────────────────────────────────── */
void vga_scroll_up(void);
void vga_scroll_down(void);
void vga_enter_scroll_mode(void);
void vga_save_screen_to_scrollback(void);

/* ── Cursor functions ─────────────────────────────────────────────────── */
void vga_update_cursor(int x, int y);
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_disable_cursor(void);
void vga_get_cursor(int *x, int *y);
void vga_set_cursor(int x, int y);

/* ── Advanced display functions ───────────────────────────────────────── */
void vga_putchar_at(char c, uint8_t color, int x, int y);
void vga_write_at(const char *str, uint8_t color, int x, int y);
void vga_fill_rect(char c, uint8_t color, int x, int y, int width, int height);
void vga_draw_box(int x, int y, int width, int height, uint8_t color);
void vga_print_progress(int percentage, int x, int y, int width);

/* ── Colour stack ─────────────────────────────────────────────────────── */
void vga_push_color(void);
void vga_pop_color(void);

/* ── I/O port helpers (also used by other drivers) ───────────────────── */
uint8_t inb(uint16_t port);
void    outb(uint16_t port, uint8_t val);

#endif /* VGA_H */