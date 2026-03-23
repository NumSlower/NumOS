#ifndef MULTIBOOT2_H
#define MULTIBOOT2_H

#include "lib/base.h"

/* =========================================================================
 * Multiboot2 info structure (passed by GRUB to the kernel via EBX)
 *
 * Layout in memory:
 *   [total_size:u32][reserved:u32][tag0][tag1]...[end_tag]
 *
 * Each tag is 8-byte aligned.
 * ======================================================================= */

#define MULTIBOOT2_BOOTLOADER_MAGIC 0x36d76289UL

/* Tag types */
#define MB2_TAG_END         0   /* Terminator                               */
#define MB2_TAG_CMDLINE     1   /* Kernel command line                      */
#define MB2_TAG_BOOTNAME    2   /* Bootloader name                          */
#define MB2_TAG_MODULE      3   /* Boot module (our disk.img lives here)    */
#define MB2_TAG_BASICMEM    4   /* Basic memory info (lower / upper)        */
#define MB2_TAG_MMAP        6   /* Full E820 memory map                     */
#define MB2_TAG_VBE         7   /* VBE control + mode info                  */
#define MB2_TAG_FRAMEBUF    8   /* Framebuffer information                  */

/* ---- Structures -------------------------------------------------------- */

struct mb2_info {
    uint32_t total_size;
    uint32_t reserved;
    /* tags follow immediately */
} __attribute__((packed));

struct mb2_tag {
    uint32_t type;
    uint32_t size;
    /* tag-specific payload follows */
} __attribute__((packed));

/* Tag type 3: Module */
struct mb2_tag_module {
    uint32_t type;       /* = MB2_TAG_MODULE (3)                            */
    uint32_t size;
    uint32_t mod_start;  /* physical start address of module data           */
    uint32_t mod_end;    /* physical end   address of module data           */
    char     cmdline[];  /* NUL-terminated command line string              */
} __attribute__((packed));

/* Tag type 8: Framebuffer */
#define MB2_FRAMEBUFFER_TYPE_INDEXED 0
#define MB2_FRAMEBUFFER_TYPE_RGB     1
#define MB2_FRAMEBUFFER_TYPE_EGA     2

struct mb2_tag_framebuffer {
    uint32_t type;       /* = MB2_TAG_FRAMEBUF (8)                          */
    uint32_t size;
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
    struct {
        uint8_t red_field_position;
        uint8_t red_mask_size;
        uint8_t green_field_position;
        uint8_t green_mask_size;
        uint8_t blue_field_position;
        uint8_t blue_mask_size;
    } __attribute__((packed)) rgb;
} __attribute__((packed));

/* Tag type 7: legacy VBE information */
struct mb2_tag_vbe {
    uint32_t type;       /* = MB2_TAG_VBE (7)                               */
    uint32_t size;
    uint16_t vbe_mode;
    uint16_t vbe_interface_seg;
    uint16_t vbe_interface_off;
    uint16_t vbe_interface_len;
    uint8_t  vbe_control_info[512];
    uint8_t  vbe_mode_info[256];
} __attribute__((packed));

/* ---- Parser API -------------------------------------------------------- */

/*
 * mb2_find_tag - walk the tag list inside the multiboot2 info block at
 * info_phys and return a pointer to the first tag whose type matches.
 *
 * info_phys must be a physical address within the first 1 GB (identity-mapped).
 * Returns NULL if the info pointer is 0 or the tag is not present.
 */
static inline struct mb2_tag *mb2_find_tag(uint64_t info_phys, uint32_t type)
{
    if (!info_phys) return NULL;

    struct mb2_info *info = (struct mb2_info *)(uintptr_t)info_phys;
    uint8_t *ptr = (uint8_t *)info + 8;           /* skip 8-byte header   */
    uint8_t *end = (uint8_t *)info + info->total_size;

    while (ptr + sizeof(struct mb2_tag) <= end) {
        struct mb2_tag *tag = (struct mb2_tag *)ptr;

        if (tag->type == MB2_TAG_END)  break;
        if (tag->type == type)         return tag;

        /* Tags are padded to 8-byte boundaries */
        uint32_t next_off = (tag->size + 7u) & ~7u;
        if (next_off == 0) break;          /* guard against infinite loop  */
        ptr += next_off;
    }
    return NULL;
}

#endif /* MULTIBOOT2_H */
