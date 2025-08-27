#include "drivers/vga.h"
#include "kernel.h"

static size_t vga_row;
static size_t vga_column;
static uint8_t vga_text_color;
static uint16_t* vga_buffer;

uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    return fg | bg << 4;
}

uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t) uc | (uint16_t) color << 8;
}

void vga_init(void) {
    vga_row = 0;
    vga_column = 0;
    vga_text_color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
    vga_buffer = (uint16_t*) VGA_MEMORY;
    vga_clear();
    vga_enable_cursor(14, 15);
}

void vga_clear(void) {
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t index = y * VGA_WIDTH + x;
            vga_buffer[index] = vga_entry(' ', vga_text_color);
        }
    }
    vga_row = 0;
    vga_column = 0;
    vga_update_cursor(0, 0);
}

void vga_setcolor(uint8_t color) {
    vga_text_color = color;
}

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
            const size_t index = vga_row * VGA_WIDTH + vga_column;
            vga_buffer[index] = vga_entry(' ', vga_text_color);
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
    
    const size_t index = vga_row * VGA_WIDTH + vga_column;
    vga_buffer[index] = vga_entry(c, vga_text_color);
    
    if (++vga_column == VGA_WIDTH) {
        vga_newline();
    } else {
        vga_update_cursor(vga_column, vga_row);
    }
}

void vga_write(const char* data, size_t size) {
    for (size_t i = 0; i < size; i++) {
        vga_putchar(data[i]);
    }
}

void vga_writestring(const char* data) {
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

void vga_scroll(void) {
    // Move all lines up by one
    for (size_t y = 1; y < VGA_HEIGHT; y++) {
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            const size_t src_index = y * VGA_WIDTH + x;
            const size_t dst_index = (y - 1) * VGA_WIDTH + x;
            vga_buffer[dst_index] = vga_buffer[src_index];
        }
    }
    
    // Clear the last line
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        const size_t index = (VGA_HEIGHT - 1) * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(' ', vga_text_color);
    }
}

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    // Enable cursor and set shape
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
    uint16_t pos = y * VGA_WIDTH + x;
    
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

uint8_t inb(uint16_t port) {
    uint8_t result;
    __asm__("in %%dx, %%al" : "=a" (result) : "d" (port));
    return result;
}

void outb(uint16_t port, uint8_t val) {
    __asm__("out %%al, %%dx" : : "a" (val), "d" (port));
}