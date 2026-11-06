/* NumOS — block.c — Block-device abstraction implementation
 *
 * Bounds checking, counter and error accounting happen here so that every
 * consumer (VFS, FAT32, monitoring) sees consistent validation and stats.
 */
#include "drivers/block.h"
#include "libs/types.h"
#include "libs/string.h"
#include "libs/kprintf.h"

static struct block_device devices[BLOCK_NAME_MAX];
static uint32_t device_count = 0;

bool block_init(void) {
    device_count = 0;
    return true;
}

struct block_device* block_register(const char* name, uint32_t sector_size, uint64_t sector_count,
                                    block_read_fn read, block_write_fn write, block_flush_fn flush,
                                    void* private_data) {
    if (!name || sector_size == 0 || (sector_size & (sector_size - 1)) != 0) return NULL;
    if (!read) return NULL;
    if (device_count >= BLOCK_NAME_MAX) return NULL;

    for (uint32_t i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) return NULL;
    }

    struct block_device* dev = &devices[device_count];
    memset(dev, 0, sizeof(*dev));
    strncpy(dev->name, name, BLOCK_NAME_MAX - 1);
    dev->name[BLOCK_NAME_MAX - 1] = '\0';
    dev->sector_size = sector_size;
    dev->sector_count = sector_count;
    dev->capacity_bytes = sector_size * sector_count;
    dev->read = read;
    dev->write = write;
    dev->flush = flush;
    dev->private_data = private_data;

    device_count++;
    return dev;
}

uint32_t block_get_device_count(void) {
    return device_count;
}

struct block_device* block_get_device(uint32_t index) {
    if (index >= device_count) return NULL;
    return &devices[index];
}

struct block_device* block_find(const char* name) {
    if (!name) return NULL;
    for (uint32_t i = 0; i < device_count; i++) {
        if (strcmp(devices[i].name, name) == 0) return &devices[i];
    }
    return NULL;
}

int block_read_blocks(struct block_device* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev) return BLOCK_ENODEV;
    if (count == 0) return BLOCK_OK;
    if (!buffer) {
        dev->error_count++;
        return BLOCK_EINVAL;
    }
    if (lba >= dev->sector_count || count > dev->sector_count - lba) {
        dev->error_count++;
        kprintf("block: %s read out of range (lba %llu, count %u, capacity %llu)\n",
                dev->name, lba, count, dev->sector_count);
        return BLOCK_ERANGE;
    }

    int rc = dev->read(dev, lba, count, buffer);
    if (rc == BLOCK_OK) {
        dev->read_count += count;
    } else {
        dev->error_count++;
    }
    return rc;
}

int block_write_blocks(struct block_device* dev, uint64_t lba, uint32_t count, const void* buffer) {
    if (!dev) return BLOCK_ENODEV;
    if (count == 0) return BLOCK_OK;
    if (!buffer) {
        dev->error_count++;
        return BLOCK_EINVAL;
    }
    if (!dev->write) {
        dev->error_count++;
        return BLOCK_EIO;
    }
    if (lba >= dev->sector_count || count > dev->sector_count - lba) {
        dev->error_count++;
        kprintf("block: %s write out of range (lba %llu, count %u, capacity %llu)\n",
                dev->name, lba, count, dev->sector_count);
        return BLOCK_ERANGE;
    }

    int rc = dev->write(dev, lba, count, buffer);
    if (rc == BLOCK_OK) {
        dev->write_count += count;
    } else {
        dev->error_count++;
    }
    return rc;
}

int block_flush(struct block_device* dev) {
    if (!dev) return BLOCK_ENODEV;
    if (!dev->flush) return BLOCK_OK;
    int rc = dev->flush(dev);
    if (rc != BLOCK_OK) dev->error_count++;
    return rc;
}
