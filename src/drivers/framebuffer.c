/*
 * framebuffer.c - Bochs Graphics Adapter (BGA) Framebuffer Driver
 *
 * Overview
 * --------
 * The QEMU/Bochs VGA device (PCI vendor 0x1234 / device 0x1111) exposes two
 * 16-bit I/O ports:
 *   0x01CE  BGA_INDEX_PORT  - index register selector
 *   0x01CF  BGA_DATA_PORT   - data read/write
 *
 * Setting a video mode:
 *   1. Write 0 to BGA_REG_ENABLE  (disable BGA while configuring)
 *   2. Write X resolution to BGA_REG_XRES
 *   3. Write Y resolution to BGA_REG_YRES
 *   4. Write colour depth  to BGA_REG_BPP
 *   5. Write BGA_ENABLED | BGA_LFB_ENABLED to BGA_REG_ENABLE
 *
 * The physical base of the linear framebuffer is NOT fixed — it must be
 * read from PCI BAR0 (bits [31:4]) of the 0x1234:0x1111 device.
 * pci_config_read32() from device.h is used for this purpose.
 *
 * Colour format: 32 BPP, 0x00RRGGBB
 *   offset = y * FB_WIDTH + x
 *   mem[offset] = (R<<16)|(G<<8)|B
 */

#include "drivers/framebuffer.h"
#include "drivers/vga.h"
#include "drivers/device.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* =========================================================================
 * Embedded 8 × 8 bitmap font (public domain)
 *
 * 96 glyphs covering printable ASCII 0x20 – 0x7F.
 * Each glyph is 8 bytes (one per row); bit 7 of each byte is the leftmost
 * pixel, bit 0 is the rightmost.
 * ======================================================================= */
