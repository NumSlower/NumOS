#ifndef NUMOS_CONFIG_H
#define NUMOS_CONFIG_H

/*
 * NumOS build configuration.
 *
 * Edit these values, then rebuild.
 *
 * 1 = enabled, 0 = disabled
 */

/* Version string reported by sysinfo and the boot banner. */
#ifndef NUMOS_VERSION
#define NUMOS_VERSION "0.8.0-beta"
#endif

/* Video: enable Bochs BGA framebuffer console support. */
#ifndef NUMOS_ENABLE_FRAMEBUFFER
#define NUMOS_ENABLE_FRAMEBUFFER 1
#endif

/* Graphics backend order: 1 = VGA, 2 = VESA, 3 = BGA. */
#ifndef NUMOS_GRAPHICS_DEFAULT_BACKEND
#define NUMOS_GRAPHICS_DEFAULT_BACKEND 1
#endif

/* Framebuffer mode (only used when framebuffer is enabled). */
#ifndef NUMOS_FB_WIDTH
#define NUMOS_FB_WIDTH  1920
#endif

#ifndef NUMOS_FB_HEIGHT
#define NUMOS_FB_HEIGHT 1200
#endif

/* Framebuffer mode overrides for virtual machines. */
#ifndef NUMOS_FB_WIDTH_QEMU
#define NUMOS_FB_WIDTH_QEMU  1920
#endif

#ifndef NUMOS_FB_HEIGHT_QEMU
#define NUMOS_FB_HEIGHT_QEMU 1200
#endif

#ifndef NUMOS_FB_SCALE_QEMU
#define NUMOS_FB_SCALE_QEMU 1
#endif

#ifndef NUMOS_FB_WIDTH_VBOX
#define NUMOS_FB_WIDTH_VBOX  1920
#endif

#ifndef NUMOS_FB_HEIGHT_VBOX
#define NUMOS_FB_HEIGHT_VBOX 1200
#endif

#ifndef NUMOS_FB_SCALE_VBOX
#define NUMOS_FB_SCALE_VBOX 1
#endif

/* Framebuffer text scale (1 = smallest, 3 = largest). */
#ifndef NUMOS_FB_SCALE
#define NUMOS_FB_SCALE 1
#endif

/* Framebuffer: set to 1 to force VGA text mode on VirtualBox. */
#ifndef NUMOS_FB_DISABLE_ON_VBOX
#define NUMOS_FB_DISABLE_ON_VBOX 0
#endif

/* Framebuffer: enable on QEMU/KVM by default. */
#ifndef NUMOS_FB_ENABLE_ON_QEMU
#define NUMOS_FB_ENABLE_ON_QEMU 1
#endif

/* Framebuffer: default kernel stays in VGA. VESA uses the VESA boot image. */
#ifndef NUMOS_FB_ENABLE_VBE
#define NUMOS_FB_ENABLE_VBE 0
#endif

/* Framebuffer: allow BGA fallback if VBE is unavailable. */
#ifndef NUMOS_FB_ALLOW_BGA_FALLBACK
#define NUMOS_FB_ALLOW_BGA_FALLBACK 1
#endif

/* VGA: reduce cursor and scrollback overhead on VirtualBox. */
#ifndef NUMOS_VGA_FAST_MODE_ON_VBOX
#define NUMOS_VGA_FAST_MODE_ON_VBOX 1
#endif

/* Framebuffer: show a test pattern during boot. */
#ifndef NUMOS_FB_TEST_PATTERN
#define NUMOS_FB_TEST_PATTERN 0
#endif

/* Default init program path when no boot cmdline override is present. */
#ifndef NUMOS_INIT_PATH
#define NUMOS_INIT_PATH "/bin/SHELL.ELF"
#endif

#endif /* NUMOS_CONFIG_H */
