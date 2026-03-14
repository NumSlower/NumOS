/*
 * framebuffer.c - Bochs Graphics Adapter (BGA) Framebuffer Driver
 *
 * The QEMU/Bochs VGA device (PCI 0x1234:0x1111) exposes two 16-bit I/O ports:
 *   0x01CE  BGA_INDEX_PORT  – register selector
 *   0x01CF  BGA_DATA_PORT   – data read/write
 *
 * Mode set sequence:
 *   1. Write 0 (disabled) to BGA_REG_ENABLE
 *   2. Write X/Y resolution and BPP
 *   3. Write BGA_ENABLED | BGA_LFB_ENABLED to BGA_REG_ENABLE
 *
 * The framebuffer base address is read from PCI BAR0 (bits [31:4]) of
 * device 0x1234:0x1111.  It must NOT be hardcoded.
 *
 * Colour format: 32 BPP, 0x00RRGGBB.
 */

#include "drivers/framebuffer.h"
#include "drivers/vga.h"
#include "drivers/device.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* =========================================================================
 * Embedded 8x8 bitmap font — public domain, 96 glyphs (0x20–0x7F).
 * Each glyph: 8 bytes, one per row; bit 7 = leftmost pixel.
 * ======================================================================= */
static const uint8_t font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x20 space */
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, /* 0x21 !     */
    {0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00}, /* 0x22 "     */
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, /* 0x23 #     */
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, /* 0x24 $     */
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, /* 0x25 %     */
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, /* 0x26 &     */
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, /* 0x27 '     */
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, /* 0x28 (     */
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, /* 0x29 )     */
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, /* 0x2A *     */
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, /* 0x2B +     */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, /* 0x2C ,     */
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, /* 0x2D -     */
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, /* 0x2E .     */
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, /* 0x2F /     */
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, /* 0x30 0     */
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, /* 0x31 1     */
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, /* 0x32 2     */
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, /* 0x33 3     */
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, /* 0x34 4     */
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, /* 0x35 5     */
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, /* 0x36 6     */
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, /* 0x37 7     */
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, /* 0x38 8     */
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, /* 0x39 9     */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, /* 0x3A :     */
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, /* 0x3B ;     */
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, /* 0x3C <     */
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, /* 0x3D =     */
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, /* 0x3E >     */
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, /* 0x3F ?     */
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, /* 0x40 @     */
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, /* 0x41 A     */
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, /* 0x42 B     */
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, /* 0x43 C     */
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, /* 0x44 D     */
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, /* 0x45 E     */
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, /* 0x46 F     */
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, /* 0x47 G     */
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, /* 0x48 H     */
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x49 I     */
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, /* 0x4A J     */
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, /* 0x4B K     */
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, /* 0x4C L     */
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, /* 0x4D M     */
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, /* 0x4E N     */
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, /* 0x4F O     */
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, /* 0x50 P     */
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, /* 0x51 Q     */
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, /* 0x52 R     */
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, /* 0x53 S     */
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x54 T     */
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, /* 0x55 U     */
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 0x56 V     */
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, /* 0x57 W     */
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, /* 0x58 X     */
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, /* 0x59 Y     */
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, /* 0x5A Z     */
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, /* 0x5B [     */
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, /* 0x5C \     */
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, /* 0x5D ]     */
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, /* 0x5E ^     */
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, /* 0x5F _     */
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, /* 0x60 `     */
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, /* 0x61 a     */
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, /* 0x62 b     */
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, /* 0x63 c     */
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, /* 0x64 d     */
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, /* 0x65 e     */
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, /* 0x66 f     */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, /* 0x67 g     */
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, /* 0x68 h     */
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x69 i     */
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, /* 0x6A j     */
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, /* 0x6B k     */
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, /* 0x6C l     */
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, /* 0x6D m     */
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, /* 0x6E n     */
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, /* 0x6F o     */
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, /* 0x70 p     */
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, /* 0x71 q     */
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, /* 0x72 r     */
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, /* 0x73 s     */
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, /* 0x74 t     */
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, /* 0x75 u     */
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, /* 0x76 v     */
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, /* 0x77 w     */
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, /* 0x78 x     */
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, /* 0x79 y     */
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, /* 0x7A z     */
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, /* 0x7B {     */
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, /* 0x7C |     */
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, /* 0x7D }     */
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, /* 0x7E ~     */
    {0xFF,0x81,0xBD,0xA5,0xBD,0x81,0xFF,0xFF}, /* 0x7F DEL   */
};

