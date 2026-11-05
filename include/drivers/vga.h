/* NumOS — vga.h — VGA Text Mode */
#ifndef NUMOS_VGA_H
#define NUMOS_VGA_H

#include "libs/types.h"

#define VGA_WIDTH  80
#define VGA_HEIGHT 25

enum vga_color {
    BLACK = 0, BLUE = 1, GREEN = 2, CYAN = 3, RED = 4, MAGENTA = 5, BROWN = 6, LIGHT_GREY = 7,
    DARK_GREY = 8, LIGHT_BLUE = 9, LIGHT_GREEN = 10, LIGHT_CYAN = 11, LIGHT_RED = 12, LIGHT_MAGENTA = 13, YELLOW = 14, WHITE = 15
};

void vga_init(void);
void vga_clear(void);
void vga_set_color(uint8_t fg, uint8_t bg);
void vga_putchar(char c);
void vga_puts(const char* str);
void vga_scroll(void);
void vga_set_cursor(uint8_t col, uint8_t row);
void vga_enable_cursor(uint8_t start, uint8_t end);
void vga_disable_cursor(void);
void vga_putchar_backend(char c);

#endif
