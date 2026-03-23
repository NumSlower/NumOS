#include "drivers/graphices/bga.h"

#include "drivers/device.h"
#include "drivers/graphices/graphics.h"
#include "kernel/config.h"
#include "kernel/kernel.h"

static void bga_write(uint16_t reg, uint16_t val) {
    outw(BGA_INDEX_PORT, reg);
    outw(BGA_DATA_PORT, val);
}

static uint16_t bga_read(uint16_t reg) {
    outw(BGA_INDEX_PORT, reg);
    return inw(BGA_DATA_PORT);
}

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

static void bga_platform_mode(int *out_w, int *out_h) {
    if (out_w) *out_w = NUMOS_FB_WIDTH;
    if (out_h) *out_h = NUMOS_FB_HEIGHT;

    const struct hypervisor_info *hv = device_get_hypervisor();
    if (hv && hv->id == HYPERVISOR_VIRTUALBOX) {
        if (out_w) *out_w = NUMOS_FB_WIDTH_VBOX;
        if (out_h) *out_h = NUMOS_FB_HEIGHT_VBOX;
        return;
    }

    if (hv && (hv->id == HYPERVISOR_QEMU || hv->id == HYPERVISOR_KVM)) {
        if (out_w) *out_w = NUMOS_FB_WIDTH_QEMU;
        if (out_h) *out_h = NUMOS_FB_HEIGHT_QEMU;
    }
}

int bga_probe_available(void) {
    uint16_t id = bga_read(BGA_REG_ID);
    return (id >= BGA_ID_MIN && id <= BGA_ID_MAX) ? 1 : 0;
}

int bga_init_mode(struct fb_mode_info *out) {
    if (!out) return 0;
    if (!bga_probe_available()) return 0;

    uint64_t fb_phys = 0;

    struct device_entry *gl[8];
    int gc = device_get_by_type(DEVICE_TYPE_GPU, gl, 8);
    for (int i = 0; i < gc; i++) {
        if (gl[i]->vendor_id == 0x1234 && gl[i]->device_id == 0x1111) {
            fb_phys = gl[i]->pci_bar[0] & 0xFFFFFFF0U;
            break;
        }
    }

    if (!fb_phys) {
        for (uint8_t bus = 0; bus < 8 && !fb_phys; bus++) {
            for (uint8_t slot = 0; slot < 32 && !fb_phys; slot++) {
                uint32_t v = pci_config_read32(bus, slot, 0, 0);
                if ((v & 0xFFFF) == 0x1234 && ((v >> 16) & 0xFFFF) == 0x1111) {
                    fb_phys = pci_config_read32(bus, slot, 0, 0x10) & 0xFFFFFFF0U;
                }
            }
        }
    }

    if (!fb_phys) fb_phys = 0xE0000000U;

    int width = NUMOS_FB_WIDTH;
    int height = NUMOS_FB_HEIGHT;
    bga_platform_mode(&width, &height);

    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES,   (uint16_t)width);
    bga_write(BGA_REG_YRES,   (uint16_t)height);
    bga_write(BGA_REG_BPP,    32);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    memset(out, 0, sizeof(*out));
    out->backend = GRAPHICS_BACKEND_BGA;
    out->width = width;
    out->height = height;
    out->bpp = 32;
    out->bytespp = 4;
    out->pitch = width * 4;
    out->phys_base = fb_phys;
    out->red_pos = 16;
    out->red_size = 8;
    out->green_pos = 8;
    out->green_size = 8;
    out->blue_pos = 0;
    out->blue_size = 8;
    out->memory_model = 6;
    copy_str(out->source, "BGA (Bochs/QEMU)", sizeof(out->source));
    return 1;
}
