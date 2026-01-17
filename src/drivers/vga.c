/*
 * vga.c - VGA text mode driver with scrollback buffer
 */

#include "drivers/vga.h"
#include "drivers/keyboard.h"
#include "lib/string.h"
#include "kernel/kernel.h"

static size_t vga_row;
static size_t vga_column;
static uint8_t vga_text_color;
static uint16_t* vga_buffer;

/* Scrollback buffer */
#define SCROLLBACK_LINES 200
static uint16_t scrollback_buffer[SCROLLBACK_LINES * VGA_WIDTH];
static size_t scrollback_current_line = 0;
static int scroll_offset = 0;
static int scroll_mode_active = 0;

/* Color stack for nested color changes */
#define COLOR_STACK_SIZE 8
static uint8_t color_stack[COLOR_STACK_SIZE];
static int color_stack_top = -1;

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
    color_stack_top = -1;
    scrollback_current_line = 0;
    scroll_offset = 0;
    scroll_mode_active = 0;
    
    /* Clear scrollback buffer */
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t i = 0; i < SCROLLBACK_LINES * VGA_WIDTH; i++) {
        scrollback_buffer[i] = blank;
    }
    
    vga_clear();
    vga_enable_cursor(14, 15);
}

void vga_clear(void) {
    uint16_t blank = vga_entry(' ', vga_text_color);
    
    /* Fast clear using word writes */
    for (size_t i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    
    vga_row = 0;
    vga_column = 0;
    vga_update_cursor(0, 0);
}

void vga_setcolor(uint8_t color) {
    vga_text_color = color;
}

/* Push current color onto stack */
void vga_push_color(void) {
    if (color_stack_top < COLOR_STACK_SIZE - 1) {
        color_stack[++color_stack_top] = vga_text_color;
    }
}

/* Pop color from stack */
void vga_pop_color(void) {
    if (color_stack_top >= 0) {
        vga_text_color = color_stack[color_stack_top--];
    }
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

/* Save current screen line to scrollback buffer */
static void save_line_to_scrollback(size_t line_num) {
    size_t scrollback_line = scrollback_current_line % SCROLLBACK_LINES;
    size_t src_offset = line_num * VGA_WIDTH;
    size_t dst_offset = scrollback_line * VGA_WIDTH;
    
    for (size_t i = 0; i < VGA_WIDTH; i++) {
        scrollback_buffer[dst_offset + i] = vga_buffer[src_offset + i];
    }
    
    scrollback_current_line++;
}

void vga_scroll(void) {
    /* Save the top line to scrollback before scrolling */
    save_line_to_scrollback(0);
    
    /* Use memmove for efficient block copy */
    size_t bytes_to_move = (VGA_HEIGHT - 1) * VGA_WIDTH * sizeof(uint16_t);
    memmove(vga_buffer, vga_buffer + VGA_WIDTH, bytes_to_move);
    
    /* Clear the last line */
    uint16_t blank = vga_entry(' ', vga_text_color);
    for (size_t x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + x] = blank;
    }
}

/* Force save current screen to scrollback */
void vga_save_screen_to_scrollback(void) {
    /* Save all visible lines to scrollback */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t scrollback_line = scrollback_current_line % SCROLLBACK_LINES;
        size_t src_offset = y * VGA_WIDTH;
        size_t dst_offset = scrollback_line * VGA_WIDTH;
        
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[dst_offset + x] = vga_buffer[src_offset + x];
        }
        
        scrollback_current_line++;
    }
}

