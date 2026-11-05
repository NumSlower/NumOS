/* NumOS — vga.c — VGA Text Mode */
#include "drivers/vga.h"
#include "libs/io.h"
#include "libs/string.h"

static uint16_t* vga_buffer = (uint16_t*)0xB8000;
static uint8_t vga_col = 0;
static uint8_t vga_row = 0;
static uint8_t vga_fg = WHITE;
static uint8_t vga_bg = BLACK;

static inline uint8_t vga_entry_color(uint8_t fg, uint8_t bg) {
    return fg | (bg << 4);
}

static inline uint16_t vga_entry(char c, uint8_t color) {
    return (uint16_t)c | ((uint16_t)color << 8);
}

void vga_init(void) {
    vga_enable_cursor(14, 15);
    vga_clear();
}

void vga_clear(void) {
    uint8_t color = vga_entry_color(vga_fg, vga_bg);
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[y * VGA_WIDTH + x] = vga_entry(' ', color);
        }
    }
    vga_col = 0;
    vga_row = 0;
    vga_set_cursor(vga_col, vga_row);
}

void vga_set_color(uint8_t fg, uint8_t bg) {
    vga_fg = fg;
    vga_bg = bg;
}

void vga_putchar(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
    } else if (c == '\r') {
        vga_col = 0;
    } else if (c == '\b') {
        if (vga_col > 0) {
            vga_col--;
        } else if (vga_row > 0) {
            vga_row--;
            vga_col = VGA_WIDTH - 1;
        }
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(' ', vga_entry_color(vga_fg, vga_bg));
    } else if (c == '\t') {
        vga_col = (vga_col + 8) & ~7;
    } else {
        vga_buffer[vga_row * VGA_WIDTH + vga_col] = vga_entry(c, vga_entry_color(vga_fg, vga_bg));
        vga_col++;
    }
    
    if (vga_col >= VGA_WIDTH) {
        vga_col = 0;
        vga_row++;
    }
    
    if (vga_row >= VGA_HEIGHT) {
        vga_scroll();
        vga_row = VGA_HEIGHT - 1;
    }
    
    vga_set_cursor(vga_col, vga_row);
}

void vga_puts(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

void vga_scroll(void) {
    memmove(vga_buffer, vga_buffer + VGA_WIDTH, (VGA_HEIGHT - 1) * VGA_WIDTH * 2);
    uint8_t color = vga_entry_color(vga_fg, vga_bg);
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = vga_entry(' ', color);
    }
}

void vga_set_cursor(uint8_t col, uint8_t row) {
    uint16_t pos = row * VGA_WIDTH + col;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_enable_cursor(uint8_t start, uint8_t end) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, (inb(0x3D5) & 0xC0) | start);
    outb(0x3D4, 0x0B);
    outb(0x3D5, (inb(0x3D5) & 0xE0) | end);
}

void vga_disable_cursor(void) {
    outb(0x3D4, 0x0A);
    outb(0x3D5, 0x20);
}

void vga_putchar_backend(char c) {
    vga_putchar(c);
}
