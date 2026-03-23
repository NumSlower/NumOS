#ifndef GRAPHICES_GRAPHICS_H
#define GRAPHICES_GRAPHICS_H

#include "lib/base.h"

#define GRAPHICS_BACKEND_AUTO 0
#define GRAPHICS_BACKEND_VGA  1
#define GRAPHICS_BACKEND_VESA 2
#define GRAPHICS_BACKEND_BGA  3

struct fb_mode_info {
    int      backend;
    int      width;
    int      height;
    int      bpp;
    int      pitch;
    int      bytespp;
    uint64_t phys_base;
    uint8_t  red_pos;
    uint8_t  red_size;
    uint8_t  green_pos;
    uint8_t  green_size;
    uint8_t  blue_pos;
    uint8_t  blue_size;
    uint8_t  memory_model;
    char     source[32];
};

int graphics_backend_from_name(const char *name);
const char *graphics_backend_name(int backend);
int graphics_backend_priority(int backend);
int graphics_is_framebuffer_backend(int backend);

int graphics_activate(int backend, uint64_t mb2_info_phys);
int graphics_activate_auto(uint64_t mb2_info_phys, int preferred_backend);
int graphics_get_active_backend(void);
const struct fb_mode_info *graphics_get_active_mode(void);
void graphics_print_info(void);

#endif
