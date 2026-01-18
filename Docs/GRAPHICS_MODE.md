# NumOS Graphics Mode Implementation

## Overview

The NumOS graphics mode driver provides full support for VESA graphics framebuffer operations. It enables kernel development with high-resolution color graphics rendering capabilities.

## Features

### Core Graphics Functionality
- **Resolution Support**: Up to 1024x768 (configurable)
- **Color Depths**: 8-bit (indexed), 16-bit (RGB565), 24-bit (RGB888), 32-bit (ARGB8888)
- **Framebuffer Access**: Direct memory access to video memory
- **Double Buffering**: Optional back-buffer for flicker-free rendering
- **Color Management**: Full RGB color support with MAKE_COLOR macro

### Graphics Primitives
- **Pixel Operations**: Individual pixel get/put
- **Rectangles**: Filled and outlined rectangles
- **Lines**: Bresenham line algorithm for smooth lines
- **Circles**: Midpoint circle algorithm
- **Text Rendering**: 8x8 bitmap font support
- **Clearing**: Fast framebuffer clearing

### Advanced Features
- **Color Format Conversion**: Automatic conversion between color depths
- **Clipping**: Automatic boundary clipping for all drawing operations
- **Palette Support**: Indexed color palette management (stub)
- **Mode Switching**: Seamless switching between graphics and text modes

## Architecture

### Header File: `Include/drivers/graphics.h`

Defines all public graphics API functions and structures:

```c
/* Graphics mode types and structures */
typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pitch;
    uint8_t  bpp;
    uint8_t  type;
    uint64_t framebuffer;
    pixel_format_t format;
} graphics_mode_t;

/* Color type */
typedef uint32_t graphics_color_t;
```

### Implementation: `src/drivers/graphics.c`

Core graphics driver with:
- Mode management and validation
- Pixel operations with format conversion
- Geometric shape rendering
- Font rendering system
- Buffer management

### Demo: `src/drivers/graphics_demo.c`

Six demonstration patterns:
1. **Pattern 1**: Colored rectangles
2. **Pattern 2**: Color gradient
3. **Pattern 3**: Geometric shapes
4. **Pattern 4**: Grid pattern
5. **Pattern 5**: Text rendering
6. **Pattern 6**: Colorful display

## API Reference

### Initialization

```c
/* Initialize graphics subsystem */
int graphics_init(void);

/* Check if graphics is available */
int graphics_is_available(void);

/* Check if graphics mode is active */
int graphics_is_active(void);
```

### Mode Management

```c
/* Set graphics mode */
int graphics_set_mode(uint16_t width, uint16_t height, uint8_t bpp);

/* Get current mode information */
int graphics_get_current_mode(graphics_mode_t *mode);

/* Switch to graphics mode */
int graphics_switch_to_graphics(uint16_t width, uint16_t height, uint8_t bpp);

/* Switch back to text mode */
int graphics_switch_to_text(void);
```

### Pixel Operations

```c
/* Put pixel at coordinates */
void graphics_putpixel(uint16_t x, uint16_t y, graphics_color_t color);

/* Get pixel value */
graphics_color_t graphics_getpixel(uint16_t x, uint16_t y);

/* Clear entire framebuffer */
void graphics_clear(graphics_color_t color);
```

### Shape Drawing

```c
/* Fill rectangle with color */
void graphics_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, graphics_color_t color);

/* Draw unfilled rectangle outline */
void graphics_draw_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, graphics_color_t color);

/* Draw line between two points */
void graphics_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, graphics_color_t color);

/* Draw circle */
void graphics_draw_circle(uint16_t cx, uint16_t cy, uint16_t radius, graphics_color_t color);
```

### Text Rendering

```c
/* Draw single character */
void graphics_draw_char(uint16_t x, uint16_t y, char c, graphics_color_t fg, graphics_color_t bg);

/* Draw string of characters */
void graphics_draw_string(uint16_t x, uint16_t y, const char *str, graphics_color_t fg, graphics_color_t bg);
```

### Framebuffer Management

```c
/* Get direct framebuffer pointer */
uint8_t* graphics_get_framebuffer(void);

/* Get total framebuffer size in bytes */
uint32_t graphics_get_framebuffer_size(void);

/* Copy back buffer to front buffer */
void graphics_flip_buffer(void);

/* Enable double buffering */
void graphics_enable_double_buffering(void);

/* Disable double buffering */
void graphics_disable_double_buffering(void);
```

