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
#include "drivers/font.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* Text rendering uses the PSF bitmap font loader in font.c. */

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
    fb_fill(FB_TERM_BG);

    /* Default full-screen framebuffer console */
    int fw = font_char_width();
    int fh = font_char_height();
    if (fw < 1) fw = 8;
    if (fh < 1) fh = 16;

    int scale = FB_SCALE_NORMAL;
    for (int s = FB_SCALE_3; s >= FB_SCALE_1; s--) {
        int cols = FB_WIDTH  / (fw * s);
        int rows = FB_HEIGHT / (fh * s);
        if (cols >= 80 && rows >= 25) { scale = s; break; }
    }

    int cw   = fw * scale;
    int ch   = fh * scale;
    int cols = FB_WIDTH  / cw;
    int rows = FB_HEIGHT / ch;
    int wpx  = cols * cw;
    int hpx  = rows * ch;
    int x0   = (FB_WIDTH  - wpx) / 2;
    int y0   = (FB_HEIGHT - hpx) / 2;

    fb_con_init(x0, y0, wpx, hpx, FB_TERM_FG, FB_TERM_BG, scale);
    fb_con_clear();
    vga_set_output_hook(fb_con_putchar);

    vga_writestring("FB: ");
    print_dec(FB_WIDTH); vga_writestring("x");
    print_dec(FB_HEIGHT); vga_writestring("x32 ready\n");
}

int fb_is_available(void) { return fb_ready; }
int fb_get_width(void)    { return FB_WIDTH;  }
int fb_get_height(void)   { return FB_HEIGHT; }

void fb_con_set_color(uint32_t fg, uint32_t bg) {
    con_fg = fg;
    con_bg = bg;
}

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
    font_draw_char(ch, x, y, fg, bg, scale);
}
void fb_draw_string(const char *s,int x,int y,
                    uint32_t fg,uint32_t bg,int scale){
    font_draw_string(s, x, y, fg, bg, scale);
}
int fb_string_width(const char *s,int scale){
    return font_string_width(s, scale);
}

/* =========================================================================
 * Console
 * ======================================================================= */
void fb_con_init(int x,int y,int w,int h,uint32_t fg,uint32_t bg,int scale){
    if (scale < 1) scale = 1;
    con_x0=x;con_y0=y;con_w=w;con_h=h;
    con_fg=fg;con_bg=bg;con_scale=scale;
    con_cx=0;con_cy=0;

    int cw = font_char_width() * scale;
    int ch = font_char_height() * scale;
    con_cols = cw ? (w / cw) : 1;
    con_rows = ch ? (h / ch) : 1;
    if (con_cols < 1) con_cols = 1;
    if (con_rows < 1) con_rows = 1;
}
void fb_con_clear(void){
    fb_fill_rect(con_x0,con_y0,con_w,con_h,con_bg);
    con_cx=0;con_cy=0;
}
static void con_scroll(void){
    if(!fb_ready) return;
    int ch=font_char_height()*con_scale;
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
    int cw=font_char_width()*con_scale,ch=font_char_height()*con_scale;
    if(c=='\f'){fb_con_clear();return;}
    if(c=='\n'){con_cx=0;con_cy++;}
    else if(c=='\r'){con_cx=0;}
    else if(c=='\t'){
        int spaces = 4 - (con_cx % 4);
        for (int i = 0; i < spaces; i++) fb_con_putchar(' ');
        return;
    }
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
    fb_draw_string("BGA 1920x1200x32",ix+10,iy,FB_TEXT,FB_TRANSPARENT,FB_SCALE_SMALL);iy+=16;
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
