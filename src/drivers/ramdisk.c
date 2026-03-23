/*
 * ramdisk.c - In-memory disk image driver
 *
 * When GRUB loads disk.img as a multiboot2 module, its physical address and
 * size are passed to ramdisk_init().  All subsequent FAT32 sector reads are
 * served from this buffer instead of the physical ATA device, so a single
 * self-contained ISO is enough to boot NumOS with its user-space programs.
 */

#include "drivers/ramdisk.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"

/* =========================================================================
 * Module state
 * ======================================================================= */

static uint8_t *g_base = NULL;
static uint64_t g_size = 0;

/* =========================================================================
 * Public API
 * ======================================================================= */

void ramdisk_init(uint64_t base_phys, uint64_t size) {
    if (!base_phys || !size) {
        vga_writestring("RAMDISK: init called with null base or zero size\n");
        return;
    }

    g_base = (uint8_t *)(uintptr_t)base_phys;
    g_size = size;

    vga_writestring("RAMDISK: ");
    print_dec(size / (1024 * 1024));
    vga_writestring(" MB at 0x");
    print_hex(base_phys);
    vga_writestring(" (");
    print_dec(size / 512);
    vga_writestring(" sectors)\n");
}

int ramdisk_available(void) {
    return (g_base != NULL);
}

int ramdisk_read_sector(uint32_t sector, void *buffer) {
    uint64_t offset = (uint64_t)sector * 512;

    if (!g_base) return -1;
    if (offset + 512 > g_size) return -1;

    memcpy(buffer, g_base + offset, 512);
    return 0;
}

int ramdisk_write_sector(uint32_t sector, const void *buffer) {
    uint64_t offset = (uint64_t)sector * 512;

    if (!g_base) return -1;
    if (offset + 512 > g_size) return -1;

    memcpy(g_base + offset, buffer, 512);
    return 0;
}