### Palette Management (Indexed Color)

```c
/* Set palette entry (for 8-bit mode) */
void graphics_set_palette_entry(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
```

## Color Management

### Predefined Colors

```c
#define GRAPHICS_COLOR_BLACK    0xFF000000
#define GRAPHICS_COLOR_WHITE    0xFFFFFFFF
#define GRAPHICS_COLOR_RED      0xFFFF0000
#define GRAPHICS_COLOR_GREEN    0xFF00FF00
#define GRAPHICS_COLOR_BLUE     0xFF0000FF
#define GRAPHICS_COLOR_YELLOW   0xFFFFFF00
#define GRAPHICS_COLOR_CYAN     0xFF00FFFF
#define GRAPHICS_COLOR_MAGENTA  0xFFFF00FF
```

### Custom Colors

```c
/* Create custom RGBA color */
graphics_color_t color = GRAPHICS_MAKE_COLOR(255, 128, 64, 255);
/* Arguments: R, G, B, A (0-255 each) */
```

## Usage Example

```c
#include "drivers/graphics.h"

void main(void) {
    /* Initialize graphics */
    graphics_init();
    
    /* Switch to graphics mode */
    graphics_switch_to_graphics(1024, 768, 32);
    
    /* Clear screen to black */
    graphics_clear(GRAPHICS_COLOR_BLACK);
    
    /* Draw some shapes */
    graphics_fill_rect(100, 100, 200, 150, GRAPHICS_COLOR_RED);
    graphics_draw_circle(600, 400, 50, GRAPHICS_COLOR_BLUE);
    graphics_draw_line(0, 0, 1023, 767, GRAPHICS_COLOR_WHITE);
    
    /* Draw text */
    graphics_draw_string(400, 300, "Hello Graphics!", 
                        GRAPHICS_COLOR_WHITE, GRAPHICS_COLOR_BLACK);
    
    /* Switch back to text mode */
    graphics_switch_to_text();
}
```

## Multiboot2 Integration

The graphics driver is integrated with Multiboot2 bootloader protocol:

- Framebuffer information tag requests color/VBE info
- Bootloader-provided framebuffer address support (when available)
- Graceful fallback to malloc-based framebuffer

## Performance Considerations

1. **Color Conversion**: Automatic conversion between formats with minimal overhead
2. **Clipping**: Efficient boundary checking prevents buffer overruns
3. **Fast Clear**: Word-aligned clear operations for 32-bit mode
4. **Double Buffering**: Optional for smooth animations without flicker

## Limitations and Future Improvements

### Current Limitations
- Fixed 8x8 bitmap font (no scalable fonts)
- Palette mode (8-bit) is grayscale only
- No hardware acceleration
- Framebuffer in system RAM (not VRAM)

### Future Enhancements
- VESA BIOS Extensions (VBE) direct support
- UEFI Graphics Output Protocol (GOP) support
- Scalable TrueType font rendering
- Hardware sprite support
- Display list/command buffer optimization
- Memory-mapped I/O optimization

## Building

The graphics driver is automatically compiled when building NumOS:

```bash
make clean
make
```

Both the core driver (`graphics.c`) and demo (`graphics_demo.c`) are built into the kernel.

## Testing

Self-test functionality available:

```c
graphics_self_test();  /* Run all graphics tests */
graphics_run_demo(1);  /* Run specific demo pattern */
graphics_run_all_demos();  /* Run all demo patterns */
```

## File Structure

```
Include/
  drivers/
    graphics.h        - Main graphics API header
    graphics_demo.h   - Demo functions header

src/
  drivers/
    graphics.c        - Core graphics driver implementation
    graphics_demo.c   - Graphics demo patterns and tests

src/boot/
  multiboot_header.asm - Updated with framebuffer tags
```

## Integration with Kernel

Graphics functions are exposed through the kernel API in `kernel.h`:

```c
int graphics_init(void);
int graphics_set_mode(uint16_t width, uint16_t height, uint8_t bpp);
int graphics_switch_to_graphics(uint16_t width, uint16_t height, uint8_t bpp);
int graphics_switch_to_text(void);
```

## Conclusion

The NumOS graphics mode implementation provides a complete, well-structured graphics layer for kernel development. It supports multiple color depths, advanced drawing primitives, and text rendering, making it suitable for GUI development and advanced kernel features.
