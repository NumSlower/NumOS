/*
 * graphics.c - Graphics Mode Driver Implementation
 * Supports VESA graphics modes with framebuffer access
 */

#include "drivers/graphics.h"
#include "drivers/vga.h"
#include "cpu/heap.h"
#include "lib/string.h"
#include "kernel/kernel.h"

/* Graphics state */
static struct {
    int initialized;
    int active;
    graphics_mode_t current_mode;
    uint8_t *framebuffer;
    uint8_t *back_buffer;
    int double_buffering_enabled;
    int graphics_available;
} g_graphics = {0};

/* Simple 8x8 bitmap font (basic ASCII characters) */
static const uint8_t font_data[256 * 8] = {
    /* Space character (0x20) - simplified font atlas */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x20 */
    0x18, 0x3C, 0x3C, 0x18, 0x18, 0x00, 0x18, 0x00, /* 0x21 ! */
    0x66, 0x66, 0x66, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x22 " */
    0x6B, 0xFF, 0x6B, 0x6B, 0xFF, 0x6B, 0x00, 0x00, /* 0x23 # */
    0x18, 0x7C, 0xC6, 0x7C, 0x1E, 0xE0, 0x7C, 0x00, /* 0x24 $ */
    0xC6, 0xC6, 0x0C, 0x18, 0x30, 0x60, 0xC6, 0x00, /* 0x25 % */
    0x7C, 0xC6, 0x7C, 0x7C, 0xCE, 0xC6, 0x7E, 0x00, /* 0x26 & */
    0x18, 0x18, 0x30, 0x00, 0x00, 0x00, 0x00, 0x00, /* 0x27 ' */
    0x0C, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0C, 0x00, /* 0x28 ( */
    0x30, 0x18, 0x0C, 0x0C, 0x0C, 0x18, 0x30, 0x00, /* 0x29 ) */
    0x00, 0x66, 0x3C, 0xFF, 0x3C, 0x66, 0x00, 0x00, /* 0x2A * */
    0x00, 0x18, 0x18, 0x7E, 0x18, 0x18, 0x00, 0x00, /* 0x2B + */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30, /* 0x2C , */
    0x00, 0x00, 0x00, 0x7E, 0x00, 0x00, 0x00, 0x00, /* 0x2D - */
    0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00, /* 0x2E . */
    0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x80, 0x00, /* 0x2F / */
    0x7C, 0xC6, 0xCE, 0xD6, 0xE6, 0xC6, 0x7C, 0x00, /* 0x30 0 */
    0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x7E, 0x00, /* 0x31 1 */
    0x7C, 0xC6, 0x06, 0x0C, 0x18, 0x30, 0xFE, 0x00, /* 0x32 2 */
    0x7C, 0xC6, 0x06, 0x3C, 0x06, 0xC6, 0x7C, 0x00, /* 0x33 3 */
    0x0C, 0x1C, 0x3C, 0x6C, 0xFE, 0x0C, 0x1E, 0x00, /* 0x34 4 */
    0xFE, 0xC0, 0xFC, 0x06, 0x06, 0xC6, 0x7C, 0x00, /* 0x35 5 */
    0x38, 0x60, 0xC0, 0xFC, 0xC6, 0xC6, 0x7C, 0x00, /* 0x36 6 */
    0xFE, 0x06, 0x0C, 0x18, 0x30, 0x60, 0xC0, 0x00, /* 0x37 7 */
    0x7C, 0xC6, 0xC6, 0x7C, 0xC6, 0xC6, 0x7C, 0x00, /* 0x38 8 */
    0x7C, 0xC6, 0xC6, 0x7E, 0x06, 0x0C, 0x38, 0x00, /* 0x39 9 */
    0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x00, 0x00, /* 0x3A : */
    0x00, 0x18, 0x18, 0x00, 0x18, 0x18, 0x30, 0x00, /* 0x3B ; */
    0x06, 0x0C, 0x18, 0x30, 0x18, 0x0C, 0x06, 0x00, /* 0x3C < */
    0x00, 0x00, 0x7E, 0x00, 0x7E, 0x00, 0x00, 0x00, /* 0x3D = */
    0x60, 0x30, 0x18, 0x0C, 0x18, 0x30, 0x60, 0x00, /* 0x3E > */
    0x7C, 0xC6, 0x0C, 0x18, 0x18, 0x00, 0x18, 0x00, /* 0x3F ? */
};