static const uint8_t font8x8[96][8] = {
    /* 0x20  space  */ { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00 },
    /* 0x21  !      */ { 0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00 },
    /* 0x22  "      */ { 0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00 },
    /* 0x23  #      */ { 0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00 },
    /* 0x24  $      */ { 0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00 },
    /* 0x25  %      */ { 0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00 },
    /* 0x26  &      */ { 0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00 },
    /* 0x27  '      */ { 0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00 },
    /* 0x28  (      */ { 0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00 },
    /* 0x29  )      */ { 0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00 },
    /* 0x2A  *      */ { 0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00 },
    /* 0x2B  +      */ { 0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00 },
    /* 0x2C  ,      */ { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06 },
    /* 0x2D  -      */ { 0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00 },
    /* 0x2E  .      */ { 0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00 },
    /* 0x2F  /      */ { 0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00 },
    /* 0x30  0      */ { 0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00 },
    /* 0x31  1      */ { 0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00 },
    /* 0x32  2      */ { 0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00 },
    /* 0x33  3      */ { 0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00 },
    /* 0x34  4      */ { 0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00 },
    /* 0x35  5      */ { 0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00 },
    /* 0x36  6      */ { 0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00 },
    /* 0x37  7      */ { 0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00 },
    /* 0x38  8      */ { 0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00 },
    /* 0x39  9      */ { 0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00 },
    /* 0x3A  :      */ { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00 },
    /* 0x3B  ;      */ { 0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06 },
    /* 0x3C  <      */ { 0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00 },
    /* 0x3D  =      */ { 0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00 },
    /* 0x3E  >      */ { 0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00 },
    /* 0x3F  ?      */ { 0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00 },
    /* 0x40  @      */ { 0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00 },
    /* 0x41  A      */ { 0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00 },
    /* 0x42  B      */ { 0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00 },
    /* 0x43  C      */ { 0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00 },
    /* 0x44  D      */ { 0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00 },
    /* 0x45  E      */ { 0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00 },
    /* 0x46  F      */ { 0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00 },
    /* 0x47  G      */ { 0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00 },
    /* 0x48  H      */ { 0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00 },
    /* 0x49  I      */ { 0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 },
    /* 0x4A  J      */ { 0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00 },
    /* 0x4B  K      */ { 0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00 },
    /* 0x4C  L      */ { 0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00 },
    /* 0x4D  M      */ { 0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00 },
    /* 0x4E  N      */ { 0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00 },
    /* 0x4F  O      */ { 0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00 },
    /* 0x50  P      */ { 0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00 },
    /* 0x51  Q      */ { 0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00 },
    /* 0x52  R      */ { 0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00 },
    /* 0x53  S      */ { 0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00 },
    /* 0x54  T      */ { 0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 },
    /* 0x55  U      */ { 0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00 },
    /* 0x56  V      */ { 0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00 },
    /* 0x57  W      */ { 0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00 },
    /* 0x58  X      */ { 0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00 },
    /* 0x59  Y      */ { 0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00 },
    /* 0x5A  Z      */ { 0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00 },
    /* 0x5B  [      */ { 0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00 },
    /* 0x5C  \      */ { 0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00 },
    /* 0x5D  ]      */ { 0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00 },
    /* 0x5E  ^      */ { 0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00 },
    /* 0x5F  _      */ { 0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF },
    /* 0x60  `      */ { 0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00 },
    /* 0x61  a      */ { 0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00 },
    /* 0x62  b      */ { 0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00 },
    /* 0x63  c      */ { 0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00 },
    /* 0x64  d      */ { 0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00 },
    /* 0x65  e      */ { 0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00 },
    /* 0x66  f      */ { 0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00 },
    /* 0x67  g      */ { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F },
    /* 0x68  h      */ { 0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00 },
    /* 0x69  i      */ { 0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00 },
    /* 0x6A  j      */ { 0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E },
    /* 0x6B  k      */ { 0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00 },
    /* 0x6C  l      */ { 0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00 },
    /* 0x6D  m      */ { 0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00 },
    /* 0x6E  n      */ { 0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00 },
    /* 0x6F  o      */ { 0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00 },
    /* 0x70  p      */ { 0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F },
    /* 0x71  q      */ { 0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78 },
    /* 0x72  r      */ { 0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00 },
    /* 0x73  s      */ { 0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00 },
    /* 0x74  t      */ { 0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00 },
    /* 0x75  u      */ { 0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00 },
    /* 0x76  v      */ { 0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00 },
    /* 0x77  w      */ { 0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00 },
    /* 0x78  x      */ { 0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00 },
    /* 0x79  y      */ { 0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F },
    /* 0x7A  z      */ { 0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00 },
    /* 0x7B  {      */ { 0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00 },
    /* 0x7C  |      */ { 0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00 },
    /* 0x7D  }      */ { 0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00 },
    /* 0x7E  ~      */ { 0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00 },
    /* 0x7F  DEL    */ { 0xFF,0x81,0xBD,0xA5,0xBD,0x81,0xFF,0xFF },
};

/* =========================================================================
 * Module state
 * ======================================================================= */

static uint32_t *fb_mem     = NULL;  /* virtual base of the linear framebuffer */
static int       fb_ready   = 0;     /* non-zero when BGA is active            */
static uint32_t  fb_phys    = 0;     /* physical base address from PCI BAR0    */

/* ---- Framebuffer console state ------------------------------------------ */
static int     con_x0, con_y0;          /* top-left pixel of console area     */
static int     con_w,  con_h;           /* pixel dimensions                   */
static int     con_cx, con_cy;          /* current character column / row     */
static int     con_cols, con_rows;      /* columns and rows in chars          */
static int     con_scale;               /* character scale factor             */
static uint32_t con_fg, con_bg;         /* foreground / background colour     */

/* =========================================================================
 * BGA port helpers
 * ======================================================================= */

static void bga_write(uint16_t reg, uint16_t val) {
    outw(BGA_INDEX_PORT, reg);
    outw(BGA_DATA_PORT,  val);
}

static uint16_t bga_read(uint16_t reg) {
    outw(BGA_INDEX_PORT, reg);
    return inw(BGA_DATA_PORT);
}

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * fb_init - detect the BGA device, read the framebuffer address from PCI
 * BAR0, set 1024×768×32 mode, map the pages, and clear to black.
 *
 * Falls back gracefully: if anything fails fb_ready stays 0 and all other
 * fb_* calls become no-ops.
 */
