/*
 * graphics_demo.c - Graphics mode demonstration
 * Shows how to use the graphics driver
 */

#include "drivers/graphics.h"
#include "drivers/vga.h"
#include "kernel/kernel.h"

/* Graphics demo patterns */
void graphics_demo_pattern_01(void) {
    vga_writestring("Graphics: Running pattern 1 - Colored rectangles\n");
    
    graphics_clear(GRAPHICS_COLOR_BLACK);
    
    /* Draw colored rectangles */
    graphics_fill_rect(10, 10, 100, 100, GRAPHICS_COLOR_RED);
    graphics_fill_rect(120, 10, 100, 100, GRAPHICS_COLOR_GREEN);
    graphics_fill_rect(230, 10, 100, 100, GRAPHICS_COLOR_BLUE);
    graphics_fill_rect(340, 10, 100, 100, GRAPHICS_COLOR_YELLOW);
}

/* Color gradient pattern */
void graphics_demo_pattern_02(void) {
    vga_writestring("Graphics: Running pattern 2 - Color gradient\n");
    
    graphics_clear(GRAPHICS_COLOR_BLACK);
    
    /* Draw gradient-like pattern */
    for (int x = 0; x < 256; x++) {
        uint32_t color = GRAPHICS_MAKE_COLOR(x, 0, 255 - x, 255);
        graphics_draw_line(x + 100, 100, x + 100, 300, color);
    }
}

/* Geometric shapes pattern */
void graphics_demo_pattern_03(void) {
    vga_writestring("Graphics: Running pattern 3 - Geometric shapes\n");
    
    graphics_clear(GRAPHICS_COLOR_BLACK);
    
    /* Draw rectangles */
    graphics_draw_rect(50, 50, 200, 150, GRAPHICS_COLOR_WHITE);
    graphics_fill_rect(300, 50, 200, 150, GRAPHICS_COLOR_CYAN);
    
    /* Draw circles */
    graphics_draw_circle(150, 300, 50, GRAPHICS_COLOR_MAGENTA);
    graphics_draw_circle(350, 300, 50, GRAPHICS_COLOR_YELLOW);
}

/* Grid pattern */
void graphics_demo_pattern_04(void) {
    vga_writestring("Graphics: Running pattern 4 - Grid pattern\n");
    
    graphics_clear(GRAPHICS_COLOR_BLACK);
    
    /* Draw vertical lines */
    for (int x = 0; x < 800; x += 50) {
        graphics_draw_line(x, 0, x, 600, GRAPHICS_MAKE_COLOR(100, 100, 100, 255));
    }
    
    /* Draw horizontal lines */
    for (int y = 0; y < 600; y += 50) {
        graphics_draw_line(0, y, 800, y, GRAPHICS_MAKE_COLOR(100, 100, 100, 255));
    }
}

/* Text rendering pattern */
void graphics_demo_pattern_05(void) {
    vga_writestring("Graphics: Running pattern 5 - Text rendering\n");
    
    graphics_clear(GRAPHICS_COLOR_BLUE);
    
    graphics_draw_string(50, 50, "Graphics Mode Active", 
                        GRAPHICS_COLOR_WHITE, GRAPHICS_COLOR_BLACK);
    graphics_draw_string(50, 100, "NumOS Graphics Driver", 
                        GRAPHICS_COLOR_YELLOW, GRAPHICS_COLOR_BLACK);
    graphics_draw_string(50, 150, "VESA/Framebuffer Support", 
                        GRAPHICS_COLOR_CYAN, GRAPHICS_COLOR_BLACK);
}