/* =========================================================================
 * Module state
 * ======================================================================= */
static uint32_t *fb_mem   = NULL;
static int       fb_ready = 0;
static uint32_t  fb_phys  = 0;

/* Console */
static int     con_x0, con_y0, con_w, con_h;
static int     con_cx, con_cy, con_cols, con_rows, con_scale;
static uint32_t con_fg, con_bg;

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
void fb_init(void) {
    vga_writestring("FB: Probing BGA...\n");

    uint16_t id = bga_read(BGA_REG_ID);
    if (id < BGA_ID_MIN || id > BGA_ID_MAX) {
        vga_writestring("FB: BGA not found (id=0x");
        print_hex32(id);
        vga_writestring(")\n");
        return;
    }
    vga_writestring("FB: BGA id=0x");
    print_hex32(id);
    vga_writestring("\n");

    /* Read BAR0 from PCI (must not be hardcoded per OSDev wiki) */
    fb_phys = 0;

    struct device_entry *gl[8];
    int gc = device_get_by_type(DEVICE_TYPE_GPU, gl, 8);
    for (int i = 0; i < gc; i++) {
        if (gl[i]->vendor_id == 0x1234 && gl[i]->device_id == 0x1111) {
            fb_phys = gl[i]->pci_bar[0] & 0xFFFFFFF0U;
            vga_writestring("FB: BAR0=0x");
            print_hex32(fb_phys);
            vga_writestring("\n");
            break;
        }
    }

    if (!fb_phys) {
        for (uint8_t bus = 0; bus < 8 && !fb_phys; bus++) {
            for (uint8_t slot = 0; slot < 32 && !fb_phys; slot++) {
                uint32_t v = pci_config_read32(bus, slot, 0, 0);
                if ((v & 0xFFFF) == 0x1234 && ((v>>16)&0xFFFF) == 0x1111) {
                    fb_phys = pci_config_read32(bus, slot, 0, 0x10) & 0xFFFFFFF0U;
                    vga_writestring("FB: BAR0 (scan)=0x");
                    print_hex32(fb_phys);
                    vga_writestring("\n");
                }
            }
        }
    }

    if (!fb_phys) {
        fb_phys = 0xE0000000U;
        vga_writestring("FB: BAR0 fallback=0xE0000000\n");
    }

    /* Program mode */
    bga_write(BGA_REG_ENABLE, BGA_DISABLED);
    bga_write(BGA_REG_XRES,   FB_WIDTH);
    bga_write(BGA_REG_YRES,   FB_HEIGHT);
    bga_write(BGA_REG_BPP,    FB_BPP);
    bga_write(BGA_REG_ENABLE, BGA_ENABLED | BGA_LFB_ENABLED);

    /* Map pages (identity map) */
    size_t pages = ((size_t)FB_WIDTH * FB_HEIGHT * 4 + PAGE_SIZE - 1) / PAGE_SIZE;
    for (size_t i = 0; i < pages; i++) {
        uint64_t a = (uint64_t)fb_phys + i * PAGE_SIZE;
        paging_map_page(a, a, PAGE_PRESENT | PAGE_WRITABLE | PAGE_CACHE_DISABLE);
    }

    fb_mem   = (uint32_t *)(uintptr_t)fb_phys;
    fb_ready = 1;
    fb_fill(FB_BLACK);

    vga_writestring("FB: ");
    print_dec(FB_WIDTH); vga_writestring("x");
    print_dec(FB_HEIGHT); vga_writestring("x32 ready\n");
}

int fb_is_available(void) { return fb_ready; }

/* =========================================================================
 * Pixel / fill
 * ======================================================================= */