void fb_init(void) {
    vga_writestring("FB: Probing BGA framebuffer...\n");

    /* ---- Step 1: verify BGA ID ----------------------------------------- */
    uint16_t bga_id = bga_read(BGA_REG_ID);
    if (bga_id < BGA_ID_MIN || bga_id > BGA_ID_MAX) {
        vga_writestring("FB: BGA not detected (id=0x");
        print_hex32(bga_id);
        vga_writestring(") — staying in VGA text mode\n");
        return;
    }
    vga_writestring("FB: BGA version 0x");
    print_hex32(bga_id);
    vga_writestring(" found\n");

    /* ---- Step 2: read PCI BAR0 for the framebuffer physical address ------ */
    /*
     * The wiki says: "The address of the framebuffer is not fixed, and must
     * be read from the first PCI base address register (BAR0) of device
     * 0x1234:0x1111."  BAR0 bits [3:0] are flags; mask them out.
     *
     * We scan the PCI device table populated by device_init() first.
     * If not found there (e.g. device_init not yet called), fall back to a
     * direct PCI config read on buses 0-7.
     */
    fb_phys = 0;

    /* Try device table */
    struct device_entry *gpu_list[8];
    int gpu_count = device_get_by_type(DEVICE_TYPE_GPU, gpu_list, 8);
    for (int i = 0; i < gpu_count; i++) {
        if (gpu_list[i]->vendor_id == 0x1234 &&
            gpu_list[i]->device_id == 0x1111) {
            fb_phys = gpu_list[i]->pci_bar[0] & 0xFFFFFFF0U;
            vga_writestring("FB: BAR0 from device table: 0x");
            print_hex32(fb_phys);
            vga_writestring("\n");
            break;
        }
    }

    /* Direct PCI scan fallback */
    if (!fb_phys) {
        for (uint8_t bus = 0; bus < 8 && !fb_phys; bus++) {
            for (uint8_t slot = 0; slot < 32 && !fb_phys; slot++) {
                uint32_t id = pci_config_read32(bus, slot, 0, 0x00);
                if ((id & 0xFFFF) == 0x1234 && ((id >> 16) & 0xFFFF) == 0x1111) {
                    uint32_t bar0 = pci_config_read32(bus, slot, 0, 0x10);
                    fb_phys = bar0 & 0xFFFFFFF0U;
                    vga_writestring("FB: BAR0 from PCI scan: 0x");
                    print_hex32(fb_phys);
                    vga_writestring("\n");
                }
            }
        }
    }

    if (!fb_phys) {
        /* Last resort: QEMU ISA default */
        fb_phys = 0xE0000000U;
        vga_writestring("FB: BAR0 not found; using QEMU default 0xE0000000\n");
    }

    /* ---- Step 3: program BGA registers ----------------------------------- */
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES,   FB_WIDTH);
    bga_write(BGA_REG_YRES,   FB_HEIGHT);
    bga_write(BGA_REG_BPP,    FB_BPP);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    /* ---- Step 4: map framebuffer pages ----------------------------------- */
    size_t fb_bytes = (size_t)FB_WIDTH * FB_HEIGHT * (FB_BPP / 8);
    size_t fb_pages = (fb_bytes + PAGE_SIZE - 1) / PAGE_SIZE;

    vga_writestring("FB: Mapping ");
    print_dec(fb_pages);
    vga_writestring(" pages at phys 0x");
    print_hex32(fb_phys);
    vga_writestring("\n");

    for (size_t i = 0; i < fb_pages; i++) {
        uint64_t phys = (uint64_t)fb_phys + i * PAGE_SIZE;
        uint64_t virt = phys;   /* identity map this region */
        if (paging_map_page(virt, phys,
                            PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE) != 0) {
            /* Page may already be mapped — not an error on repeated calls */
        }
    }

    fb_mem   = (uint32_t *)(uintptr_t)fb_phys;
    fb_ready = 1;

    /* Clear to black */
    fb_fill(FB_BLACK);

    vga_writestring("FB: Ready — ");
    print_dec(FB_WIDTH);
    vga_writestring("x");
    print_dec(FB_HEIGHT);
    vga_writestring("x");
    print_dec(FB_BPP);
    vga_writestring(" bpp, mem=0x");
    print_hex32(fb_phys);
    vga_writestring("\n");
}