/* Redraw screen from scrollback buffer */
static void vga_redraw_from_scrollback(void) {
    if (scrollback_current_line < VGA_HEIGHT) {
        /* Not enough history yet, just show what we have */
        for (size_t y = 0; y < VGA_HEIGHT; y++) {
            for (size_t x = 0; x < VGA_WIDTH; x++) {
                size_t scrollback_line = (y < scrollback_current_line) ? y : scrollback_current_line - 1;
                if (scrollback_line >= SCROLLBACK_LINES) scrollback_line = SCROLLBACK_LINES - 1;
                
                size_t src_offset = (scrollback_line % SCROLLBACK_LINES) * VGA_WIDTH;
                vga_buffer[y * VGA_WIDTH + x] = scrollback_buffer[src_offset + x];
            }
        }
        return;
    }
    
    /* Calculate which lines to display based on scroll offset */
    /* When scroll_offset is 0, we show the most recent lines (bottom of scrollback) */
    /* When scroll_offset increases, we go backwards in time */
    size_t total_lines = scrollback_current_line;
    
    /* Calculate the starting line to display */
    /* We want to show lines from (total_lines - VGA_HEIGHT - scroll_offset) onwards */
    size_t display_start;
    if (scroll_offset == 0) {
        /* Show the most recent VGA_HEIGHT lines */
        display_start = total_lines - VGA_HEIGHT;
    } else {
        display_start = total_lines - VGA_HEIGHT - scroll_offset;
    }
    
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t line_to_show = display_start + y;
        size_t scrollback_line = line_to_show % SCROLLBACK_LINES;
        size_t src_offset = scrollback_line * VGA_WIDTH;
        size_t dst_offset = y * VGA_WIDTH;
        
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[dst_offset + x] = scrollback_buffer[src_offset + x];
        }
    }
    
    /* Show scroll indicator in top-right corner */
    if (scroll_offset > 0) {
        uint8_t indicator_color = vga_entry_color(VGA_COLOR_BLACK, VGA_COLOR_LIGHT_GREY);
        char indicator[24];  /* Increased size for safety */
        /* Show current position */
        const char* prefix = " SCROLL ";
        int pos = 0;
        for (int i = 0; prefix[i] && pos < 23; i++) {
            indicator[pos++] = prefix[i];
        }
        
        /* Add line numbers */
        size_t current_top_line = total_lines - VGA_HEIGHT - scroll_offset + 1;
        
        /* Simple number to string */
        char num_buf[10];
        int num_pos = 0;
        size_t temp = current_top_line;
        if (temp == 0) {
            num_buf[num_pos++] = '0';
        } else {
            char reverse[10];
            int rev_pos = 0;
            while (temp > 0 && rev_pos < 9) {
                reverse[rev_pos++] = '0' + (temp % 10);
                temp /= 10;
            }
            for (int i = rev_pos - 1; i >= 0 && num_pos < 9; i--) {
                num_buf[num_pos++] = reverse[i];
            }
        }
        num_buf[num_pos] = '\0';
        
        for (int i = 0; i < num_pos && pos < 22; i++) {
            indicator[pos++] = num_buf[i];
        }
        
        if (pos < 23) {
            indicator[pos++] = ' ';
        }
        indicator[pos] = '\0';
        
        /* Display indicator */
        for (size_t i = 0; i < (size_t)pos && i < VGA_WIDTH; i++) {
            vga_buffer[VGA_WIDTH - pos + i] = vga_entry(indicator[i], indicator_color);
        }
    }
}

void vga_scroll_up(void) {
    /* Maximum scroll is to see the oldest lines */
    int max_scroll = (int)scrollback_current_line - VGA_HEIGHT;
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
        
        /* If we're back at the bottom, restore the live view */
        if (scroll_offset == 0) {
            /* Copy the current screen content back */
            for (size_t y = 0; y < VGA_HEIGHT; y++) {
                size_t line_num = scrollback_current_line - VGA_HEIGHT + y;
                size_t scrollback_line = line_num % SCROLLBACK_LINES;
                size_t src_offset = scrollback_line * VGA_WIDTH;
                size_t dst_offset = y * VGA_WIDTH;
                
                for (size_t x = 0; x < VGA_WIDTH; x++) {
                    vga_buffer[dst_offset + x] = scrollback_buffer[src_offset + x];
                }
            }
        }
    }
}

