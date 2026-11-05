/* NumOS — kdebug.h — Debug/verbose logging macro */
#ifndef NUMOS_KERNEL_KDEBUG_H
#define NUMOS_KERNEL_KDEBUG_H

#include "libs/kprintf.h"
#include "drivers/vga.h"

#ifdef KDEBUG

#define kdebug(fmt, ...) do { \
    vga_set_color(DARK_GREY, BLACK); \
    kprintf("  > " fmt "\n", ##__VA_ARGS__); \
    vga_set_color(WHITE, BLACK); \
} while (0)

#else

#define kdebug(fmt, ...) ((void)0)

#endif /* KDEBUG */

#endif /* NUMOS_KERNEL_KDEBUG_H */