void fb_set_pixel(int x, int y, uint32_t c) {
    if (!fb_ready || x<0 || x>=FB_WIDTH || y<0 || y>=FB_HEIGHT) return;
    fb_mem[y*FB_WIDTH+x] = c;
}
uint32_t fb_get_pixel(int x, int y) {
    if (!fb_ready || x<0 || x>=FB_WIDTH || y<0 || y>=FB_HEIGHT) return 0;
    return fb_mem[y*FB_WIDTH+x];
}
void fb_fill(uint32_t c) {
    if (!fb_ready) return;
    int n=FB_WIDTH*FB_HEIGHT;
    for (int i=0;i<n;i++) fb_mem[i]=c;
}
void fb_fill_rect(int x, int y, int w, int h, uint32_t c) {
    if (!fb_ready) return;
    if (x<0){w+=x;x=0;} if (y<0){h+=y;y=0;}
    if (x+w>FB_WIDTH)  w=FB_WIDTH-x;
    if (y+h>FB_HEIGHT) h=FB_HEIGHT-y;
    if (w<=0||h<=0) return;
    for (int dy=0;dy<h;dy++){
        uint32_t *row=fb_mem+(y+dy)*FB_WIDTH+x;
        for (int dx=0;dx<w;dx++) row[dx]=c;
    }
}
void fb_gradient_v(int x,int y,int w,int h,uint32_t top,uint32_t bot){
    if (!fb_ready||h<=0) return;
    int tr=(top>>16)&0xFF,tg=(top>>8)&0xFF,tb2=top&0xFF;
    int br=(bot>>16)&0xFF,bg2=(bot>>8)&0xFF,bb=bot&0xFF;
    for (int dy=0;dy<h;dy++){
        fb_fill_rect(x,y+dy,w,1,FB_COLOR(
            (uint32_t)(tr+(br-tr)*dy/h),
            (uint32_t)(tg+(bg2-tg)*dy/h),
            (uint32_t)(tb2+(bb-tb2)*dy/h)));
    }
}
void fb_gradient_h(int x,int y,int w,int h,uint32_t left,uint32_t right){
    if (!fb_ready||w<=0) return;
    int lr=(left>>16)&0xFF,lg=(left>>8)&0xFF,lb=left&0xFF;
    int rr=(right>>16)&0xFF,rg=(right>>8)&0xFF,rb=right&0xFF;
    for (int dx=0;dx<w;dx++){
        fb_fill_rect(x+dx,y,1,h,FB_COLOR(
            (uint32_t)(lr+(rr-lr)*dx/w),
            (uint32_t)(lg+(rg-lg)*dx/w),
            (uint32_t)(lb+(rb-lb)*dx/w)));
    }
}

/* =========================================================================
 * Shapes
 * ======================================================================= */