void vga_enter_scroll_mode(void) {
    /* First, save the current visible screen to scrollback */
    /* This ensures we don't lose the last screen of content */
    for (size_t y = 0; y < VGA_HEIGHT; y++) {
        size_t scrollback_line = scrollback_current_line % SCROLLBACK_LINES;
        size_t src_offset = y * VGA_WIDTH;
        size_t dst_offset = scrollback_line * VGA_WIDTH;
        
        for (size_t x = 0; x < VGA_WIDTH; x++) {
            scrollback_buffer[dst_offset + x] = vga_buffer[src_offset + x];
        }
        
        scrollback_current_line++;
    }
    
    /* Start scrolled all the way to the bottom */
    scroll_offset = 0;
    
    scroll_mode_active = 1;
    vga_disable_cursor();
    
    /* Show instructions at bottom */
    uint8_t help_color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);
    const char* help_text = " UP/DOWN Arrows: Scroll | Q: Quit ";
    size_t help_len = strlen(help_text);
    size_t start_x = (VGA_WIDTH - help_len) / 2;
    
    for (size_t i = 0; i < help_len && (start_x + i) < VGA_WIDTH; i++) {
        vga_buffer[(VGA_HEIGHT - 1) * VGA_WIDTH + start_x + i] = 
            vga_entry(help_text[i], help_color);
    }
    
    /* Main scroll loop */
    while (scroll_mode_active) {
        uint8_t scan_code = keyboard_read_scan_code();
        
        /* Check for extended scan codes (arrow keys start with 0xE0) */
        if (scan_code == 0xE0) {
            /* Read the next byte for the actual arrow key code */
            scan_code = keyboard_read_scan_code();
            
            /* Arrow key scan codes:
             * Up arrow: 0x48
             * Down arrow: 0x50
             * Left arrow: 0x4B
             * Right arrow: 0x4D
             */
            if (scan_code == 0x48) {  /* Up arrow */
                vga_scroll_up();
            } else if (scan_code == 0x50) {  /* Down arrow */
                vga_scroll_down();
            }
            continue;
        }
        
        /* Convert scan code to ASCII for regular keys */
        char c = scan_code_to_ascii(scan_code);
        
        if (c == 0) {
            continue;
        }
        
        /* Handle regular keys */
        if (c == 'q' || c == 'Q') {
            scroll_mode_active = 0;
            break;
        }
        
        /* Also support WASD as alternative */
        if (c == 'w' || c == 'W') {
            vga_scroll_up();
        } else if (c == 's' || c == 'S') {
            vga_scroll_down();
        }
    }
    
    /* Restore normal display */
    scroll_offset = 0;
    vga_enable_cursor(14, 15);
    vga_clear();
    vga_writestring("Exited scroll mode\n");
}

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
    uint16_t pos = y * VGA_WIDTH + x;
    
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t) (pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

/* Get current cursor position */
void vga_get_cursor(int *x, int *y) {
    if (x) *x = vga_column;
    if (y) *y = vga_row;
}

/* Set cursor position */
void vga_set_cursor(int x, int y) {
    if (x >= 0 && x < (int)VGA_WIDTH && y >= 0 && y < (int)VGA_HEIGHT) {
        vga_column = x;
        vga_row = y;
        vga_update_cursor(x, y);
    }
}

/* Write character at specific position without moving cursor */
void vga_putchar_at(char c, uint8_t color, int x, int y) {
    if (x >= 0 && x < (int)VGA_WIDTH && y >= 0 && y < (int)VGA_HEIGHT) {
        const size_t index = y * VGA_WIDTH + x;
        vga_buffer[index] = vga_entry(c, color);
    }
}

/* Write string at specific position */
void vga_write_at(const char *str, uint8_t color, int x, int y) {
    int orig_x = x;
    while (*str && y < (int)VGA_HEIGHT) {
        if (*str == '\n') {
            y++;
            x = orig_x;
        } else {
            vga_putchar_at(*str, color, x++, y);
            if (x >= (int)VGA_WIDTH) {
                x = orig_x;
                y++;
            }
        }
        str++;
    }
}

/* Fill rectangle with character */
void vga_fill_rect(char c, uint8_t color, int x, int y, int width, int height) {
    for (int dy = 0; dy < height && y + dy < (int)VGA_HEIGHT; dy++) {
        for (int dx = 0; dx < width && x + dx < (int)VGA_WIDTH; dx++) {
            vga_putchar_at(c, color, x + dx, y + dy);
        }
    }
}

/* Draw a simple box */
void vga_draw_box(int x, int y, int width, int height, uint8_t color) {
    /* Corners */
    vga_putchar_at('+', color, x, y);
    vga_putchar_at('+', color, x + width - 1, y);
    vga_putchar_at('+', color, x, y + height - 1);
    vga_putchar_at('+', color, x + width - 1, y + height - 1);
    
    /* Horizontal lines */
    for (int i = 1; i < width - 1; i++) {
        vga_putchar_at('-', color, x + i, y);
        vga_putchar_at('-', color, x + i, y + height - 1);
    }
    
    /* Vertical lines */
    for (int i = 1; i < height - 1; i++) {
        vga_putchar_at('|', color, x, y + i);
        vga_putchar_at('|', color, x + width - 1, y + i);
    }
}

/* Print progress bar */
void vga_print_progress(int percentage, int x, int y, int width) {
    uint8_t bar_color = vga_entry_color(VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    uint8_t empty_color = vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK);
    
    int filled = (percentage * width) / 100;
    
    vga_putchar_at('[', VGA_COLOR_WHITE, x, y);
    
    for (int i = 0; i < width; i++) {
        char c = (i < filled) ? '=' : ' ';
        uint8_t color = (i < filled) ? bar_color : empty_color;
        vga_putchar_at(c, color, x + 1 + i, y);
    }
    
    vga_putchar_at(']', VGA_COLOR_WHITE, x + width + 1, y);
}