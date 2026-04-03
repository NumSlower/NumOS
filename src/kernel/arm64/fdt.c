#include "kernel/fdt.h"
#include "kernel/kernel.h"

#define FDT_MAGIC 0xD00DFEEDu

#define FDT_BEGIN_NODE 0x00000001u
#define FDT_END_NODE   0x00000002u
#define FDT_PROP       0x00000003u
#define FDT_NOP        0x00000004u
#define FDT_END        0x00000009u

struct fdt_header {
    uint32_t magic;
    uint32_t totalsize;
    uint32_t off_dt_struct;
    uint32_t off_dt_strings;
    uint32_t off_mem_rsvmap;
    uint32_t version;
    uint32_t last_comp_version;
    uint32_t boot_cpuid_phys;
    uint32_t size_dt_strings;
    uint32_t size_dt_struct;
} __attribute__((packed));

struct fdt_prop {
    uint32_t len;
    uint32_t nameoff;
} __attribute__((packed));

static uint32_t be32(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static uint64_t be64(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    uint64_t value = 0;
    for (int i = 0; i < 8; i++) value = (value << 8) | p[i];
    return value;
}

static const uint8_t *align4(const uint8_t *ptr) {
    uintptr_t value = (uintptr_t)ptr;
    return (const uint8_t *)((value + 3u) & ~(uintptr_t)3u);
}

static int names_equal(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == '\0' && *b == '\0';
}

static int string_has_prefix(const char *text, const char *prefix) {
    while (*prefix) {
        if (*text++ != *prefix++) return 0;
    }
    return 1;
}

static int copy_text(char *dst, size_t cap, const uint8_t *src, size_t len) {
    if (!dst || cap == 0) return -1;
    size_t n = len;
    if (n >= cap) n = cap - 1;
    for (size_t i = 0; i < n; i++) dst[i] = (char)src[i];
    dst[n] = '\0';
    return (n > 0) ? 0 : -1;
}

int fdt_is_valid_blob(uint64_t fdt_addr) {
    if (!fdt_addr) return 0;

    const struct fdt_header *hdr = (const struct fdt_header *)(uintptr_t)fdt_addr;
    if (be32(&hdr->magic) != FDT_MAGIC) return 0;

    uint32_t totalsize = be32(&hdr->totalsize);
    uint32_t off_struct = be32(&hdr->off_dt_struct);
    uint32_t size_struct = be32(&hdr->size_dt_struct);
    uint32_t off_strings = be32(&hdr->off_dt_strings);
    uint32_t size_strings = be32(&hdr->size_dt_strings);

    if (totalsize < sizeof(struct fdt_header)) return 0;
    if (totalsize > (2u * 1024u * 1024u)) return 0;
    if (off_struct >= totalsize || off_strings >= totalsize) return 0;
    if (size_struct > totalsize - off_struct) return 0;
    if (size_strings > totalsize - off_strings) return 0;
    return 1;
}

int fdt_find_initrd(uint64_t fdt_addr, struct numos_fdt_initrd *out) {
    if (!fdt_addr || !out) return -1;

    const struct fdt_header *hdr = (const struct fdt_header *)(uintptr_t)fdt_addr;
    if (!fdt_is_valid_blob(fdt_addr)) return -1;

    const uint8_t *blob = (const uint8_t *)(uintptr_t)fdt_addr;
    uint32_t off_struct = be32(&hdr->off_dt_struct);
    uint32_t size_struct = be32(&hdr->size_dt_struct);
    uint32_t off_strings = be32(&hdr->off_dt_strings);
    uint32_t size_strings = be32(&hdr->size_dt_strings);

    const uint8_t *ptr = blob + off_struct;
    const uint8_t *end = ptr + size_struct;
    const char *strings = (const char *)(blob + off_strings);
    const char *strings_end = strings + size_strings;
    int depth = 0;
    int in_chosen = 0;

    out->start = 0;
    out->end = 0;

    while (ptr + 4 <= end) {
        uint32_t token = be32(ptr);
        ptr += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)ptr;
            if (depth == 1 && names_equal(name, "chosen")) {
                in_chosen = 1;
            } else if (depth <= 1) {
                in_chosen = 0;
            }

            while (ptr < end && *ptr) ptr++;
            if (ptr < end) ptr++;
            ptr = align4(ptr);
            depth++;
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth > 0) depth--;
            if (depth < 2) in_chosen = 0;
            continue;
        }

        if (token == FDT_PROP) {
            if (ptr + sizeof(struct fdt_prop) > end) return -1;
            const struct fdt_prop *prop = (const struct fdt_prop *)ptr;
            uint32_t len = be32(&prop->len);
            uint32_t nameoff = be32(&prop->nameoff);
            ptr += sizeof(struct fdt_prop);
            if (ptr + len > end) return -1;
            if (strings + nameoff >= strings_end) return -1;

            const char *name = strings + nameoff;
            if (in_chosen) {
                if (names_equal(name, "linux,initrd-start")) {
                    out->start = (len >= 8) ? be64(ptr) :
                                 (len >= 4) ? (uint64_t)be32(ptr) : 0;
                } else if (names_equal(name, "linux,initrd-end")) {
                    out->end = (len >= 8) ? be64(ptr) :
                               (len >= 4) ? (uint64_t)be32(ptr) : 0;
                }
            }

            ptr = align4(ptr + len);
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;
        return -1;
    }

    return (out->start != 0 && out->end > out->start) ? 0 : -1;
}

