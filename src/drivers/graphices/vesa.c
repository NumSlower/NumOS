/*
 * vesa.c - VESA/VBE mode detection and information driver
 *
 * Parses the multiboot2 framebuffer tag (type 8) populated by GRUB after it
 * negotiates a VBE mode via INT 0x10 before kernel start.  Classifies the
 * framebuffer source as VBE (real BIOS), BGA (Bochs/QEMU), or VGA legacy
 * based on the physical base address.
 */

#include "drivers/graphices/vesa.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"
#include "kernel/multiboot2.h"

static struct vesa_mode g_mode = {0};

/* Bounded string copy - no libc dependency */
static void strncpy_s(char *dst, const char *src, size_t cap) {
    size_t i = 0;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int vesa_supported_bpp(int bpp) {
    return bpp == 15 || bpp == 16 || bpp == 24 || bpp == 32;
}

static void vesa_set_layout_defaults(int bpp) {
    if (bpp == 15) {
        g_mode.red_pos = 10; g_mode.red_size = 5;
        g_mode.green_pos = 5; g_mode.green_size = 5;
        g_mode.blue_pos = 0; g_mode.blue_size = 5;
    } else if (bpp == 16) {
        g_mode.red_pos = 11; g_mode.red_size = 5;
        g_mode.green_pos = 5; g_mode.green_size = 6;
        g_mode.blue_pos = 0; g_mode.blue_size = 5;
    } else {
        g_mode.red_pos = 16; g_mode.red_size = 8;
        g_mode.green_pos = 8; g_mode.green_size = 8;
        g_mode.blue_pos = 0; g_mode.blue_size = 8;
    }
}

static int vesa_init_from_fb_tag(struct mb2_tag_framebuffer *fb) {
    if (!fb) return 0;
    if (fb->framebuffer_type != MB2_FRAMEBUFFER_TYPE_RGB) return 0;
    if (!fb->framebuffer_addr || !fb->framebuffer_width || !fb->framebuffer_height)
        return 0;

    int bpp = (int)fb->framebuffer_bpp;
    if (!vesa_supported_bpp(bpp)) return 0;

    int bytespp = (bpp + 7) / 8;
    int pitch = (int)fb->framebuffer_pitch;
    if (pitch == 0) pitch = (int)fb->framebuffer_width * bytespp;
    if (pitch < (int)fb->framebuffer_width * bytespp) return 0;

    g_mode.active = 1;
    g_mode.width = (int)fb->framebuffer_width;
    g_mode.height = (int)fb->framebuffer_height;
    g_mode.bpp = bpp;
    g_mode.bytespp = bytespp;
    g_mode.pitch = pitch;
    g_mode.phys_base = fb->framebuffer_addr;

    if (fb->rgb.red_mask_size == 0 && fb->rgb.green_mask_size == 0
                                   && fb->rgb.blue_mask_size == 0) {
        vesa_set_layout_defaults(bpp);
    } else {
        g_mode.red_pos = fb->rgb.red_field_position;
        g_mode.red_size = fb->rgb.red_mask_size;
        g_mode.green_pos = fb->rgb.green_field_position;
        g_mode.green_size = fb->rgb.green_mask_size;
        g_mode.blue_pos = fb->rgb.blue_field_position;
        g_mode.blue_size = fb->rgb.blue_mask_size;
    }
    g_mode.memory_model = VBE_MEMMODEL_DIRECT;
    return 1;
}

static int vesa_init_from_vbe_tag(struct mb2_tag_vbe *vbe) {
    if (!vbe) return 0;

    struct vbe_mode_info *mi = (struct vbe_mode_info *)vbe->vbe_mode_info;
    if (!(mi->mode_attributes & VBE_ATTR_GRAPHICS)) return 0;
    if (!(mi->mode_attributes & VBE_ATTR_LINEAR)) return 0;
    if (!mi->phys_base_ptr || !mi->x_resolution || !mi->y_resolution) return 0;

    int bpp = (int)mi->bits_per_pixel;
    if (!vesa_supported_bpp(bpp)) return 0;

    int bytespp = (bpp + 7) / 8;
    int pitch = (int)mi->lin_bytes_per_scan_line;
    if (pitch == 0) pitch = (int)mi->bytes_per_scan_line;
    if (pitch == 0) pitch = (int)mi->x_resolution * bytespp;
    if (pitch < (int)mi->x_resolution * bytespp) return 0;

    g_mode.active = 1;
    g_mode.width = (int)mi->x_resolution;
    g_mode.height = (int)mi->y_resolution;
    g_mode.bpp = bpp;
    g_mode.bytespp = bytespp;
    g_mode.pitch = pitch;
    g_mode.phys_base = (uint64_t)mi->phys_base_ptr;

    if (mi->lin_red_mask_size && mi->lin_green_mask_size && mi->lin_blue_mask_size) {
        g_mode.red_pos = mi->lin_red_field_position;
        g_mode.red_size = mi->lin_red_mask_size;
        g_mode.green_pos = mi->lin_green_field_position;
        g_mode.green_size = mi->lin_green_mask_size;
        g_mode.blue_pos = mi->lin_blue_field_position;
        g_mode.blue_size = mi->lin_blue_mask_size;
    } else if (mi->red_mask_size && mi->green_mask_size && mi->blue_mask_size) {
        g_mode.red_pos = mi->red_field_position;
        g_mode.red_size = mi->red_mask_size;
        g_mode.green_pos = mi->green_field_position;
        g_mode.green_size = mi->green_mask_size;
        g_mode.blue_pos = mi->blue_field_position;
        g_mode.blue_size = mi->blue_mask_size;
    } else {
        vesa_set_layout_defaults(bpp);
    }

    g_mode.memory_model = mi->memory_model;
    strncpy_s(g_mode.source, "VBE via GRUB", sizeof(g_mode.source));
    return 1;
}

int vesa_init(uint64_t mb2_info_phys) {
    memset(&g_mode, 0, sizeof(g_mode));
    if (!mb2_info_phys) return 0;

    struct mb2_tag *tag = mb2_find_tag(mb2_info_phys, MB2_TAG_FRAMEBUF);
    if (tag && vesa_init_from_fb_tag((struct mb2_tag_framebuffer *)tag)) {
        uint64_t base = g_mode.phys_base;
        if (base >= 0xA0000UL && base < 0xC0000UL) {
            strncpy_s(g_mode.source, "VGA legacy", sizeof(g_mode.source));
        } else if (base >= 0xE0000000UL && base < 0xF0000000UL) {
            strncpy_s(g_mode.source, "BGA (Bochs/QEMU)", sizeof(g_mode.source));
        } else {
            strncpy_s(g_mode.source, "VBE via GRUB (real BIOS)", sizeof(g_mode.source));
        }
        return 1;
    }

    tag = mb2_find_tag(mb2_info_phys, MB2_TAG_VBE);
    if (tag && vesa_init_from_vbe_tag((struct mb2_tag_vbe *)tag)) return 1;
    return 0;
}

int vesa_is_active(void)                   { return g_mode.active; }
const struct vesa_mode *vesa_get_mode(void){ return &g_mode; }

int vesa_fill_mode(uint64_t mb2_info_phys, struct fb_mode_info *out) {
    if (!out) return 0;
    if (!vesa_init(mb2_info_phys)) return 0;

    memset(out, 0, sizeof(*out));
    out->backend = GRAPHICS_BACKEND_VESA;
    out->width = g_mode.width;
    out->height = g_mode.height;
    out->bpp = g_mode.bpp;
    out->pitch = g_mode.pitch;
    out->bytespp = g_mode.bytespp;
    out->phys_base = g_mode.phys_base;
    out->red_pos = g_mode.red_pos;
    out->red_size = g_mode.red_size;
    out->green_pos = g_mode.green_pos;
    out->green_size = g_mode.green_size;
    out->blue_pos = g_mode.blue_pos;
    out->blue_size = g_mode.blue_size;
    out->memory_model = g_mode.memory_model;
    strncpy_s(out->source, g_mode.source, sizeof(out->source));
    return 1;
}

/* Write WxHxBPP string into buf without using printf */
void vesa_mode_str(char *buf, size_t cap) {
    if (!buf || cap < 2) return;
    size_t pos = 0;
    uint64_t nums[3] = {
        (uint64_t)g_mode.width,
        (uint64_t)g_mode.height,
        (uint64_t)g_mode.bpp
    };
    for (int n = 0; n < 3; n++) {
        uint64_t v = nums[n];
        char rev[20]; int rp = 0;
        if (!v) rev[rp++] = '0';
        while (v) { rev[rp++] = (char)('0' + v % 10); v /= 10; }
        for (int j = rp - 1; j >= 0 && pos + 1 < cap; j--)
            buf[pos++] = rev[j];
        if (n < 2 && pos + 1 < cap) buf[pos++] = 'x';
    }
    buf[pos] = '\0';
}

void vesa_print_info(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\nVESA/VBE Mode Information\n");
    vga_writestring("=========================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    if (!g_mode.active) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("  No VBE or VESA framebuffer in MB2 info block.\n");
        vga_writestring("  Set NUMOS_FB_ENABLE_VBE 1 or build with FORCE_VBE=1\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        return;
    }

    /* Source is the most important field - highlight it */
    vga_writestring("  Source    : ");
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    vga_writestring(g_mode.source);
    vga_putchar('\n');
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_writestring("  Mode      : ");
    print_dec((uint64_t)g_mode.width);  vga_writestring(" x ");
    print_dec((uint64_t)g_mode.height); vga_writestring(" x ");
    print_dec((uint64_t)g_mode.bpp);    vga_writestring(" bpp\n");

    vga_writestring("  Pitch     : ");
    print_dec((uint64_t)g_mode.pitch);  vga_writestring(" bytes/line\n");

    vga_writestring("  FB base   : 0x"); print_hex(g_mode.phys_base); vga_putchar('\n');

    uint64_t fb_bytes = (uint64_t)g_mode.pitch * (uint64_t)g_mode.height;
    vga_writestring("  FB size   : ");
    if (fb_bytes >= 1024UL * 1024UL) {
        uint64_t mb_w = fb_bytes / (1024UL * 1024UL);
        uint64_t mb_f = (fb_bytes % (1024UL * 1024UL)) * 10 / (1024UL * 1024UL);
        print_dec(mb_w); vga_putchar('.'); print_dec(mb_f); vga_writestring(" MB\n");
    } else {
        print_dec(fb_bytes / 1024); vga_writestring(" KB\n");
    }

    vga_writestring("  RGB layout: R[");
    print_dec((uint64_t)(g_mode.red_pos   + g_mode.red_size   - 1)); vga_putchar(':');
    print_dec((uint64_t)g_mode.red_pos);   vga_writestring("] G[");
    print_dec((uint64_t)(g_mode.green_pos + g_mode.green_size - 1)); vga_putchar(':');
    print_dec((uint64_t)g_mode.green_pos); vga_writestring("] B[");
    print_dec((uint64_t)(g_mode.blue_pos  + g_mode.blue_size  - 1)); vga_putchar(':');
    print_dec((uint64_t)g_mode.blue_pos);  vga_writestring("]\n");

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
}
