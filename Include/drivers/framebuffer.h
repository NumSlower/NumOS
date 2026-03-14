#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "lib/base.h"

/* =========================================================================
 * BGA (Bochs Graphics Adapter) Framebuffer Driver
 *
 * PCI device 0x1234:0x1111 (QEMU/Bochs VGA).
 * I/O ports: 0x01CE (index), 0x01CF (data).
 * Framebuffer base read from PCI BAR0.
 * Resolution probed at boot (tries 1920×1080 then 1024×768).
 *
 * VirtualBox: use "VBoxVGA" display adapter.  VBoxSVGA / VMSVGA do not
 * expose BGA registers; the driver falls back to VGA text mode cleanly.
 * For disk access in VirtualBox attach the disk image to an IDE controller,
 * not SATA (the ATA driver uses I/O ports 0x1F0-0x1F7).
 * ========================================================================= */

#define BGA_INDEX_PORT  0x01CE
#define BGA_DATA_PORT   0x01CF
#define BGA_REG_ID      0
#define BGA_REG_XRES    1
#define BGA_REG_YRES    2
#define BGA_REG_BPP     3
#define BGA_REG_ENABLE  4
#define BGA_DISABLED    0x00
#define BGA_ENABLED     0x01
#define BGA_LFB_ENABLED 0x40
#define BGA_ID_MIN      0xB0C0
#define BGA_ID_MAX      0xB0C5

#define FB_BPP          32
#define FB_TRANSPARENT  0xFFFFFFFFU

/* Colour helpers */
#define FB_COLOR(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define FB_BLACK        FB_COLOR(  0,  0,  0)
#define FB_WHITE        FB_COLOR(255,255,255)
#define FB_RED          FB_COLOR(220, 50, 50)
#define FB_GREEN        FB_COLOR( 50,210, 80)
#define FB_BLUE         FB_COLOR( 50,100,230)
#define FB_CYAN         FB_COLOR( 50,200,210)
#define FB_YELLOW       FB_COLOR(230,210, 50)
#define FB_GREY         FB_COLOR(120,130,150)

/* Terminal theme */
#define FB_TERM_BG      FB_COLOR( 14, 14, 22)
#define FB_TERM_FG      FB_COLOR(200,215,235)
#define FB_TERM_DIM     FB_COLOR(100,110,135)
#define FB_TERM_ACCENT  FB_COLOR( 80,140,255)
#define FB_TERM_SUCCESS FB_COLOR( 60,210,100)
#define FB_TERM_WARN    FB_COLOR(230,180, 50)
#define FB_TERM_ERR     FB_COLOR(230, 70, 70)
#define FB_HDR_BG       FB_COLOR( 20, 20, 36)

/* Character scales (multiplier on the 8×8 base glyph) */
#define FB_SCALE_1  1
#define FB_SCALE_2  2
#define FB_SCALE_3  3

/* Framebuffer syscall numbers */
#define SYS_FB_INFO     201
#define SYS_FB_WRITE    202
#define SYS_FB_CLEAR    203
#define SYS_FB_SETCOLOR 204
#define SYS_FB_SETPIXEL 205
#define SYS_FB_FILLRECT 206

/* Init / query */
void fb_init(void);
int  fb_is_available(void);
int  fb_get_width(void);
int  fb_get_height(void);

/* Pixels */
void     fb_set_pixel(int x, int y, uint32_t color);
uint32_t fb_get_pixel(int x, int y);

/* Fills */
void fb_fill(uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);
void fb_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot);

/* Lines / shapes */
void fb_draw_hline(int x, int y, int len, uint32_t color);
void fb_draw_vline(int x, int y, int len, uint32_t color);
void fb_draw_rect(int x, int y, int w, int h, uint32_t color);
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color);
void fb_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);
void fb_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color);

/* Text — bit0 of each glyph byte = leftmost pixel (LSB-left encoding) */
void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg, int scale);
void fb_draw_string(const char *s, int x, int y,
                    uint32_t fg, uint32_t bg, int scale);
int  fb_string_width(const char *s, int scale);

/* Scrolling console inside a rectangular region */
void fb_con_init(int x, int y, int w, int h,
                 uint32_t fg, uint32_t bg, int scale);
void fb_con_putchar(char c);
void fb_con_print(const char *s);
void fb_con_set_color(uint32_t fg, uint32_t bg);
void fb_con_clear(void);

/* Terminal UI */
void fb_draw_terminal(void);
void fb_update_header(void);

#endif