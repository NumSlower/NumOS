#include "drivers/graphices/vga.h"
#include "drivers/serial.h"

static uint8_t current_color = 0x07;
static vga_output_hook_t output_hook = 0;

uint8_t vga_entry_color(vga_color_t fg, vga_color_t bg) {
    return (uint8_t)fg | ((uint8_t)bg << 4);
}

uint16_t vga_entry(unsigned char uc, uint8_t color) {
    return (uint16_t)uc | ((uint16_t)color << 8);
}

void vga_disable_blink(void) {
}

void vga_init(void) {
    serial_init();
}

void vga_clear(void) {
    serial_write("\n");
}

void vga_setcolor(uint8_t color) {
    current_color = color;
    (void)current_color;
}

void vga_putchar(char c) {
    serial_putc(c);
    if (output_hook) output_hook(c);
}

void vga_write(const char *data, size_t size) {
    if (!data) return;
    for (size_t i = 0; i < size; i++) vga_putchar(data[i]);
}

void vga_writestring(const char *data) {
    if (!data) return;
    while (*data) vga_putchar(*data++);
}

void vga_scroll(void) {
}

void vga_newline(void) {
    vga_putchar('\n');
}

void vga_scroll_up(void) {
}

void vga_scroll_down(void) {
}

void vga_enter_scroll_mode(void) {
}

void vga_update_cursor(int x, int y) {
    (void)x;
    (void)y;
}

void vga_enable_cursor(uint8_t cursor_start, uint8_t cursor_end) {
    (void)cursor_start;
    (void)cursor_end;
}

void vga_disable_cursor(void) {
}

void vga_putchar_at(char c, uint8_t color, int x, int y) {
    (void)color;
    (void)x;
    (void)y;
    vga_putchar(c);
}

void vga_set_fast_mode(int enabled) {
    (void)enabled;
}

void vga_set_output_hook(vga_output_hook_t hook) {
    output_hook = hook;
}
