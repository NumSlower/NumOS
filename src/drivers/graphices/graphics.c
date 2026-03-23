#include "drivers/graphices/graphics.h"

#include "drivers/framebuffer.h"
#include "drivers/graphices/bga.h"
#include "drivers/graphices/vesa.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"

static int active_backend = GRAPHICS_BACKEND_VGA;
static struct fb_mode_info active_mode;

static void copy_str(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void set_active_vga(void) {
    fb_reset();
    memset(&active_mode, 0, sizeof(active_mode));
    active_backend = GRAPHICS_BACKEND_VGA;
    active_mode.backend = GRAPHICS_BACKEND_VGA;
    copy_str(active_mode.source, "VGA text mode", sizeof(active_mode.source));
}

int graphics_backend_from_name(const char *name) {
    if (!name || !name[0]) return GRAPHICS_BACKEND_AUTO;
    if (strcmp(name, "auto") == 0) return GRAPHICS_BACKEND_AUTO;
    if (strcmp(name, "vga") == 0) return GRAPHICS_BACKEND_VGA;
    if (strcmp(name, "vesa") == 0) return GRAPHICS_BACKEND_VESA;
    if (strcmp(name, "bga") == 0) return GRAPHICS_BACKEND_BGA;
    return -1;
}

const char *graphics_backend_name(int backend) {
    switch (backend) {
        case GRAPHICS_BACKEND_VGA:  return "VGA";
        case GRAPHICS_BACKEND_VESA: return "VESA";
        case GRAPHICS_BACKEND_BGA:  return "BGA";
        default:                    return "AUTO";
    }
}

int graphics_backend_priority(int backend) {
    switch (backend) {
        case GRAPHICS_BACKEND_VGA:
        case GRAPHICS_BACKEND_VESA:
        case GRAPHICS_BACKEND_BGA:
            return backend;
        default:
            return 0;
    }
}

int graphics_is_framebuffer_backend(int backend) {
    return backend == GRAPHICS_BACKEND_VESA || backend == GRAPHICS_BACKEND_BGA;
}

int graphics_activate(int backend, uint64_t mb2_info_phys) {
    struct fb_mode_info mode;

    switch (backend) {
        case GRAPHICS_BACKEND_VGA:
            set_active_vga();
            return 1;

        case GRAPHICS_BACKEND_VESA:
            if (!vesa_fill_mode(mb2_info_phys, &mode)) return 0;
            if (!fb_init_from_mode(&mode)) return 0;
            active_backend = GRAPHICS_BACKEND_VESA;
            active_mode = mode;
            return 1;

        case GRAPHICS_BACKEND_BGA:
            if (!bga_init_mode(&mode)) return 0;
            if (!fb_init_from_mode(&mode)) return 0;
            active_backend = GRAPHICS_BACKEND_BGA;
            active_mode = mode;
            return 1;

        default:
            return 0;
    }
}

int graphics_activate_auto(uint64_t mb2_info_phys, int preferred_backend) {
    int order[4];
    int count = 0;

    if (preferred_backend == GRAPHICS_BACKEND_VGA ||
        preferred_backend == GRAPHICS_BACKEND_VESA ||
        preferred_backend == GRAPHICS_BACKEND_BGA) {
        order[count++] = preferred_backend;
    }

    order[count++] = GRAPHICS_BACKEND_VGA;
    order[count++] = GRAPHICS_BACKEND_VESA;
    order[count++] = GRAPHICS_BACKEND_BGA;

    for (int i = 0; i < count; i++) {
        int backend = order[i];
        int seen = 0;
        for (int j = 0; j < i; j++) {
            if (order[j] == backend) { seen = 1; break; }
        }
        if (seen) continue;
        if (graphics_activate(backend, mb2_info_phys)) return backend;
    }

    set_active_vga();
    return GRAPHICS_BACKEND_VGA;
}

int graphics_get_active_backend(void) {
    return active_backend;
}

const struct fb_mode_info *graphics_get_active_mode(void) {
    return &active_mode;
}

void graphics_print_info(void) {
    vga_writestring("\nGraphics Backend Information\n");
    vga_writestring("============================\n");
    vga_writestring("  Active  : ");
    vga_writestring(graphics_backend_name(active_backend));
    vga_writestring("\n");
    vga_writestring("  Priority: ");
    print_dec((uint64_t)graphics_backend_priority(active_backend));
    vga_writestring("\n");

    if (!graphics_is_framebuffer_backend(active_backend)) {
        vga_writestring("  Mode    : VGA text mode\n");
        return;
    }

    vga_writestring("  Source  : ");
    vga_writestring(active_mode.source);
    vga_writestring("\n");
    vga_writestring("  Mode    : ");
    print_dec((uint64_t)active_mode.width);
    vga_putchar('x');
    print_dec((uint64_t)active_mode.height);
    vga_putchar('x');
    print_dec((uint64_t)active_mode.bpp);
    vga_writestring(" bpp\n");
    vga_writestring("  Base    : 0x");
    print_hex(active_mode.phys_base);
    vga_writestring("\n");
}