/* Initialize graphics subsystem */
int graphics_init(void) {
    if (g_graphics.initialized) {
        return 0;
    }
    
    vga_writestring("Graphics: Initializing graphics driver\n");
    
    memset(&g_graphics, 0, sizeof(g_graphics));
    g_graphics.initialized = 1;
    
    /* Check if framebuffer is available from bootloader */
    /* This would be set by the bootloader (GRUB) via multiboot info */
    /* For now, we detect available graphics modes */
    
    vga_writestring("Graphics: Graphics driver initialized\n");
    return 0;
}

/* Check if graphics is available */
int graphics_is_available(void) {
    return g_graphics.graphics_available || g_graphics.framebuffer != NULL;
}

/* Check if graphics mode is currently active */
int graphics_is_active(void) {
    return g_graphics.active;
}

/* Set graphics mode */
int graphics_set_mode(uint16_t width, uint16_t height, uint8_t bpp) {
    if (!g_graphics.initialized) {
        graphics_init();
    }
    
    /* Validate parameters */
    if (width == 0 || height == 0 || (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)) {
        vga_writestring("Graphics: Invalid mode parameters\n");
        return -1;
    }
    
    if (width > GRAPHICS_MAX_WIDTH || height > GRAPHICS_MAX_HEIGHT) {
        vga_writestring("Graphics: Mode too large\n");
        return -1;
    }
    
    /* Store current mode */
    g_graphics.current_mode.width = width;
    g_graphics.current_mode.height = height;
    g_graphics.current_mode.bpp = bpp;
    g_graphics.current_mode.pitch = width * (bpp / 8);
    
    uint32_t framebuffer_size = g_graphics.current_mode.pitch * height;
    
    /* If framebuffer is already allocated, free it */
    if (g_graphics.framebuffer) {
        kfree(g_graphics.framebuffer);
    }
    if (g_graphics.back_buffer) {
        kfree(g_graphics.back_buffer);
    }
    
    /* Allocate framebuffer */
    g_graphics.framebuffer = kmalloc(framebuffer_size);
    if (!g_graphics.framebuffer) {
        vga_writestring("Graphics: Failed to allocate framebuffer\n");
        return -1;
    }
    
    /* Allocate back buffer if double buffering is enabled */
    if (g_graphics.double_buffering_enabled) {
        g_graphics.back_buffer = kmalloc(framebuffer_size);
        if (!g_graphics.back_buffer) {
            vga_writestring("Graphics: Failed to allocate back buffer\n");
            kfree(g_graphics.framebuffer);
            g_graphics.framebuffer = NULL;
            return -1;
        }
    }
    
    /* Set up pixel format based on bpp */
    switch (bpp) {
        case 8:
            g_graphics.current_mode.type = 0; /* Indexed color */
            break;
        case 16:
            g_graphics.current_mode.type = 1; /* Direct RGB (5-6-5) */
            g_graphics.current_mode.format.red_mask = 0xF800;
            g_graphics.current_mode.format.green_mask = 0x07E0;
            g_graphics.current_mode.format.blue_mask = 0x001F;
            break;
        case 24:
            g_graphics.current_mode.type = 1; /* Direct RGB (8-8-8) */
            g_graphics.current_mode.format.red_mask = 0xFF0000;
            g_graphics.current_mode.format.green_mask = 0x00FF00;
            g_graphics.current_mode.format.blue_mask = 0x0000FF;
            break;
        case 32:
            g_graphics.current_mode.type = 1; /* Direct ARGB (8-8-8-8) */
            g_graphics.current_mode.format.alpha_mask = 0xFF000000;
            g_graphics.current_mode.format.red_mask = 0x00FF0000;
            g_graphics.current_mode.format.green_mask = 0x0000FF00;
            g_graphics.current_mode.format.blue_mask = 0x000000FF;
            break;
    }
    
    vga_writestring("Graphics: Set mode ");
    print_dec(width);
    vga_writestring("x");
    print_dec(height);
    vga_writestring("x");
    print_dec(bpp);
    vga_writestring(" (");
    print_dec(framebuffer_size / 1024);
    vga_writestring(" KB)\n");
    
    return 0;
}

/* Get current graphics mode */
int graphics_get_current_mode(graphics_mode_t *mode) {
    if (!mode || !g_graphics.framebuffer) {
        return -1;
    }
    
    memcpy(mode, &g_graphics.current_mode, sizeof(graphics_mode_t));
    return 0;
}

/* Helper: Convert ARGB color to pixel format */
static uint32_t color_to_pixel(graphics_color_t color, uint8_t bpp) {
    if (bpp == 32) {
        return color;
    } else if (bpp == 24) {
        return color & 0x00FFFFFF;
    } else if (bpp == 16) {
        /* Convert ARGB8888 to RGB565 */
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    } else if (bpp == 8) {
        /* Convert to grayscale for indexed color */
        uint8_t r = (color >> 16) & 0xFF;
        uint8_t g = (color >> 8) & 0xFF;
        uint8_t b = color & 0xFF;
        return ((r + g + b) / 3) >> 5; /* 3-bit per channel */
    }
    return color;
}