int fdt_get_bootargs(uint64_t fdt_addr, struct numos_fdt_bootargs *out) {
    if (!fdt_addr || !out) return -1;

    const struct fdt_header *hdr = (const struct fdt_header *)(uintptr_t)fdt_addr;
    if (!fdt_is_valid_blob(fdt_addr)) return -1;

    const uint8_t *blob = (const uint8_t *)(uintptr_t)fdt_addr;
    uint32_t off_struct = be32(&hdr->off_dt_struct);
    uint32_t size_struct = be32(&hdr->size_dt_struct);
    uint32_t off_strings = be32(&hdr->off_dt_strings);
    uint32_t size_strings = be32(&hdr->size_dt_strings);

    const uint8_t *ptr = blob + off_struct;
    const uint8_t *end = ptr + size_struct;
    const char *strings = (const char *)(blob + off_strings);
    const char *strings_end = strings + size_strings;
    int depth = 0;
    int in_chosen = 0;

    out->text[0] = '\0';

    while (ptr + 4 <= end) {
        uint32_t token = be32(ptr);
        ptr += 4;

        if (token == FDT_BEGIN_NODE) {
            const char *name = (const char *)ptr;
            if (depth == 1 && names_equal(name, "chosen")) {
                in_chosen = 1;
            } else if (depth <= 1) {
                in_chosen = 0;
            }

            while (ptr < end && *ptr) ptr++;
            if (ptr < end) ptr++;
            ptr = align4(ptr);
            depth++;
            continue;
        }

        if (token == FDT_END_NODE) {
            if (depth > 0) depth--;
            if (depth < 2) in_chosen = 0;
            continue;
        }

        if (token == FDT_PROP) {
            if (ptr + sizeof(struct fdt_prop) > end) return -1;
            const struct fdt_prop *prop = (const struct fdt_prop *)ptr;
            uint32_t len = be32(&prop->len);
            uint32_t nameoff = be32(&prop->nameoff);
            ptr += sizeof(struct fdt_prop);
            if (ptr + len > end) return -1;
            if (strings + nameoff >= strings_end) return -1;

            const char *name = strings + nameoff;
            if (in_chosen && names_equal(name, "bootargs")) {
                return copy_text(out->text, sizeof(out->text), ptr, len);
            }

            ptr = align4(ptr + len);
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;
        return -1;
    }

    return -1;
}

int fdt_find_simple_framebuffer(uint64_t fdt_addr,
                                struct numos_fdt_framebuffer *out) {
    if (!fdt_addr || !out) return -1;

    const struct fdt_header *hdr = (const struct fdt_header *)(uintptr_t)fdt_addr;
    if (!fdt_is_valid_blob(fdt_addr)) return -1;

    const uint8_t *blob = (const uint8_t *)(uintptr_t)fdt_addr;
    uint32_t off_struct = be32(&hdr->off_dt_struct);
    uint32_t size_struct = be32(&hdr->size_dt_struct);
    uint32_t off_strings = be32(&hdr->off_dt_strings);
    uint32_t size_strings = be32(&hdr->size_dt_strings);

    const uint8_t *ptr = blob + off_struct;
    const uint8_t *end = ptr + size_struct;
    const char *strings = (const char *)(blob + off_strings);
    const char *strings_end = strings + size_strings;
    int depth = 0;
    int node_depth = -1;
    int node_is_simplefb = 0;

    memset(out, 0, sizeof(*out));

    while (ptr + 4 <= end) {
        uint32_t token = be32(ptr);
        ptr += 4;

        if (token == FDT_BEGIN_NODE) {
            while (ptr < end && *ptr) ptr++;
            if (ptr < end) ptr++;
            ptr = align4(ptr);
            depth++;
            continue;
        }

        if (token == FDT_END_NODE) {
            if (node_depth == depth) {
                if (node_is_simplefb &&
                    out->base != 0 &&
                    out->width != 0 &&
                    out->height != 0 &&
                    out->stride != 0 &&
                    out->bpp != 0) {
                    return 0;
                }
                memset(out, 0, sizeof(*out));
                node_depth = -1;
                node_is_simplefb = 0;
            }
            if (depth > 0) depth--;
            continue;
        }

        if (token == FDT_PROP) {
            if (ptr + sizeof(struct fdt_prop) > end) return -1;
            const struct fdt_prop *prop = (const struct fdt_prop *)ptr;
            uint32_t len = be32(&prop->len);
            uint32_t nameoff = be32(&prop->nameoff);
            ptr += sizeof(struct fdt_prop);
            if (ptr + len > end) return -1;
            if (strings + nameoff >= strings_end) return -1;

            const char *name = strings + nameoff;
            if (names_equal(name, "compatible") && len >= 18) {
                if (string_has_prefix((const char *)ptr, "simple-framebuffer")) {
                    node_is_simplefb = 1;
                    node_depth = depth;
                }
            } else if (node_is_simplefb && depth == node_depth) {
                if (names_equal(name, "reg")) {
                    if (len >= 16) {
                        out->base = be64(ptr);
                        out->size = be64(ptr + 8);
                    } else if (len >= 8) {
                        out->base = (uint64_t)be32(ptr);
                        out->size = (uint64_t)be32(ptr + 4);
                    }
                } else if (names_equal(name, "width") && len >= 4) {
                    out->width = be32(ptr);
                } else if (names_equal(name, "height") && len >= 4) {
                    out->height = be32(ptr);
                } else if (names_equal(name, "stride") && len >= 4) {
                    out->stride = be32(ptr);
                } else if (names_equal(name, "format")) {
                    if (string_has_prefix((const char *)ptr, "a8r8g8b8") ||
                        string_has_prefix((const char *)ptr, "x8r8g8b8")) {
                        out->bpp = 32;
                        out->red_pos = 16;
                        out->red_size = 8;
                        out->green_pos = 8;
                        out->green_size = 8;
                        out->blue_pos = 0;
                        out->blue_size = 8;
                    } else if (string_has_prefix((const char *)ptr, "r5g6b5")) {
                        out->bpp = 16;
                        out->red_pos = 11;
                        out->red_size = 5;
                        out->green_pos = 5;
                        out->green_size = 6;
                        out->blue_pos = 0;
                        out->blue_size = 5;
                    }
                }
            }

            ptr = align4(ptr + len);
            continue;
        }

        if (token == FDT_NOP) continue;
        if (token == FDT_END) break;
        return -1;
    }

    return -1;
}
