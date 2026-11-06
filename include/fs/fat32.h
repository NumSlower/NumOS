/* NumOS — fat32.h — FAT32 filesystem driver (VFS backend) */
#ifndef NUMOS_FS_FAT32_H
#define NUMOS_FS_FAT32_H

#include "libs/types.h"
#include "drivers/block.h"
#include "fs/vfs.h"

#define FAT32_EOC_MIN 0x0FFFFFF8u
#define FAT32_BAD_CLUSTER 0x0FFFFFF7u
#define FAT32_FREE_CLUSTER 0x00000000u

struct vfs_filesystem* fat32_mount(struct block_device* dev);

#endif