int fb_is_available(void) { return fb_ready; }

/* =========================================================================
 * Pixel operations
 * ======================================================================= */

void fb_set_pixel(int x, int y, uint32_t color) {
    if (!fb_ready) return;
    if (x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return;
    fb_mem[y * FB_WIDTH + x] = color;
}

uint32_t fb_get_pixel(int x, int y) {
    if (!fb_ready || x < 0 || x >= FB_WIDTH || y < 0 || y >= FB_HEIGHT) return 0;
    return fb_mem[y * FB_WIDTH + x];
}

/* =========================================================================
 * Fill helpers
 * ======================================================================= */

void fb_fill(uint32_t color) {
    if (!fb_ready) return;
    int total = FB_WIDTH * FB_HEIGHT;
    for (int i = 0; i < total; i++) fb_mem[i] = color;
}

void fb_fill_rect(int x, int y, int w, int h, uint32_t color) {
    if (!fb_ready) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > FB_WIDTH)  w = FB_WIDTH  - x;
    if (y + h > FB_HEIGHT) h = FB_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    for (int dy = 0; dy < h; dy++) {
        uint32_t *row = fb_mem + (y + dy) * FB_WIDTH + x;
        for (int dx = 0; dx < w; dx++) row[dx] = color;
    }
}

/* Linear blend: top colour -> bottom colour, vertically */
void fb_gradient_v(int x, int y, int w, int h, uint32_t top, uint32_t bot) {
    if (!fb_ready || h <= 0) return;
    uint32_t tr = (top >> 16) & 0xFF, tg = (top >> 8) & 0xFF, tb = top & 0xFF;
    uint32_t br = (bot >> 16) & 0xFF, bg = (bot >> 8) & 0xFF, bb = bot & 0xFF;
    for (int dy = 0; dy < h; dy++) {
        uint32_t r = tr + (br - tr) * dy / h;
        uint32_t g = tg + (bg - tg) * dy / h;
        uint32_t b = tb + (bb - tb) * dy / h;
        fb_fill_rect(x, y + dy, w, 1, FB_COLOR(r, g, b));
    }
}

/* Left colour -> right colour, horizontally */
void fb_gradient_h(int x, int y, int w, int h, uint32_t left, uint32_t right) {
    if (!fb_ready || w <= 0) return;
    uint32_t lr = (left  >> 16) & 0xFF, lg = (left  >> 8) & 0xFF, lb = left  & 0xFF;
    uint32_t rr = (right >> 16) & 0xFF, rg = (right >> 8) & 0xFF, rb = right & 0xFF;
    for (int dx = 0; dx < w; dx++) {
        uint32_t r = lr + (rr - lr) * dx / w;
        uint32_t g = lg + (rg - lg) * dx / w;
        uint32_t b = lb + (rb - lb) * dx / w;
        fb_fill_rect(x + dx, y, 1, h, FB_COLOR(r, g, b));
    }
}

/* =========================================================================
 * Shapes
 * ======================================================================= */

void fb_draw_hline(int x, int y, int len, uint32_t color) {
    fb_fill_rect(x, y, len, 1, color);
}

void fb_draw_vline(int x, int y, int len, uint32_t color) {
    fb_fill_rect(x, y, 1, len, color);
}

void fb_draw_rect(int x, int y, int w, int h, uint32_t color) {
    fb_draw_hline(x,         y,         w, color);
    fb_draw_hline(x,         y + h - 1, w, color);
    fb_draw_vline(x,         y,         h, color);
    fb_draw_vline(x + w - 1, y,         h, color);
}

void fb_draw_rect_thick(int x, int y, int w, int h, int thick, uint32_t color) {
    for (int t = 0; t < thick; t++) {
        fb_draw_rect(x + t, y + t, w - 2 * t, h - 2 * t, color);
    }
}

