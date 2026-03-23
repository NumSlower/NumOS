#ifndef RAMDISK_H
#define RAMDISK_H

#include "lib/base.h"

/* =========================================================================
 * RAM Disk Driver
 *
 * When a disk image is passed to the kernel as a multiboot2 module,
 * the FAT32 driver reads sectors from this in-memory buffer instead of
 * the physical ATA device.  This means a single ISO file is enough —
 * no separate disk.img attachment is required in QEMU or VirtualBox.
 * ======================================================================= */

/*
 * ramdisk_init - register a memory region as the RAM disk.
 *   base_phys : physical (= identity-mapped virtual) base address
 *   size      : size in bytes
 */
void ramdisk_init(uint64_t base_phys, uint64_t size);

/* Returns 1 if a RAM disk was registered, 0 otherwise. */
int ramdisk_available(void);

/*
 * ramdisk_read_sector - copy 512 bytes of sector #sector into buffer.
 * Returns 0 on success, -1 if the sector is out of range.
 */
int ramdisk_read_sector(uint32_t sector, void *buffer);
int ramdisk_write_sector(uint32_t sector, const void *buffer);

#endif /* RAMDISK_H */
