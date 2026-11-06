/* NumOS — block.h — Block-device abstraction
 *
 * Registration, discovery, bounds-checked sector I/O with error reporting
 * and per-device counters. Filesystems sit on this layer and must not
 * depend on the controller driver (ATA) directly.
 */
#ifndef NUMOS_DRIVERS_BLOCK_H
#define NUMOS_DRIVERS_BLOCK_H

#include "libs/types.h"

#define BLOCK_NAME_MAX 16

#define BLOCK_OK       0
#define BLOCK_EINVAL   1
#define BLOCK_ERANGE   2
#define BLOCK_EIO      3
#define BLOCK_ENODEV   4
#define BLOCK_ETIMEDOUT 5

struct block_device;

typedef int (*block_read_fn)(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
typedef int (*block_write_fn)(struct block_device* dev, uint64_t lba, uint32_t count, const void* buffer);
typedef int (*block_flush_fn)(struct block_device* dev);

struct block_device {
    char name[BLOCK_NAME_MAX];
    uint32_t sector_size;
    uint64_t sector_count;
    uint64_t capacity_bytes;
    block_read_fn read;
    block_write_fn write;
    block_flush_fn flush;
    uint64_t read_count;
    uint64_t write_count;
    uint64_t error_count;
    void* private_data;
};

bool block_init(void);
struct block_device* block_register(const char* name, uint32_t sector_size, uint64_t sector_count,
                                    block_read_fn read, block_write_fn write, block_flush_fn flush,
                                    void* private_data);
uint32_t block_get_device_count(void);
struct block_device* block_get_device(uint32_t index);
struct block_device* block_find(const char* name);
int block_read_blocks(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer);
int block_write_blocks(struct block_device* dev, uint64_t lba, uint32_t count, const void* buffer);
int block_flush(struct block_device* dev);

#endif
