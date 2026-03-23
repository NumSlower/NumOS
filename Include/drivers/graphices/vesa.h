#ifndef VESA_H
#define VESA_H

#include "lib/base.h"
#include "drivers/graphices/graphics.h"

/*
 * vesa.h - VBE (VESA BIOS Extensions) structures and API
 *
 * In a multiboot2 kernel, GRUB handles the real-mode VBE calls.
 * We request a framebuffer in the multiboot header; GRUB negotiates
 * the VBE mode and passes the result back via the MB2 framebuffer tag.
 *
 * VBE info is exposed here so other subsystems can query the active
 * display mode without parsing raw multiboot2 tags themselves.
 *
 * References:
 *   https://wiki.osdev.org/VESA_Video_Modes
 *   https://wiki.osdev.org/User:Omarrx024/VESA_Tutorial
 *   VESA BIOS Extension Core Functions Standard v3.0
 */

/* =========================================================================
 * VBE Controller Info Block (returned by INT 0x10 / AX=0x4F00 in real mode)
 * GRUB reads this and exposes a subset via the MB2 framebuffer tag.
 * Stored here for reference; fields marked with * are filled by vesa_init().
 * ======================================================================= */
struct vbe_controller_info {
    uint8_t  signature[4];       /* "VESA" */
    uint16_t version;            /* VBE version (0x0300 = VBE 3.0) */
    uint32_t oem_string_ptr;     /* Far pointer to OEM string */
    uint32_t capabilities;       /* Bit flags */
    uint32_t video_mode_ptr;     /* Far pointer to mode list */
    uint16_t total_memory;       /* Number of 64 KB memory blocks */
    uint16_t oem_software_rev;
    uint32_t oem_vendor_name_ptr;
    uint32_t oem_product_name_ptr;
    uint32_t oem_product_rev_ptr;
    uint8_t  reserved[222];
    uint8_t  oem_data[256];
} __attribute__((packed));

/* =========================================================================
 * VBE Mode Info Block (returned by INT 0x10 / AX=0x4F01 in real mode)
 * ======================================================================= */
struct vbe_mode_info {
    /* Mandatory in VBE 1.2+ */
    uint16_t mode_attributes;    /* Bit 7 = linear framebuffer available */
    uint8_t  win_a_attributes;
    uint8_t  win_b_attributes;
    uint16_t win_granularity;    /* KB */
    uint16_t win_size;           /* KB */
    uint16_t win_a_segment;
    uint16_t win_b_segment;
    uint32_t win_func_ptr;
    uint16_t bytes_per_scan_line;

    /* Mandatory in VBE 1.2+ for VESA-defined modes */
    uint16_t x_resolution;
    uint16_t y_resolution;
    uint8_t  x_char_size;
    uint8_t  y_char_size;
    uint8_t  number_of_planes;
    uint8_t  bits_per_pixel;
    uint8_t  number_of_banks;
    uint8_t  memory_model;
    uint8_t  bank_size;          /* KB */
    uint8_t  number_of_image_pages;
    uint8_t  reserved1;

    /* Color mask info (valid for direct-color modes) */
    uint8_t  red_mask_size;
    uint8_t  red_field_position;
    uint8_t  green_mask_size;
    uint8_t  green_field_position;
    uint8_t  blue_mask_size;
    uint8_t  blue_field_position;
    uint8_t  rsvd_mask_size;
    uint8_t  rsvd_field_position;
    uint8_t  direct_color_mode_info;

    /* VBE 2.0+ */
    uint32_t phys_base_ptr;      /* Physical address of linear framebuffer */
    uint32_t reserved2;
    uint16_t reserved3;

    /* VBE 3.0 */
    uint16_t lin_bytes_per_scan_line;
    uint8_t  bnk_number_of_image_pages;
    uint8_t  lin_number_of_image_pages;
    uint8_t  lin_red_mask_size;
    uint8_t  lin_red_field_position;
    uint8_t  lin_green_mask_size;
    uint8_t  lin_green_field_position;
    uint8_t  lin_blue_mask_size;
    uint8_t  lin_blue_field_position;
    uint8_t  lin_rsvd_mask_size;
    uint8_t  lin_rsvd_field_position;
    uint32_t max_pixel_clock;    /* Hz */
    uint8_t  reserved4[189];
} __attribute__((packed));

/* =========================================================================
 * VBE mode attribute flags
 * ======================================================================= */
