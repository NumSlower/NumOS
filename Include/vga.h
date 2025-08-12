#ifndef VGA_H
#define VGA_H

#include <stdint.h>
#include <stddef.h>

/* VGA text mode constants */
#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define VGA_MEMORY 0xB8000

/* VGA colors */
typedef enum {
    VGA_COLOR_BLACK = 0,
    VGA_COLOR_BLUE = 1,
    VGA_COLOR_GREEN = 2,
    VGA_COLOR_CYAN = 3,
    VGA_COLOR_RED = 4,
    VGA_COLOR_MAGENTA = 5,
    VGA_COLOR_BROWN = 6,
    VGA_COLOR_LIGHT_GREY = 7,
    VGA_COLOR_DARK_GREY = 8,
    VGA_COLOR_LIGHT_BLUE = 9,
    VGA_COLOR_LIGHT_GREEN = 10,
    VGA_COLOR_LIGHT_CYAN = 11,
    VGA_COLOR_LIGHT_RED = 12,
    VGA_COLOR_LIGHT_MAGENTA = 13,
    VGA_COLOR_LIGHT_BROWN = 14,
    VGA_COLOR_WHITE = 15,
} vga_color_t;

/* VGA functions */
void vga_init(void);
void vga_clear(void);
void vga_setcolor(uint8_t color);
void vga_putchar(char c);
void vga_write(const char *data, size_t size);
void vga_writestring(const char *data);
void vga_scroll(void);
void vga_newline(void);
uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg);
uint16_t vga_entry(unsigned char uc, uint8_t color);

/* Cursor functions */
void vga_update_cursor(int x, int y);
void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end);
void vga_disable_cursor(void);

/* I/O port functions */
uint8_t inb(uint16_t port);
void outb(uint16_t port, uint8_t val);

#endif /* VGA_H */