/* Bresenham line */
void fb_draw_line(int x0, int y0, int x1, int y1, uint32_t color) {
    if (!fb_ready) return;
    int dx  =  (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int dy  = -((y1 > y0) ? (y1 - y0) : (y0 - y1));
    int sx  =  (x0 < x1) ? 1 : -1;
    int sy  =  (y0 < y1) ? 1 : -1;
    int err =  dx + dy;
    while (1) {
        fb_set_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void fb_fill_circle(int cx, int cy, int r, uint32_t color) {
    if (!fb_ready) return;
    for (int y = -r; y <= r; y++) {
        int xw = 0;
        /* compute x width at this y using integer sqrt approximation */
        int yy = y * y;
        int rr = r * r;
        while ((xw + 1) * (xw + 1) + yy <= rr) xw++;
        fb_fill_rect(cx - xw, cy + y, xw * 2 + 1, 1, color);
    }
}

/* Rounded rectangle fill */
void fb_fill_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
    if (!fb_ready) return;
    /* Fill centre strips */
    fb_fill_rect(x + r, y,         w - 2 * r, h,         color);
    fb_fill_rect(x,     y + r,     r,          h - 2 * r, color);
    fb_fill_rect(x + w - r, y + r, r,          h - 2 * r, color);
    /* Corners */
    for (int cy2 = 0; cy2 < r; cy2++) {
        int xw = 0;
        int yy = (r - cy2 - 1) * (r - cy2 - 1);
        int rr = r * r;
        while ((xw + 1) * (xw + 1) + yy <= rr) xw++;
        /* Top-left */
        fb_fill_rect(x + r - xw - 1, y + cy2, xw + 1, 1, color);
        /* Top-right */
        fb_fill_rect(x + w - r,      y + cy2, xw + 1, 1, color);
        /* Bottom-left */
        fb_fill_rect(x + r - xw - 1, y + h - 1 - cy2, xw + 1, 1, color);
        /* Bottom-right */
        fb_fill_rect(x + w - r,      y + h - 1 - cy2, xw + 1, 1, color);
    }
}

void fb_draw_rounded_rect(int x, int y, int w, int h, int r, uint32_t color) {
    if (!fb_ready) return;
    fb_draw_hline(x + r, y,         w - 2 * r, color);
    fb_draw_hline(x + r, y + h - 1, w - 2 * r, color);
    fb_draw_vline(x,         y + r, h - 2 * r, color);
    fb_draw_vline(x + w - 1, y + r, h - 2 * r, color);
    /* Approximate corners with circles */
    for (int a = 0; a <= r; a++) {
        int yy = (r - a) * (r - a);
        int rr = r * r;
        int xw = 0;
        while ((xw + 1) * (xw + 1) + yy <= rr) xw++;
        fb_set_pixel(x + r - xw, y + a,         color);
        fb_set_pixel(x + w - 1 - r + xw, y + a, color);
        fb_set_pixel(x + r - xw, y + h - 1 - a, color);
        fb_set_pixel(x + w - 1 - r + xw, y + h - 1 - a, color);
    }
}

/* =========================================================================
 * Text rendering
 * ======================================================================= */

void fb_draw_char(char c, int x, int y, uint32_t fg, uint32_t bg, int scale) {
    if (!fb_ready) return;
    if (c < 0x20 || c > 0x7F) c = '?';
    const uint8_t *glyph = font8x8[c - 0x20];

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            int set = (glyph[row] >> (7 - col)) & 1;
            uint32_t color = set ? fg : bg;
            if (color == FB_TRANSPARENT) continue;
            if (scale == 1) {
                fb_set_pixel(x + col, y + row, color);
            } else {
                fb_fill_rect(x + col * scale, y + row * scale,
                             scale, scale, color);
            }
        }
    }
}

void fb_draw_string(const char *s, int x, int y,
                    uint32_t fg, uint32_t bg, int scale) {
    if (!fb_ready || !s) return;
    int cx = x, cy = y;
    int cw = 8 * scale;
    int ch = 8 * scale;
    while (*s) {
        if (*s == '\n') { cx = x; cy += ch; }
        else if (*s == '\r') { cx = x; }
        else if (*s == '\t') {
            int tab = 4 * cw;
            cx = ((cx / tab) + 1) * tab;
        } else {
            fb_draw_char(*s, cx, cy, fg, bg, scale);
            cx += cw;
        }
        s++;
    }
}