void fb_draw_hline(int x,int y,int len,uint32_t c){fb_fill_rect(x,y,len,1,c);}
void fb_draw_vline(int x,int y,int len,uint32_t c){fb_fill_rect(x,y,1,len,c);}
void fb_draw_rect(int x,int y,int w,int h,uint32_t c){
    fb_draw_hline(x,y,w,c); fb_draw_hline(x,y+h-1,w,c);
    fb_draw_vline(x,y,h,c); fb_draw_vline(x+w-1,y,h,c);
}
void fb_draw_rect_thick(int x,int y,int w,int h,int t,uint32_t c){
    for(int i=0;i<t;i++) fb_draw_rect(x+i,y+i,w-2*i,h-2*i,c);
}
void fb_draw_line(int x0,int y0,int x1,int y1,uint32_t c){
    if(!fb_ready) return;
    int dx=(x1>x0)?(x1-x0):(x0-x1);
    int dy=-((y1>y0)?(y1-y0):(y0-y1));
    int sx=(x0<x1)?1:-1,sy=(y0<y1)?1:-1,err=dx+dy;
    for(;;){
        fb_set_pixel(x0,y0,c);
        if(x0==x1&&y0==y1) break;
        int e2=2*err;
        if(e2>=dy){err+=dy;x0+=sx;}
        if(e2<=dx){err+=dx;y0+=sy;}
    }
}
void fb_fill_circle(int cx,int cy,int r,uint32_t c){
    if(!fb_ready) return;
    for(int y=-r;y<=r;y++){
        int xw=0,yy=y*y,rr=r*r;
        while((xw+1)*(xw+1)+yy<=rr) xw++;
        fb_fill_rect(cx-xw,cy+y,xw*2+1,1,c);
    }
}
void fb_fill_rounded_rect(int x,int y,int w,int h,int r,uint32_t c){
    if(!fb_ready) return;
    fb_fill_rect(x+r,y,w-2*r,h,c);
    fb_fill_rect(x,y+r,r,h-2*r,c);
    fb_fill_rect(x+w-r,y+r,r,h-2*r,c);
    for(int cy2=0;cy2<r;cy2++){
        int xw=0,yy=(r-cy2-1)*(r-cy2-1),rr=r*r;
        while((xw+1)*(xw+1)+yy<=rr) xw++;
        fb_fill_rect(x+r-xw-1, y+cy2,      xw+1,1,c);
        fb_fill_rect(x+w-r,    y+cy2,      xw+1,1,c);
        fb_fill_rect(x+r-xw-1, y+h-1-cy2, xw+1,1,c);
        fb_fill_rect(x+w-r,    y+h-1-cy2, xw+1,1,c);
    }
}
void fb_draw_rounded_rect(int x,int y,int w,int h,int r,uint32_t c){
    fb_draw_hline(x+r,y,w-2*r,c); fb_draw_hline(x+r,y+h-1,w-2*r,c);
    fb_draw_vline(x,y+r,h-2*r,c); fb_draw_vline(x+w-1,y+r,h-2*r,c);
    for(int a=0;a<=r;a++){
        int yy=(r-a)*(r-a),rr=r*r,xw=0;
        while((xw+1)*(xw+1)+yy<=rr) xw++;
        fb_set_pixel(x+r-xw,     y+a,     c); fb_set_pixel(x+w-1-r+xw,y+a,     c);
        fb_set_pixel(x+r-xw,     y+h-1-a, c); fb_set_pixel(x+w-1-r+xw,y+h-1-a, c);
    }
}

/* =========================================================================
 * Text
 * ======================================================================= */
void fb_draw_char(char ch,int x,int y,uint32_t fg,uint32_t bg,int scale){
    if(!fb_ready) return;
    if(ch<0x20||ch>0x7F) ch='?';
    const uint8_t *g=font8x8[ch-0x20];
    for(int row=0;row<8;row++){
        for(int col=0;col<8;col++){
            uint32_t c=((g[row]>>(7-col))&1)?fg:bg;
            if(c==FB_TRANSPARENT) continue;
            if(scale==1) fb_set_pixel(x+col,y+row,c);
            else         fb_fill_rect(x+col*scale,y+row*scale,scale,scale,c);
        }
    }
}
void fb_draw_string(const char *s,int x,int y,
                    uint32_t fg,uint32_t bg,int scale){
    if(!fb_ready||!s) return;
    int cx=x,cy=y,cw=8*scale,ch=8*scale;
    while(*s){
        if     (*s=='\n'){cx=x;cy+=ch;}
        else if(*s=='\r'){cx=x;}
        else if(*s=='\t'){int t=4*cw;cx=((cx/t)+1)*t;}
        else             {fb_draw_char(*s,cx,cy,fg,bg,scale);cx+=cw;}
        s++;
    }
}
int fb_string_width(const char *s,int scale){
    int n=0; if(s) while(*s++){n++;} return n*8*scale;
}

/* =========================================================================
 * Console
 * ======================================================================= */
