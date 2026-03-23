#ifndef FONT_H
#define FONT_H

#include "lib/base.h"

/* =========================================================================
 * NumOS Bitmap Font System
 *
 * Loads a PSF1 or PSF2 bitmap font embedded in the kernel binary at
 * build time.  The font is used by the framebuffer driver for all text
 * rendering.  font_init() is the very first call in kernel_main() so
 * every subsequent message - including the boot banner - uses it.
 *
 * Supported formats:
 *   PSF1  - classic Linux console font (8 x N pixels, up to 512 glyphs)
 *   PSF2  - modern Linux console font (any width/height, Unicode table)
 *
 * Recommended free sources:
 *   Terminus Font (clean, many sizes 6x12 -> 32x64):
 *     https://terminus-font.sourceforge.net/
 *     Grab the .psf.gz files from the "psf" package.
 *
 *   Spleen (pixel-perfect, 5x8 -> 32x64):
 *     https://github.com/fcambus/spleen
 *     Releases include ready-to-use .psf files.
 *
 *   Linux system fonts (already on your machine):
 *     /usr/share/consolefonts/   on Debian/Ubuntu
 *     /usr/lib/kbd/consolefonts/ on Arch/Fedora
 *     Pick any .psf.gz, run: gunzip font.psf.gz
 *
 *   GNU Unifont (full Unicode block coverage):
 *     https://unifoundry.com/unifont/
 *
 * After downloading, run:
 *   python3 tools/font2c.py myfile.psf > src/drivers/font_data.h
 * Then rebuild.
 * ======================================================================= */

/* ---- PSF1 ------------------------------------------------------------- */
#define PSF1_MAGIC0     0x36
#define PSF1_MAGIC1     0x04
#define PSF1_MODE512    0x01   /* 512-glyph mode (otherwise 256)          */
#define PSF1_MODEHASTAB 0x02   /* Unicode translation table present       */

struct psf1_header {
    uint8_t magic[2];
    uint8_t mode;
    uint8_t char_size;         /* bytes per glyph == glyph height; width=8 */
} __attribute__((packed));

/* ---- PSF2 ------------------------------------------------------------- */
/* Magic bytes on disk: 0x72 0xb5 0x4a 0x86 (little-endian uint32) */
#define PSF2_MAGIC      0x864ab572UL
#define PSF2_HAS_UNICODE_TABLE 0x01

struct psf2_header {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;      /* offset to first glyph                    */
    uint32_t flags;
    uint32_t num_glyphs;
    uint32_t bytes_per_glyph;
    uint32_t height;           /* pixels                                   */
    uint32_t width;            /* pixels                                   */
} __attribute__((packed));

/* ---- Runtime font descriptor ------------------------------------------ */
struct font_info {
    int            valid;
    int            width;          /* pixels per glyph                     */
    int            height;         /* pixels per glyph                     */
    int            bytes_per_glyph;
    int            bytes_per_row;  /* ceil(width / 8)                      */
    int            num_glyphs;
    int            first_char;     /* codepoint of glyph 0 in glyph_data   */
    const uint8_t *glyph_data;     /* pointer into the embedded font blob  */
};

/* =========================================================================
 * Public API
 * ======================================================================= */

/*
 * font_init - parse the embedded font blob and populate the runtime
 * descriptor.  Call this as the very first thing in kernel_main() before
 * any framebuffer or VGA output so every message uses the custom font.
 *
 * font_data / font_size are produced by tools/font2c.py and live in
 * src/drivers/font_data.h as a static uint8_t array.
 */
void font_init(const void *font_data, size_t font_size);

/* font_is_loaded - returns 1 after a successful font_init(), 0 otherwise. */
int font_is_loaded(void);

/* font_get_info - fill *out with the current font dimensions. */
void font_get_info(struct font_info *out);

/*
 * font_draw_char - render one glyph at pixel (x, y).
 * bg == FB_TRANSPARENT skips background pixels (compositing mode).
 * scale >= 1 multiplies each pixel into a scale×scale square.
 */
void font_draw_char(char c, int x, int y,
                    uint32_t fg, uint32_t bg, int scale);

/*
 * font_draw_string - render a null-terminated string starting at (x, y).
 * Handles '\n' (new line), '\r' (carriage return), '\t' (4-space tab).
 */
void font_draw_string(const char *s, int x, int y,
                      uint32_t fg, uint32_t bg, int scale);

/* Glyph dimensions in pixels (0 before font_init succeeds). */
int font_char_width(void);
int font_char_height(void);

/* Pixel width of string s at the given scale. */
int font_string_width(const char *s, int scale);

#endif /* FONT_H */
