#ifndef VGA_H
#define VGA_H

#include "lib/base.h"

/* VGA text mode constants */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

/*
 * VGA text-mode colour model
 * After vga_disable_blink() bit 7 of the attribute byte becomes background
 * intensity, unlocking all 16 bg colours (256 combinations total).
 */
typedef enum {
    VGA_COLOR_BLACK         = 0,
    VGA_COLOR_BLUE          = 1,
    VGA_COLOR_GREEN         = 2,
    VGA_COLOR_CYAN          = 3,
    VGA_COLOR_RED           = 4,
    VGA_COLOR_MAGENTA       = 5,
    VGA_COLOR_BROWN         = 6,
    VGA_COLOR_LIGHT_GREY    = 7,
    VGA_COLOR_DARK_GREY     = 8,
    VGA_COLOR_LIGHT_BLUE    = 9,
    VGA_COLOR_LIGHT_GREEN   = 10,
    VGA_COLOR_LIGHT_CYAN    = 11,
    VGA_COLOR_LIGHT_RED     = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN   = 14,
    VGA_COLOR_WHITE         = 15,
} vga_color_t;

#define VGA_COLOR_YELLOW  VGA_COLOR_LIGHT_BROWN
#define VGA_COLOR_ORANGE  VGA_COLOR_BROWN

/* Attribute / entry helpers */
uint8_t  vga_entry_color(vga_color_t fg, vga_color_t bg);
uint16_t vga_entry(unsigned char uc, uint8_t color);

/* Blink control */
void vga_disable_blink(void);
void vga_enable_blink(void);

/* Core functions */
void vga_init(void);
void vga_clear(void);
void vga_setcolor(uint8_t color);
void vga_putchar(char c);
void vga_write(const char *data, size_t size);
void vga_writestring(const char *data);
void vga_scroll(void);
void vga_newline(void);

/* Scrollback */
void vga_scroll_up(void);
void vga_scroll_down(void);
void vga_enter_scroll_mode(void);
void vga_save_screen_to_scrollback(void);

/* Cursor */
void vga_update_cursor(int x, int y);
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_disable_cursor(void);
void vga_get_cursor(int *x, int *y);
void vga_set_cursor(int x, int y);

/* Positioned / region writes */
void vga_putchar_at(char c, uint8_t color, int x, int y);
void vga_write_at(const char *str, uint8_t color, int x, int y);
void vga_fill_rect(char c, uint8_t color, int x, int y, int width, int height);
void vga_draw_box(int x, int y, int width, int height, uint8_t color);
void vga_print_progress(int percentage, int x, int y, int width);

/* Colour stack */
void vga_push_color(void);
void vga_pop_color(void);

/* =========================================================================
 * Output hook — optional secondary output channel.
 *
 * When the framebuffer terminal is active it registers a callback here so
 * every vga_putchar() call also appears in the framebuffer console.
 * This makes all kernel subsystems (fat32_list_directory, heap_print_stats,
 * scheduler_print_processes, etc.) automatically visible in graphics mode
 * without any changes to those drivers.
 *
 * Call vga_set_output_hook(NULL) to detach.
 * ======================================================================= */
typedef void (*vga_output_hook_t)(char c);
void vga_set_output_hook(vga_output_hook_t hook);

/* I/O port helpers */
uint8_t inb(uint16_t port);
void    outb(uint16_t port, uint8_t val);

#endif /* VGA_H */