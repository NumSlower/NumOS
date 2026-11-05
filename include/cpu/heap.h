/* NumOS — heap.h — Kernel Heap */
#ifndef NUMOS_HEAP_H
#define NUMOS_HEAP_H

#include "libs/types.h"

void heap_init(uint64_t start, size_t size);
void* kmalloc(size_t size);
void kfree(void* ptr);
void* kcalloc(size_t count, size_t size);
void* krealloc(void* ptr, size_t new_size);

#endif
