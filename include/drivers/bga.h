/* NumOS — bga.h — Bochs Graphics Adapter */
#ifndef NUMOS_BGA_H
#define NUMOS_BGA_H

#include "libs/types.h"

bool bga_is_available(void);
bool bga_init(void);
void bga_set_mode(uint16_t w, uint16_t h, uint16_t bpp);
uint32_t* bga_get_framebuffer(void);

#endif
