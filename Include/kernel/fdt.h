#ifndef NUMOS_KERNEL_FDT_H
#define NUMOS_KERNEL_FDT_H

#include "lib/base.h"

struct numos_fdt_initrd {
    uint64_t start;
    uint64_t end;
};

struct numos_fdt_bootargs {
    char text[256];
};

struct numos_fdt_framebuffer {
    uint64_t base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t bpp;
    uint8_t red_pos;
    uint8_t red_size;
    uint8_t green_pos;
    uint8_t green_size;
    uint8_t blue_pos;
    uint8_t blue_size;
};

int fdt_find_initrd(uint64_t fdt_addr, struct numos_fdt_initrd *out);
int fdt_get_bootargs(uint64_t fdt_addr, struct numos_fdt_bootargs *out);
int fdt_find_simple_framebuffer(uint64_t fdt_addr,
                                struct numos_fdt_framebuffer *out);
int fdt_is_valid_blob(uint64_t fdt_addr);

#endif /* NUMOS_KERNEL_FDT_H */