int fb_string_width(const char *s, int scale) {
    if (!s) return 0;
    int len = 0;
    while (*s++) len++;
    return len * 8 * scale;
}

/* =========================================================================
 * Framebuffer console
 *
 * A simple scrolling text terminal inside a rectangular region.
 * Scroll: when the cursor reaches the bottom, all rows shift up by one
 * character height and the bottom row is cleared.
 * ======================================================================= */

void fb_con_init(int x, int y, int w, int h,
                 uint32_t fg, uint32_t bg, int scale) {
    con_x0    = x;
    con_y0    = y;
    con_w     = w;
    con_h     = h;
    con_fg    = fg;
    con_bg    = bg;
    con_scale = scale;
    con_cx    = 0;
    con_cy    = 0;
    con_cols  = w / (8 * scale);
    con_rows  = h / (8 * scale);
}

void fb_con_clear(void) {
    fb_fill_rect(con_x0, con_y0, con_w, con_h, con_bg);
    con_cx = 0;
    con_cy = 0;
}

static void con_scroll_up(void) {
    if (!fb_ready) return;
    int ch = 8 * con_scale;
    /* Shift framebuffer rows up by one character height */
    for (int row = 0; row < con_rows - 1; row++) {
        for (int py = 0; py < ch; py++) {
            uint32_t *src = fb_mem + (con_y0 + (row + 1) * ch + py) * FB_WIDTH + con_x0;
            uint32_t *dst = fb_mem + (con_y0 +  row      * ch + py) * FB_WIDTH + con_x0;
            for (int px = 0; px < con_w; px++) dst[px] = src[px];
        }
    }
    /* Clear last row */
    fb_fill_rect(con_x0, con_y0 + (con_rows - 1) * ch, con_w, ch, con_bg);
}

void fb_con_putchar(char c) {
    if (!fb_ready) return;
    int cw = 8 * con_scale;
    int ch = 8 * con_scale;

    if (c == '\n') {
        con_cx = 0;
        con_cy++;
    } else if (c == '\r') {
        con_cx = 0;
    } else if (c == '\b') {
        if (con_cx > 0) {
            con_cx--;
            fb_fill_rect(con_x0 + con_cx * cw, con_y0 + con_cy * ch,
                         cw, ch, con_bg);
        }
    } else {
        if (con_cx >= con_cols) { con_cx = 0; con_cy++; }
        if (con_cy >= con_rows) { con_scroll_up(); con_cy = con_rows - 1; }
        fb_draw_char(c,
                     con_x0 + con_cx * cw,
                     con_y0 + con_cy * ch,
                     con_fg, con_bg, con_scale);
        con_cx++;
    }

    if (con_cy >= con_rows) { con_scroll_up(); con_cy = con_rows - 1; }
}

void fb_con_print(const char *s) {
    if (!s) return;
    while (*s) fb_con_putchar(*s++);
}

/* =========================================================================
 * High-level UI helpers
 * ======================================================================= */

/*
 * fb_draw_panel - draw a titled window panel.
 * title_bg: colour for the title bar strip.
 */
void fb_draw_panel(int x, int y, int w, int h,
                   const char *title, uint32_t title_bg) {
    /* Shadow */
    fb_fill_rect(x + 4, y + 4, w, h, FB_COLOR(8, 8, 16));
    /* Body */
    fb_fill_rounded_rect(x, y, w, h, 6, FB_PANEL);
    fb_draw_rounded_rect(x, y, w, h, 6, FB_BORDER);
    /* Title bar */
    fb_fill_rect(x + 1, y + 1, w - 2, 28, title_bg);
    fb_draw_hline(x + 1, y + 29, w - 2, FB_BORDER);
    if (title) {
        int tx = x + 10;
        int ty = y + 7;
        fb_draw_string(title, tx, ty, FB_WHITE, FB_TRANSPARENT, FB_SCALE_SMALL);
    }
}

/*
 * fb_draw_button - draw a rounded button with label.
 */