#define VBE_ATTR_SUPPORTED       (1 << 0)  /* Mode is supported by hardware */
#define VBE_ATTR_TTY             (1 << 2)  /* TTY output supported */
#define VBE_ATTR_COLOR           (1 << 3)  /* Color mode (else monochrome) */
#define VBE_ATTR_GRAPHICS        (1 << 4)  /* Graphics mode (else text) */
#define VBE_ATTR_VGA_COMPAT      (1 << 5)  /* Not VGA-compatible */
#define VBE_ATTR_NO_BANK         (1 << 6)  /* Banked mode not supported */
#define VBE_ATTR_LINEAR          (1 << 7)  /* Linear framebuffer available */
#define VBE_ATTR_DOUBLE_SCAN     (1 << 8)  /* Double-scan mode available */
#define VBE_ATTR_INTERLACE       (1 << 9)  /* Interlaced mode available */
#define VBE_ATTR_HW_TRIPLE       (1 << 10) /* Triple buffering supported */
#define VBE_ATTR_HW_STEREO       (1 << 11) /* Stereoscopic display supported */
#define VBE_ATTR_DUAL_START      (1 << 12) /* Dual display start address */

/* =========================================================================
 * VBE memory model types
 * ======================================================================= */
#define VBE_MEMMODEL_TEXT        0x00  /* Text mode */
#define VBE_MEMMODEL_CGA         0x01  /* CGA graphics */
#define VBE_MEMMODEL_HERCULES    0x02  /* Hercules graphics */
#define VBE_MEMMODEL_PLANAR      0x03  /* Planar (EGA/VGA 16-color) */
#define VBE_MEMMODEL_PACKED      0x04  /* Packed pixel (256-color) */
#define VBE_MEMMODEL_NONCHAIN4   0x05  /* Non-chain 4 (256-color) */
#define VBE_MEMMODEL_DIRECT      0x06  /* Direct color (15/16/24/32 bpp) */
#define VBE_MEMMODEL_YUV         0x07  /* YUV color */

/* =========================================================================
 * Common VBE standard mode numbers
 * ======================================================================= */
#define VBE_MODE_640x480x16      0x111
#define VBE_MODE_640x480x24      0x112
#define VBE_MODE_640x480x32      0x115
#define VBE_MODE_800x600x16      0x114
#define VBE_MODE_800x600x24      0x115
#define VBE_MODE_800x600x32      0x118
#define VBE_MODE_1024x768x16     0x117
#define VBE_MODE_1024x768x24     0x118
#define VBE_MODE_1024x768x32     0x11B
#define VBE_MODE_1280x720x32     0x11D
#define VBE_MODE_1280x1024x32    0x11F
#define VBE_MODE_1920x1080x32    0x120
#define VBE_MODE_LINEAR          0x4000 /* OR with mode number for LFB */

/* =========================================================================
 * Active mode descriptor (populated by vesa_init from the MB2 tag)
 * ======================================================================= */
struct vesa_mode {
    int      active;           /* 1 if a VESA/VBE mode is in use */
    int      width;            /* Horizontal resolution (pixels) */
    int      height;           /* Vertical resolution (pixels) */
    int      bpp;              /* Bits per pixel */
    int      pitch;            /* Bytes per scan line */
    int      bytespp;          /* Bytes per pixel */
    uint64_t phys_base;        /* Physical base address of framebuffer */

    /* Channel layout (from VBE mode info or MB2 tag) */
    uint8_t  red_pos;          /* Bit position of red channel */
    uint8_t  red_size;         /* Bits in red channel */
    uint8_t  green_pos;
    uint8_t  green_size;
    uint8_t  blue_pos;
    uint8_t  blue_size;

    /* Memory model */
    uint8_t  memory_model;     /* VBE_MEMMODEL_* */

    /* Source */
    char     source[32];       /* "VBE via GRUB", "BGA", "VGA", etc. */
};

/* =========================================================================
 * Public API
 * ======================================================================= */

/*
 * vesa_init - populate the active mode descriptor from the multiboot2
 * framebuffer tag.  Must be called after paging_init() and heap_init().
 * Returns 1 if a VESA/VBE framebuffer is in use, 0 otherwise.
 */
int vesa_init(uint64_t mb2_info_phys);

/* vesa_get_mode - return pointer to the active mode descriptor. */
const struct vesa_mode *vesa_get_mode(void);

/* vesa_print_info - write a formatted mode summary to the VGA console. */
void vesa_print_info(void);

/* vesa_is_active - returns 1 if a VESA/VBE mode is in use. */
int vesa_is_active(void);

/* vesa_fill_mode - copy the detected VESA mode into framebuffer form. */
int vesa_fill_mode(uint64_t mb2_info_phys, struct fb_mode_info *out);

/*
 * vesa_mode_str - write a compact mode string like "1920x1080x32" into buf.
 * buf must be at least 24 bytes.
 */
void vesa_mode_str(char *buf, size_t len);

#endif /* VESA_H */
