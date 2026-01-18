/*
 * graphics.h - Graphics Mode Driver (VESA)
 * Provides support for graphics modes via VESA BIOS Extensions
 */

#ifndef GRAPHICS_H
#define GRAPHICS_H

#include "lib/base.h"

/* Graphics mode constants */
#define GRAPHICS_MAX_WIDTH  1024
#define GRAPHICS_MAX_HEIGHT 768
#define GRAPHICS_MAX_DEPTH  32

/* Pixel format */
typedef struct {
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t alpha_mask;
} pixel_format_t;

/* Graphics mode info */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pitch;           /* bytes per scanline */
    uint8_t  bpp;             /* bits per pixel */
    uint8_t  type;            /* 0 = indexed, 1 = direct RGB */
    uint64_t framebuffer;     /* physical address */
    pixel_format_t format;
} graphics_mode_t;

/* Color representation (32-bit ARGB) */
typedef uint32_t graphics_color_t;

#define GRAPHICS_COLOR_BLACK    0xFF000000
#define GRAPHICS_COLOR_WHITE    0xFFFFFFFF
#define GRAPHICS_COLOR_RED      0xFFFF0000
#define GRAPHICS_COLOR_GREEN    0xFF00FF00
#define GRAPHICS_COLOR_BLUE     0xFF0000FF
#define GRAPHICS_COLOR_YELLOW   0xFFFFFF00
#define GRAPHICS_COLOR_CYAN     0xFF00FFFF
#define GRAPHICS_COLOR_MAGENTA  0xFFFF00FF

/* Create color from RGBA components */
#define GRAPHICS_MAKE_COLOR(r, g, b, a) \
    (((uint32_t)(a) << 24) | ((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* Graphics driver interface */
int graphics_init(void);
int graphics_set_mode(uint16_t width, uint16_t height, uint8_t bpp);
int graphics_get_current_mode(graphics_mode_t *mode);
int graphics_is_available(void);
int graphics_is_active(void);

/* Framebuffer operations */
void graphics_putpixel(uint16_t x, uint16_t y, graphics_color_t color);
graphics_color_t graphics_getpixel(uint16_t x, uint16_t y);
void graphics_clear(graphics_color_t color);
void graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, graphics_color_t color);
void graphics_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, graphics_color_t color);
void graphics_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, graphics_color_t color);
void graphics_draw_circle(uint16_t x, uint16_t y, uint16_t radius, graphics_color_t color);

/* Font rendering (basic 8x8 bitmap font) */
void graphics_draw_char(uint16_t x, uint16_t y, char c, graphics_color_t fg, graphics_color_t bg);
void graphics_draw_string(uint16_t x, uint16_t y, const char *str, graphics_color_t fg, graphics_color_t bg);

/* Framebuffer access */
uint8_t* graphics_get_framebuffer(void);
uint32_t graphics_get_framebuffer_size(void);
void graphics_flip_buffer(void);

/* Video mode switching */
int graphics_switch_to_graphics(uint16_t width, uint16_t height, uint8_t bpp);
int graphics_switch_to_text(void);

/* Advanced features */
void graphics_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void graphics_enable_double_buffering(void);
void graphics_disable_double_buffering(void);

#endif /* GRAPHICS_H */