void fb_draw_button(int x, int y, int w, int h,
                    const char *label, uint32_t bg, uint32_t fg) {
    fb_fill_rounded_rect(x, y, w, h, 4, bg);
    fb_draw_rounded_rect(x, y, w, h, 4, FB_BORDER);
    if (label) {
        int lw  = fb_string_width(label, FB_SCALE_SMALL);
        int lx  = x + (w - lw) / 2;
        int ly  = y + (h - 8)  / 2;
        fb_draw_string(label, lx, ly, fg, FB_TRANSPARENT, FB_SCALE_SMALL);
    }
}

void fb_draw_separator(int x, int y, int w, uint32_t color) {
    fb_draw_hline(x, y, w, FB_COLOR(
        ((color >> 16) & 0xFF) / 2,
        ((color >>  8) & 0xFF) / 2,
        ((color      ) & 0xFF) / 2));
    fb_draw_hline(x, y + 1, w, color);
}

/* =========================================================================
 * Taskbar refresh  (call periodically to update the uptime counter)
 * ======================================================================= */

void fb_update_taskbar(void) {
    if (!fb_ready) return;

    uint64_t uptime_s = timer_get_uptime_seconds();
    uint64_t h        =  uptime_s / 3600;
    uint64_t m        = (uptime_s % 3600) / 60;
    uint64_t s        =  uptime_s % 60;

    /* Format "UP  HH:MM:SS" */
    char buf[32];
    int  pos = 0;
    const char *pre = "UP  ";
    for (int i = 0; pre[i]; i++) buf[pos++] = pre[i];

    /* HH */
    buf[pos++] = '0' + (char)(h / 10);
    buf[pos++] = '0' + (char)(h % 10);
    buf[pos++] = ':';
    /* MM */
    buf[pos++] = '0' + (char)(m / 10);
    buf[pos++] = '0' + (char)(m % 10);
    buf[pos++] = ':';
    /* SS */
    buf[pos++] = '0' + (char)(s / 10);
    buf[pos++] = '0' + (char)(s % 10);
    buf[pos]   = '\0';

    int bw = (int)(pos * 8) + 20;
    int bx = FB_WIDTH - bw - 8;
    int by = 6;

    fb_fill_rect(bx, by, bw + 4, 16, FB_TASKBAR);   /* clear old value */
    fb_draw_string(buf, bx, by, FB_DIM, FB_TRANSPARENT, FB_SCALE_SMALL);
}

/* =========================================================================
 * Desktop  — drawn once after boot
 * ======================================================================= */