/* Put pixel to framebuffer */
void graphics_putpixel(uint16_t x, uint16_t y, graphics_color_t color) {
    if (!g_graphics.framebuffer || x >= g_graphics.current_mode.width || 
        y >= g_graphics.current_mode.height) {
        return;
    }
    
    uint8_t *fb = g_graphics.double_buffering_enabled ? 
                  g_graphics.back_buffer : g_graphics.framebuffer;
    
    uint32_t offset = (y * g_graphics.current_mode.pitch) + (x * (g_graphics.current_mode.bpp / 8));
    uint32_t pixel = color_to_pixel(color, g_graphics.current_mode.bpp);
    
    switch (g_graphics.current_mode.bpp) {
        case 8:
            fb[offset] = pixel & 0xFF;
            break;
        case 16:
            *(uint16_t*)(fb + offset) = pixel & 0xFFFF;
            break;
        case 24:
            fb[offset] = pixel & 0xFF;
            fb[offset + 1] = (pixel >> 8) & 0xFF;
            fb[offset + 2] = (pixel >> 16) & 0xFF;
            break;
        case 32:
            *(uint32_t*)(fb + offset) = pixel;
            break;
    }
}

/* Get pixel from framebuffer */
graphics_color_t graphics_getpixel(uint16_t x, uint16_t y) {
    if (!g_graphics.framebuffer || x >= g_graphics.current_mode.width || 
        y >= g_graphics.current_mode.height) {
        return 0;
    }
    
    uint8_t *fb = g_graphics.framebuffer;
    uint32_t offset = (y * g_graphics.current_mode.pitch) + (x * (g_graphics.current_mode.bpp / 8));
    
    uint32_t pixel = 0;
    switch (g_graphics.current_mode.bpp) {
        case 8:
            pixel = fb[offset];
            break;
        case 16:
            pixel = *(uint16_t*)(fb + offset);
            break;
        case 24:
            pixel = fb[offset] | (fb[offset + 1] << 8) | (fb[offset + 2] << 16);
            break;
        case 32:
            pixel = *(uint32_t*)(fb + offset);
            break;
    }
    
    return pixel;
}

/* Clear framebuffer */
void graphics_clear(graphics_color_t color) {
    if (!g_graphics.framebuffer) {
        return;
    }
    
    uint8_t *fb = g_graphics.double_buffering_enabled ? 
                  g_graphics.back_buffer : g_graphics.framebuffer;
    
    uint32_t pixel = color_to_pixel(color, g_graphics.current_mode.bpp);
    uint32_t size = g_graphics.current_mode.pitch * g_graphics.current_mode.height;
    
    /* Fast clear using word writes when possible */
    if (g_graphics.current_mode.bpp == 32) {
        uint32_t *ptr = (uint32_t*)fb;
        for (uint32_t i = 0; i < size / 4; i++) {
            ptr[i] = pixel;
        }
    } else {
        memset(fb, pixel, size);
    }
}

/* Fill rectangle */
void graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, graphics_color_t color) {
    if (!g_graphics.framebuffer) {
        return;
    }
    
    /* Clip rectangle */
    if (x >= g_graphics.current_mode.width || y >= g_graphics.current_mode.height) {
        return;
    }
    
    if (x + width > g_graphics.current_mode.width) {
        width = g_graphics.current_mode.width - x;
    }
    if (y + height > g_graphics.current_mode.height) {
        height = g_graphics.current_mode.height - y;
    }
    
    for (uint16_t row = y; row < y + height; row++) {
        for (uint16_t col = x; col < x + width; col++) {
            graphics_putpixel(col, row, color);
        }
    }
}

/* Draw line using Bresenham's algorithm */
void graphics_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, graphics_color_t color) {
    if (!g_graphics.framebuffer) {
        return;
    }
    
    int dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int sx = (x1 > x0) ? 1 : -1;
    int sy = (y1 > y0) ? 1 : -1;
    int err = (dx > dy) ? (dx / 2) : -(dy / 2);
    
    int x = x0, y = y0;
    while (1) {
        graphics_putpixel(x, y, color);
        
        if (x == x1 && y == y1) break;
        
        int e2 = err;
        if (e2 > -dx) { err -= dy; x += sx; }
        if (e2 <  dy) { err += dx; y += sy; }
    }
}

