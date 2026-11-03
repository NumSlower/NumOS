/* NumOS — bga.c — Bochs Graphics Adapter */
#include "drivers/bga.h"
#include "kernel/io.h"

#define VBE_INDEX 0x01CE
#define VBE_DATA  0x01CF

#define BGA_ID          0
#define BGA_XRES        1
#define BGA_YRES        2
#define BGA_BPP         3
#define BGA_ENABLE      4
#define BGA_BANK        5
#define BGA_VIRT_WIDTH  6
#define BGA_VIRT_HEIGHT 7

#define VBE_DISPI_ENABLED     0x01
#define VBE_DISPI_LFB_ENABLED 0x40

static void bga_write(uint16_t index, uint16_t val) {
    outw(VBE_INDEX, index);
    outw(VBE_DATA, val);
}

static uint16_t bga_read(uint16_t index) {
    outw(VBE_INDEX, index);
    return inw(VBE_DATA);
}

bool bga_is_available(void) {
    uint16_t id = bga_read(BGA_ID);
    return (id >= 0xB0C0 && id <= 0xB0C5);
}

bool bga_init(void) {
    if (!bga_is_available()) return false;
    // Default to 1024x768x32
    // bga_set_mode(1024, 768, 32); 
    return true;
}

void bga_set_mode(uint16_t w, uint16_t h, uint16_t bpp) {
    bga_write(BGA_ENABLE, 0); // Disable VBE
    bga_write(BGA_XRES, w);
    bga_write(BGA_YRES, h);
    bga_write(BGA_BPP, bpp);
    bga_write(BGA_ENABLE, VBE_DISPI_ENABLED | VBE_DISPI_LFB_ENABLED);
}

uint32_t* bga_get_framebuffer(void) {
    return (uint32_t*)0xE0000000;
}