void fb_draw_desktop(void) {
    if (!fb_ready) return;

    /* ---- Background ------------------------------------------------------- */
    fb_gradient_v(0, 0, FB_WIDTH, FB_HEIGHT, FB_COLOR(12, 14, 28), FB_COLOR(22, 24, 48));

    /* ---- Decorative accent lines ------------------------------------------ */
    for (int i = 0; i < 3; i++) {
        uint32_t c = FB_COLOR(30 + i * 8, 40 + i * 8, 80 + i * 12);
        fb_draw_hline(0, 80 + i * 60, FB_WIDTH, c);
    }

    /* ---- Taskbar ---------------------------------------------------------- */
    int tb_h = 40;
    fb_fill_rect(0, 0, FB_WIDTH, tb_h, FB_TASKBAR);
    fb_gradient_h(0, 0, FB_WIDTH / 3, tb_h,
                  FB_COLOR(50, 60, 130), FB_TASKBAR);

    /* OS name + version */
    fb_draw_string("NumOS", 12, 11, FB_WHITE, FB_TRANSPARENT, FB_SCALE_NORMAL);
    fb_draw_string("v0.8.0-beta", 94, 15, FB_ACCENT, FB_TRANSPARENT, FB_SCALE_SMALL);

    /* Status indicators */
    fb_fill_circle(220, 20, 5, FB_SUCCESS);
    fb_draw_string("RUNNING", 232, 13, FB_SUCCESS, FB_TRANSPARENT, FB_SCALE_SMALL);

    fb_draw_hline(0, tb_h, FB_WIDTH, FB_ACCENT);

    /* Uptime (initial draw) */
    fb_update_taskbar();

    /* ---- Left info panel -------------------------------------------------- */
    int lp_x = 20, lp_y = 58, lp_w = 290, lp_h = 400;
    fb_draw_panel(lp_x, lp_y, lp_w, lp_h, " System Information", FB_TITLE_BG);

    int iy = lp_y + 40;
    int ix = lp_x + 14;

    /* Section: Hardware */
    fb_draw_string("HARDWARE", ix, iy, FB_ACCENT, FB_TRANSPARENT, FB_SCALE_SMALL);
    iy += 14;
    fb_draw_hline(ix, iy, lp_w - 28, FB_BORDER);
    iy += 6;

    fb_draw_string("Architecture", ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("x86-64 (Long Mode)", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    fb_draw_string("Display", ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("BGA 1024x768x32", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    fb_draw_string("Memory", ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("4096 MB (QEMU)", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 20;

    /* Section: Kernel */
    fb_draw_string("KERNEL", ix, iy, FB_ACCENT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 14;
    fb_draw_hline(ix, iy, lp_w - 28, FB_BORDER); iy += 6;

    fb_draw_string("Version", ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("NumOS 0.8.0-beta", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    fb_draw_string("Paging",  ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("4-Level (PML4)", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    fb_draw_string("Heap", ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("128 MB (best-fit)", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    fb_draw_string("Scheduler", ix, iy, FB_DIM,  FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("Round-Robin (100 Hz)", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    fb_draw_string("Filesystem", ix, iy, FB_DIM, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 12;
    fb_draw_string("FAT32 (ATA PIO)", ix + 10, iy, FB_TEXT, FB_TRANSPARENT, FB_SCALE_SMALL); iy += 16;

    /* Status badges */
    iy = lp_y + lp_h - 40;
    int badges[] = { 0, 1, 2, 3 };
    const char *badge_labels[] = { "GDT", "IDT", "PMM", "VMM" };
    (void)badges;
    for (int b = 0; b < 4; b++) {
        int bx2 = ix + b * 66;
        fb_fill_rounded_rect(bx2, iy, 58, 20, 4, FB_COLOR(20, 80, 30));
        fb_draw_rounded_rect(bx2, iy, 58, 20, 4, FB_SUCCESS);
        int lw2 = fb_string_width(badge_labels[b], FB_SCALE_SMALL);
        fb_draw_string(badge_labels[b],
                       bx2 + (58 - lw2) / 2, iy + 6,
                       FB_SUCCESS, FB_TRANSPARENT, FB_SCALE_SMALL);
    }

    /* ---- Terminal / menu panel -------------------------------------------- */
    int tp_x = 330, tp_y = 58, tp_w = 670, tp_h = 400;
    fb_draw_panel(tp_x, tp_y, tp_w, tp_h, " Interactive Shell", FB_COLOR(20, 40, 80));

    /* Prompt area set up as the framebuffer console */
    fb_con_init(tp_x + 10, tp_y + 36,
                tp_w - 20, tp_h - 46,
                FB_TEXT, FB_PANEL,
                FB_SCALE_SMALL);

    fb_con_print("NumOS kernel shell ready.\n");
    fb_con_print("Type a letter to execute:\n\n");
    fb_con_print("  [S] Scroll mode       [L] List root dir\n");
    fb_con_print("  [I] Syscall stats     [P] Process list\n");
    fb_con_print("  [D] Device list       [R] Re-run ELF\n");
    fb_con_print("  [H] Halt\n\n");
    fb_con_print("> ");

    /* ---- Bottom status bar ------------------------------------------------ */
    int sb_y = FB_HEIGHT - 28;
    fb_fill_rect(0, sb_y, FB_WIDTH, 28, FB_COLOR(14, 16, 32));
    fb_draw_hline(0, sb_y, FB_WIDTH, FB_BORDER);

    fb_draw_string("NumOS  |  BGA Framebuffer Active  |  1024x768x32  |  Kernel Mode",
                   12, sb_y + 9, FB_DIM, FB_TRANSPARENT, FB_SCALE_SMALL);

    /* Build/copyright notice right-aligned */
    const char *copy = "2025 NumOS Project";
    int cw2 = fb_string_width(copy, FB_SCALE_SMALL);
    fb_draw_string(copy, FB_WIDTH - cw2 - 12, sb_y + 9,
                   FB_DIM, FB_TRANSPARENT, FB_SCALE_SMALL);
}