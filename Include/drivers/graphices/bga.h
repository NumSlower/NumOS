#ifndef GRAPHICES_BGA_H
#define GRAPHICES_BGA_H

#include "lib/base.h"

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

struct fb_mode_info;

int bga_init_mode(struct fb_mode_info *out);
int bga_probe_available(void);

#endif