/* Draw unfilled rectangle */
void graphics_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, graphics_color_t color) {
    if (!g_graphics.framebuffer) {
        return;
    }
    
    /* Top edge */
    graphics_draw_line(x, y, x + width - 1, y, color);
    /* Bottom edge */
    graphics_draw_line(x, y + height - 1, x + width - 1, y + height - 1, color);
    /* Left edge */
    graphics_draw_line(x, y, x, y + height - 1, color);
    /* Right edge */
    graphics_draw_line(x + width - 1, y, x + width - 1, y + height - 1, color);
}

/* Draw circle using Midpoint Circle Algorithm */
void graphics_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, graphics_color_t color) {
    if (!g_graphics.framebuffer) {
        return;
    }
    
    int x = radius;
    int y = 0;
    int d = 3 - 2 * radius;
    
    while (x >= y) {
        graphics_putpixel(cx + x, cy + y, color);
        graphics_putpixel(cx - x, cy + y, color);
        graphics_putpixel(cx + x, cy - y, color);
        graphics_putpixel(cx - x, cy - y, color);
        graphics_putpixel(cx + y, cy + x, color);
        graphics_putpixel(cx - y, cy + x, color);
        graphics_putpixel(cx + y, cy - x, color);
        graphics_putpixel(cx - y, cy - x, color);
        
        if (d < 0) {
            d = d + 4 * y + 6;
        } else {
            d = d + 4 * (y - x) + 10;
            x--;
        }
        y++;
    }
}

/* Draw character from font */
void graphics_draw_char(uint16_t x, uint16_t y, char c, graphics_color_t fg, graphics_color_t bg) {
    if (!g_graphics.framebuffer || c < 0x20) {
        return;
    }
    
    /* For now, draw a simple 8x8 block for each character */
    graphics_fill_rect(x, y, 8, 8, bg);
    
    /* Simple character representation */
    if (c >= 0x20 && c < 0x7F) {
        uint32_t char_offset = (c - 0x20) * 8;
        
        for (int row = 0; row < 8; row++) {
            uint8_t byte = font_data[char_offset + row];
            
            for (int col = 0; col < 8; col++) {
                if (byte & (0x80 >> col)) {
                    graphics_putpixel(x + col, y + row, fg);
                }
            }
        }
    }
}

/* Draw string */
void graphics_draw_string(uint16_t x, uint16_t y, const char *str, graphics_color_t fg, graphics_color_t bg) {
    if (!g_graphics.framebuffer || !str) {
        return;
    }
    
    uint16_t cur_x = x;
    
    while (*str) {
        graphics_draw_char(cur_x, y, *str, fg, bg);
        cur_x += 8;
        
        if (cur_x >= g_graphics.current_mode.width) {
            cur_x = x;
            y += 8;
        }
        
        if (y >= g_graphics.current_mode.height) {
            break;
        }
        
        str++;
    }
}

/* Get framebuffer pointer */
uint8_t* graphics_get_framebuffer(void) {
    return g_graphics.framebuffer;
}

/* Get framebuffer size */
uint32_t graphics_get_framebuffer_size(void) {
    if (!g_graphics.framebuffer) {
        return 0;
    }
    return g_graphics.current_mode.pitch * g_graphics.current_mode.height;
}

/* Flip buffers (for double buffering) */
void graphics_flip_buffer(void) {
    if (!g_graphics.double_buffering_enabled || !g_graphics.back_buffer) {
        return;
    }
    
    /* Copy back buffer to front buffer */
    memcpy(g_graphics.framebuffer, g_graphics.back_buffer, graphics_get_framebuffer_size());
}

/* Switch to graphics mode */
int graphics_switch_to_graphics(uint16_t width, uint16_t height, uint8_t bpp) {
    vga_writestring("Graphics: Switching to graphics mode\n");
    
    int result = graphics_set_mode(width, height, bpp);
    if (result == 0) {
        g_graphics.active = 1;
        graphics_clear(GRAPHICS_COLOR_BLACK);
    }
    
    return result;
}

/* Switch back to text mode */
int graphics_switch_to_text(void) {
    vga_writestring("Graphics: Switching back to text mode\n");
    g_graphics.active = 0;
    return 0;
}

/* Set palette entry (for indexed color mode) */
void graphics_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    /* Palette operations would go to VGA DAC registers or similar */
    /* This is a stub for indexed color support */
    (void)index;
    (void)r;
    (void)g;
    (void)b;
}

/* Enable double buffering */
void graphics_enable_double_buffering(void) {
    if (!g_graphics.initialized) {
        graphics_init();
    }
    
    g_graphics.double_buffering_enabled = 1;
    vga_writestring("Graphics: Double buffering enabled\n");
}

/* Disable double buffering */
void graphics_disable_double_buffering(void) {
    g_graphics.double_buffering_enabled = 0;
    vga_writestring("Graphics: Double buffering disabled\n");
}
