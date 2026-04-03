#include "cpu/heap.h"

static unsigned char heap_area[HEAP_SIZE] __attribute__((aligned(16)));
static size_t heap_offset = 0;
static struct heap_stats heap_stats_data;

static size_t align_up(size_t value, size_t alignment) {
    return (value + alignment - 1) & ~(alignment - 1);
}

void heap_init(void) {
    heap_offset = 0;
    heap_stats_data.total_size = HEAP_SIZE;
    heap_stats_data.used_size = 0;
    heap_stats_data.free_size = HEAP_SIZE;
    heap_stats_data.total_blocks = 0;
    heap_stats_data.used_blocks = 0;
    heap_stats_data.free_blocks = 0;
    heap_stats_data.allocations = 0;
    heap_stats_data.deallocations = 0;
    heap_stats_data.allocation_failures = 0;
    heap_stats_data.corruptions = 0;
    heap_stats_data.largest_free = HEAP_SIZE;
    heap_stats_data.smallest_free = HEAP_SIZE;
}

void *kmalloc(size_t size) {
    if (size == 0) return 0;

    size_t aligned = align_up(size, HEAP_ALIGNMENT);
    if (heap_offset + aligned > sizeof(heap_area)) {
        heap_stats_data.allocation_failures++;
        return 0;
    }

    void *ptr = &heap_area[heap_offset];
    heap_offset += aligned;

    heap_stats_data.allocations++;
    heap_stats_data.total_blocks++;
    heap_stats_data.used_blocks++;
    heap_stats_data.used_size += aligned;
    heap_stats_data.free_size = HEAP_SIZE - heap_stats_data.used_size;
    heap_stats_data.largest_free = heap_stats_data.free_size;
    heap_stats_data.smallest_free = heap_stats_data.free_size;
    return ptr;
}

void *kzalloc(size_t size) {
    unsigned char *ptr = (unsigned char *)kmalloc(size);
    if (!ptr) return 0;
    for (size_t i = 0; i < size; i++) ptr[i] = 0;
    return ptr;
}

void kfree(void *ptr) {
    (void)ptr;
    heap_stats_data.deallocations++;
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment < HEAP_ALIGNMENT) alignment = HEAP_ALIGNMENT;

    size_t start = align_up(heap_offset, alignment);
    size_t aligned_size = align_up(size, HEAP_ALIGNMENT);
    if (start + aligned_size > sizeof(heap_area)) {
        heap_stats_data.allocation_failures++;
        return 0;
    }

    void *ptr = &heap_area[start];
    heap_offset = start + aligned_size;
    heap_stats_data.allocations++;
    heap_stats_data.total_blocks++;
    heap_stats_data.used_blocks++;
    heap_stats_data.used_size += aligned_size;
    heap_stats_data.free_size = HEAP_SIZE - heap_stats_data.used_size;
    heap_stats_data.largest_free = heap_stats_data.free_size;
    heap_stats_data.smallest_free = heap_stats_data.free_size;
    return ptr;
}

void heap_print_stats(void) {
}

int heap_validate(void) {
    return 1;
}

void heap_get_stats(struct heap_stats *out) {
    if (!out) return;
    *out = heap_stats_data;
}
