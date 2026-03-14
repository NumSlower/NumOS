#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "lib/base.h"

/* =========================================================================
 * NumOS BGA (Bochs Graphics Adapter) Framebuffer Driver
 *
 * Accesses the QEMU/Bochs VGA device (PCI 0x1234:0x1111) via two 16-bit
 * I/O ports.  The linear framebuffer base address is read from PCI BAR0.
 *
 * Colour format: 32 BPP, 0x00RRGGBB (byte order: B G R 0 in memory).
 * Resolution:    1024 x 768.
 * ========================================================================= */

/* ---- BGA I/O ports ------------------------------------------------------- */
#define BGA_INDEX_PORT   0x01CE
#define BGA_DATA_PORT    0x01CF

/* ---- BGA register indices ------------------------------------------------ */
#define BGA_REG_ID          0
#define BGA_REG_XRES        1
#define BGA_REG_YRES        2
#define BGA_REG_BPP         3
#define BGA_REG_ENABLE      4
#define BGA_REG_BANK        5
#define BGA_REG_VIRT_WIDTH  6
#define BGA_REG_VIRT_HEIGHT 7
#define BGA_REG_X_OFFSET    8
#define BGA_REG_Y_OFFSET    9

/* ---- BGA ENABLE flags ---------------------------------------------------- */
#define BGA_DISABLED        0x00
#define BGA_ENABLED         0x01
#define BGA_LFB_ENABLED     0x40
#define BGA_NOCLEARMEM      0x80

/* ---- BGA version range --------------------------------------------------- */
#define BGA_ID_MIN          0xB0C0
#define BGA_ID_MAX          0xB0C5

/* ---- Resolution & depth -------------------------------------------------- */
#define FB_WIDTH    1920
#define FB_HEIGHT   1200
#define FB_BPP      32
#define FB_PITCH    (FB_WIDTH * 4)   /* bytes per row */

/* ---- Colour helpers ------------------------------------------------------- */
#define FB_COLOR(r, g, b) \
    (((uint32_t)(r) << 16) | ((uint32_t)(g) << 8) | (uint32_t)(b))

/* ---- Palette ------------------------------------------------------------- */
#define FB_BLACK        FB_COLOR(  0,   0,   0)
#define FB_WHITE        FB_COLOR(255, 255, 255)
#define FB_RED          FB_COLOR(220,  60,  60)
#define FB_GREEN        FB_COLOR( 80, 200, 100)
#define FB_BLUE         FB_COLOR( 60, 110, 220)
#define FB_CYAN         FB_COLOR( 60, 200, 210)
#define FB_MAGENTA      FB_COLOR(180,  70, 180)
#define FB_YELLOW       FB_COLOR(220, 200,  50)
#define FB_ORANGE       FB_COLOR(220, 140,  40)

/* ---- UI theme colours ---------------------------------------------------- */
#define FB_BG           FB_COLOR( 18,  18,  30)   /* desktop background     */
#define FB_PANEL        FB_COLOR( 28,  28,  48)   /* panel / window bg      */
#define FB_PANEL2       FB_COLOR( 36,  36,  58)   /* secondary panel        */
#define FB_TASKBAR      FB_COLOR( 22,  24,  50)   /* top bar                */
#define FB_BORDER       FB_COLOR( 70,  80, 130)   /* border / separator     */
#define FB_ACCENT       FB_COLOR(100, 140, 255)   /* highlight              */
#define FB_ACCENT2      FB_COLOR(180, 110, 255)   /* secondary accent       */
#define FB_TEXT         FB_COLOR(210, 220, 240)   /* normal text            */
#define FB_DIM          FB_COLOR(110, 120, 150)   /* dimmed text            */
#define FB_SUCCESS      FB_COLOR( 80, 210, 130)   /* success / ok           */
#define FB_WARN         FB_COLOR(220, 180,  60)   /* warning                */
#define FB_ERR          FB_COLOR(220,  80,  80)   /* error                  */
#define FB_TITLE_BG     FB_COLOR( 40,  50,  90)   /* window title bar       */

/* ---- Text scale ---------------------------------------------------------- */
#define FB_SCALE_SMALL  1   /*  8x8  px per character                        */
#define FB_SCALE_NORMAL 2   /* 16x16 px per character (recommended)          */
#define FB_SCALE_LARGE  3   /* 24x24 px per character                        */

/* =========================================================================
 * Initialisation
 * ======================================================================= */
void fb_init(void);
int  fb_is_available(void);

/* =========================================================================
 * Pixel operations
 * ======================================================================= */
void     fb_set_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);

/* =========================================================================
 * Fill / clear
 * ======================================================================= */
void fb_fill(uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot);
void fb_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right);

/* =========================================================================
 * Shapes
 * ======================================================================= */
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_rect_thick(int x, int y, int w, int h, int thick, uint32_t color);
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_draw_hline(int x, int y, int len, uint32_t color);
void fb_draw_vline(int x, int y, int len, uint32_t color);
void fb_fill_circle(int cx, int cy, int r, uint32_t color);
void fb_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void fb_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

/* =========================================================================
 * Text rendering  (embedded 8x8 font, MSB = leftmost pixel)
 * scale: 1 = 8x8 px, 2 = 16x16 px, …
 * Pass bg = 0xFFFFFFFF for transparent background.
 * ======================================================================= */
#define FB_TRANSPARENT 0xFFFFFFFF

void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg, int scale);
void fb_draw_string(const char *s, int x, int y,
                    uint32_t fg, uint32_t bg, int scale);
int  fb_string_width(const char *s, int scale);

/* =========================================================================
 * Framebuffer console  (scrolling text area inside a rectangle)
 * ======================================================================= */
void fb_con_init(int x, int y, int w, int h, uint32_t fg, uint32_t bg, int scale);
void fb_con_putchar(char c);
void fb_con_print(const char *s);
void fb_con_clear(void);

/* =========================================================================
 * High-level UI helpers
 * ======================================================================= */
void fb_draw_desktop(void);
void fb_draw_panel(int x, int y, int w, int h,
                   const char *title, uint32_t title_bg);
void fb_draw_button(int x, int y, int w, int h,
                    const char *label, uint32_t bg, uint32_t fg);
void fb_draw_separator(int x, int y, int w, uint32_t color);
void fb_update_taskbar(void);   /* refresh uptime / clock in taskbar          */

#endif /* FRAMEBUFFER_H */