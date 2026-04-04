#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

#include "lib/base.h"
#include "drivers/graphices/graphics.h"
#include "kernel/config.h"

/* ---- Resolution --------------------------------------------------------- */
#ifndef FB_WIDTH
#define FB_WIDTH  NUMOS_FB_WIDTH
#endif

#ifndef FB_HEIGHT
#define FB_HEIGHT NUMOS_FB_HEIGHT
#endif

#define FB_BPP          32
#define FB_TRANSPARENT  0xFFFFFFFFU

#define FB_COLOR(r,g,b) (((uint32_t)(r)<<16)|((uint32_t)(g)<<8)|(uint32_t)(b))
#define FB_BLACK        FB_COLOR(0,0,0)
#define FB_WHITE        FB_COLOR(255,255,255)
#define FB_RED          FB_COLOR(220, 50, 50)
#define FB_GREEN        FB_COLOR( 50,210, 80)
#define FB_BLUE         FB_COLOR( 50,100,230)
#define FB_CYAN         FB_COLOR( 50,200,210)
#define FB_YELLOW       FB_COLOR(230,210, 50)
#define FB_GREY         FB_COLOR(120,130,150)

#define FB_TERM_BG      FB_BLACK
#define FB_TERM_FG      FB_WHITE
#define FB_TERM_DIM     FB_GREY
#define FB_TERM_ACCENT  FB_WHITE
#define FB_TERM_SUCCESS FB_WHITE
#define FB_TERM_WARN    FB_WHITE
#define FB_TERM_ERR     FB_WHITE
#define FB_HDR_BG       FB_WHITE

#define FB_PANEL    FB_BLACK
#define FB_BORDER   FB_GREY
#define FB_TASKBAR  FB_BLACK
#define FB_DIM      FB_GREY
#define FB_ACCENT   FB_WHITE
#define FB_SUCCESS  FB_WHITE
#define FB_TEXT     FB_WHITE
#define FB_TITLE_BG FB_BLACK

#define FB_SCALE_1      1
#define FB_SCALE_2      2
#define FB_SCALE_3      3
#define FB_SCALE_SMALL  FB_SCALE_1
#define FB_SCALE_NORMAL FB_SCALE_2
#define FB_SCALE_PREFERRED NUMOS_FB_SCALE

#define SYS_FB_INFO     201
#define SYS_FB_WRITE    202
#define SYS_FB_CLEAR    203
#define SYS_FB_SETCOLOR 204
#define SYS_FB_SETPIXEL 205
#define SYS_FB_FILLRECT 206

int  fb_init_from_mode(const struct fb_mode_info *mode);
void fb_reset(void);
int  fb_is_available(void);
int  fb_get_width(void);
int  fb_get_height(void);
int  fb_get_bpp(void);

void     fb_set_pixel(int x, int y, uint32_t color);

void fb_fill(uint32_t color);
void fb_fill_rect(int x, int y, int w, int h, uint32_t color);

void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg, int scale);
void fb_draw_string(const char *s, int x, int y,
                    uint32_t fg, uint32_t bg, int scale);
int  fb_string_width(const char *s, int scale);

void fb_con_init(int x, int y, int w, int h,
                 uint32_t fg, uint32_t bg, int scale);
void fb_con_putchar(char c);
void fb_con_write(const char *buf, size_t len);
void fb_con_print(const char *s);
void fb_con_set_color(uint32_t fg, uint32_t bg);
void fb_con_clear(void);
void fb_con_enter_scroll_mode(void);


#endif