void fb_con_init(int x,int y,int w,int h,uint32_t fg,uint32_t bg,int scale){
    con_x0=x;con_y0=y;con_w=w;con_h=h;
    con_fg=fg;con_bg=bg;con_scale=scale;
    con_cx=0;con_cy=0;
    con_cols=w/(8*scale);con_rows=h/(8*scale);
}
void fb_con_clear(void){
    fb_fill_rect(con_x0,con_y0,con_w,con_h,con_bg);
    con_cx=0;con_cy=0;
}
static void con_scroll(void){
    if(!fb_ready) return;
    int ch=8*con_scale;
    for(int row=0;row<con_rows-1;row++)
        for(int py=0;py<ch;py++){
            uint32_t *src=fb_mem+(con_y0+(row+1)*ch+py)*FB_WIDTH+con_x0;
            uint32_t *dst=fb_mem+(con_y0+ row   *ch+py)*FB_WIDTH+con_x0;
            for(int px=0;px<con_w;px++) dst[px]=src[px];
        }
    fb_fill_rect(con_x0,con_y0+(con_rows-1)*ch,con_w,ch,con_bg);
}
void fb_con_putchar(char c){
    if(!fb_ready) return;
    int cw=8*con_scale,ch=8*con_scale;
    if(c=='\n'){con_cx=0;con_cy++;}
    else if(c=='\r'){con_cx=0;}
    else if(c=='\b'){
        if(con_cx>0){con_cx--;fb_fill_rect(con_x0+con_cx*cw,con_y0+con_cy*ch,cw,ch,con_bg);}
    }else{
        if(con_cx>=con_cols){con_cx=0;con_cy++;}
        if(con_cy>=con_rows){con_scroll();con_cy=con_rows-1;}
        fb_draw_char(c,con_x0+con_cx*cw,con_y0+con_cy*ch,con_fg,con_bg,con_scale);
        con_cx++;
    }
    if(con_cy>=con_rows){con_scroll();con_cy=con_rows-1;}
}
void fb_con_print(const char *s){if(s)while(*s)fb_con_putchar(*s++);}

/* =========================================================================
 * UI helpers
 * ======================================================================= */
void fb_draw_panel(int x,int y,int w,int h,const char *title,uint32_t tbg){
    fb_fill_rect(x+4,y+4,w,h,FB_COLOR(8,8,16));
    fb_fill_rounded_rect(x,y,w,h,6,FB_PANEL);
    fb_draw_rounded_rect(x,y,w,h,6,FB_BORDER);
    fb_fill_rect(x+1,y+1,w-2,28,tbg);
    fb_draw_hline(x+1,y+29,w-2,FB_BORDER);
    if(title) fb_draw_string(title,x+10,y+7,FB_WHITE,FB_TRANSPARENT,FB_SCALE_SMALL);
}
void fb_draw_button(int x,int y,int w,int h,
                    const char *label,uint32_t bg,uint32_t fg){
    fb_fill_rounded_rect(x,y,w,h,4,bg);
    fb_draw_rounded_rect(x,y,w,h,4,FB_BORDER);
    if(label){
        int lw=fb_string_width(label,FB_SCALE_SMALL);
        fb_draw_string(label,x+(w-lw)/2,y+(h-8)/2,fg,FB_TRANSPARENT,FB_SCALE_SMALL);
    }
}
void fb_draw_separator(int x,int y,int w,uint32_t c){
    fb_draw_hline(x,y,  w,FB_COLOR(((c>>16)&0xFF)/2,((c>>8)&0xFF)/2,(c&0xFF)/2));
    fb_draw_hline(x,y+1,w,c);
}
void fb_update_taskbar(void){
    if(!fb_ready) return;
    uint64_t up=timer_get_uptime_seconds();
    uint64_t h=up/3600,m=(up%3600)/60,s=up%60;
    char buf[20];int p=0;
    const char *pre="UP  ";for(int i=0;pre[i];i++)buf[p++]=pre[i];
    buf[p++]='0'+(char)(h/10);buf[p++]='0'+(char)(h%10);buf[p++]=':';
    buf[p++]='0'+(char)(m/10);buf[p++]='0'+(char)(m%10);buf[p++]=':';
    buf[p++]='0'+(char)(s/10);buf[p++]='0'+(char)(s%10);buf[p]='\0';
    int bw=p*8+20,bx=FB_WIDTH-bw-8;
    fb_fill_rect(bx,6,bw+4,16,FB_TASKBAR);
    fb_draw_string(buf,bx,6,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);
}

/* =========================================================================
 * Desktop
 * ======================================================================= */