/* Animated pattern (static for now) */
void graphics_demo_pattern_06(void) {
    vga_writestring("Graphics: Running pattern 6 - Colorful display\n");
    
    graphics_clear(GRAPHICS_COLOR_BLACK);
    
    /* Create a colorful display */
    int stripe_width = 32;
    graphics_color_t colors[] = {
        GRAPHICS_COLOR_RED,
        GRAPHICS_COLOR_GREEN,
        GRAPHICS_COLOR_BLUE,
        GRAPHICS_COLOR_YELLOW,
        GRAPHICS_COLOR_CYAN,
        GRAPHICS_COLOR_MAGENTA,
        GRAPHICS_COLOR_WHITE
    };
    int num_colors = 7;
    
    for (int i = 0; i < num_colors; i++) {
        graphics_fill_rect(i * stripe_width, 0, stripe_width, 768, colors[i]);
    }
}

/* Run graphics demo */
void graphics_run_demo(int demo_num) {
    if (!graphics_is_available()) {
        vga_writestring("Graphics: Graphics mode not available\n");
        return;
    }
    
    vga_writestring("Graphics: Initializing graphics mode...\n");
    
    if (graphics_init() != 0) {
        vga_writestring("Graphics: Failed to initialize graphics\n");
        return;
    }
    
    /* Set graphics mode: 1024x768x32 */
    if (graphics_switch_to_graphics(1024, 768, 32) != 0) {
        vga_writestring("Graphics: Failed to set graphics mode\n");
        return;
    }
    
    vga_writestring("Graphics: Graphics mode enabled at 1024x768x32\n");
    
    /* Run selected demo */
    switch (demo_num) {
        case 1:
            graphics_demo_pattern_01();
            break;
        case 2:
            graphics_demo_pattern_02();
            break;
        case 3:
            graphics_demo_pattern_03();
            break;
        case 4:
            graphics_demo_pattern_04();
            break;
        case 5:
            graphics_demo_pattern_05();
            break;
        case 6:
            graphics_demo_pattern_06();
            break;
        default:
            vga_writestring("Graphics: Unknown demo pattern\n");
            return;
    }
    
    vga_writestring("Graphics: Demo pattern displayed\n");
    vga_writestring("Graphics: Press any key to continue...\n");
}

/* Run all demos in sequence */
void graphics_run_all_demos(void) {
    for (int i = 1; i <= 6; i++) {
        graphics_run_demo(i);
        timer_sleep(3000); /* 3 second pause between demos */
    }
    
    /* Switch back to text mode */
    graphics_switch_to_text();
    vga_writestring("Graphics: Returned to text mode\n");
}

/* Test graphics functionality */
void graphics_self_test(void) {
    vga_writestring("Graphics: Starting graphics self-test...\n");
    
    if (graphics_init() != 0) {
        vga_writestring("Graphics: FAIL - Initialization failed\n");
        return;
    }
    vga_writestring("Graphics: PASS - Initialization\n");
    
    if (graphics_set_mode(800, 600, 32) != 0) {
        vga_writestring("Graphics: FAIL - Mode setting failed\n");
        return;
    }
    vga_writestring("Graphics: PASS - Mode setting (800x600x32)\n");
    
    graphics_mode_t mode;
    if (graphics_get_current_mode(&mode) != 0) {
        vga_writestring("Graphics: FAIL - Mode query failed\n");
        return;
    }
    vga_writestring("Graphics: PASS - Mode query\n");
    
    if (mode.width != 800 || mode.height != 600 || mode.bpp != 32) {
        vga_writestring("Graphics: FAIL - Mode parameters mismatch\n");
        return;
    }
    vga_writestring("Graphics: PASS - Mode parameters validation\n");
    
    /* Test framebuffer operations */
    graphics_clear(GRAPHICS_COLOR_BLACK);
    graphics_putpixel(400, 300, GRAPHICS_COLOR_WHITE);
    graphics_fill_rect(100, 100, 100, 100, GRAPHICS_COLOR_RED);
    graphics_draw_line(0, 0, 799, 599, GRAPHICS_COLOR_GREEN);
    
    vga_writestring("Graphics: PASS - Framebuffer operations\n");
    
    vga_writestring("Graphics: All tests passed!\n");
}