void fb_draw_desktop(void){
    if(!fb_ready) return;

    fb_gradient_v(0,0,FB_WIDTH,FB_HEIGHT,FB_COLOR(12,14,28),FB_COLOR(22,24,48));
    for(int i=0;i<3;i++)
        fb_draw_hline(0,80+i*60,FB_WIDTH,FB_COLOR(30+i*8,40+i*8,80+i*12));

    /* Taskbar */
    fb_fill_rect(0,0,FB_WIDTH,40,FB_TASKBAR);
    fb_gradient_h(0,0,FB_WIDTH/3,40,FB_COLOR(50,60,130),FB_TASKBAR);
    fb_draw_string("NumOS",12,11,FB_WHITE,FB_TRANSPARENT,FB_SCALE_NORMAL);
    fb_draw_string("v0.8.0-beta",94,15,FB_ACCENT,FB_TRANSPARENT,FB_SCALE_SMALL);
    fb_fill_circle(220,20,5,FB_SUCCESS);
    fb_draw_string("RUNNING",232,13,FB_SUCCESS,FB_TRANSPARENT,FB_SCALE_SMALL);
    fb_draw_hline(0,40,FB_WIDTH,FB_ACCENT);
    fb_update_taskbar();

    /* Left panel */
    int lx=20,ly=58,lw=290,lh=400;
    fb_draw_panel(lx,ly,lw,lh," System Information",FB_TITLE_BG);
    int iy=ly+40,ix=lx+14;
    fb_draw_string("HARDWARE",ix,iy,FB_ACCENT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=14;
    fb_draw_hline(ix,iy,lw-28,FB_BORDER);iy+=6;
    fb_draw_string("Architecture",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("x86-64 (Long Mode)",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
    fb_draw_string("Display",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("BGA 1024x768x32",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
    fb_draw_string("Memory",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("4096 MB (QEMU)",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=20;
    fb_draw_string("KERNEL",ix,iy,FB_ACCENT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=14;
    fb_draw_hline(ix,iy,lw-28,FB_BORDER);iy+=6;
    fb_draw_string("Version",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("NumOS 0.8.0-beta",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
    fb_draw_string("Paging",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("4-Level (PML4)",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
    fb_draw_string("Heap",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("128 MB (best-fit)",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
    fb_draw_string("Scheduler",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("Round-Robin 100Hz",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
    fb_draw_string("Filesystem",ix,iy,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=12;
    fb_draw_string("FAT32 (ATA PIO)",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);
    /* Badges */
    iy=ly+lh-40;
    const char *badges[]={"GDT","IDT","PMM","VMM"};
    for(int b=0;b<4;b++){
        int bx2=ix+b*66;
        fb_fill_rounded_rect(bx2,iy,58,20,4,FB_COLOR(20,80,30));
        fb_draw_rounded_rect(bx2,iy,58,20,4,FB_SUCCESS);
        int lw2=fb_string_width(badges[b],FB_SCALE_SMALL);
        fb_draw_string(badges[b],bx2+(58-lw2)/2,iy+6,FB_SUCCESS,FB_TRANSPARENT,FB_SCALE_SMALL);
    }

    /* Terminal panel */
    int tx=330,ty=58,tw=670,th=400;
    fb_draw_panel(tx,ty,tw,th," Interactive Shell",FB_COLOR(20,40,80));
    fb_con_init(tx+10,ty+36,tw-20,th-46,FB_TEXT,FB_PANEL,FB_SCALE_SMALL);
    fb_con_print("NumOS kernel shell ready.\n");
    fb_con_print("Type a letter to execute:\n\n");
    fb_con_print("  [S] Scroll (VGA)    [L] List root dir\n");
    fb_con_print("  [I] Syscall stats   [P] Process list\n");
    fb_con_print("  [D] Device list     [R] Re-run ELF\n");
    fb_con_print("  [H] Halt\n\n");
    fb_con_print("> ");

    /* Status bar */
    int sb=FB_HEIGHT-28;
    fb_fill_rect(0,sb,FB_WIDTH,28,FB_COLOR(14,16,32));
    fb_draw_hline(0,sb,FB_WIDTH,FB_BORDER);
    fb_draw_string("NumOS  |  BGA Framebuffer  |  1024x768x32  |  Kernel Mode",
                   12,sb+9,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);
    const char *copy="2025 NumOS Project";
    fb_draw_string(copy,FB_WIDTH-fb_string_width(copy,FB_SCALE_SMALL)-12,
                   sb+9,FB_DIM,FB_TRANSPARENT,FB_SCALE_SMALL);
}