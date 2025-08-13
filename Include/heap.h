#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Heap configuration */
#define HEAP_START          0xFFFFFFFF90000000UL  // Start of kernel heap
#define HEAP_INITIAL_SIZE   (1024 * 1024)        // 1MB initial heap
#define HEAP_MAX_SIZE       (64 * 1024 * 1024)   // 64MB maximum heap
#define HEAP_MIN_BLOCK_SIZE 32                    // Minimum allocation size
#define HEAP_ALIGNMENT      16                    // Alignment for allocations

/* Block header structure */
struct heap_block {
    size_t size;                    // Size of this block (including header)
    uint8_t is_free;               // 1 if free, 0 if allocated
    uint8_t magic;                 // Magic number for corruption detection
    struct heap_block *next;       // Next block in the list
    struct heap_block *prev;       // Previous block in the list
} __attribute__((packed));

/* Heap statistics */
struct heap_stats {
    size_t total_size;
    size_t allocated_size;
    size_t free_size;
    size_t num_blocks;
    size_t num_free_blocks;
    size_t largest_free_block;
};

/* Magic numbers for heap corruption detection */
#define HEAP_MAGIC_FREE     0xDE
#define HEAP_MAGIC_ALLOC    0xAD
#define HEAP_MAGIC_GUARD    0xBE

/* Function prototypes */
void heap_init(void);
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void *kcalloc(size_t num, size_t size);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);
void heap_dump(void);
void heap_get_stats(struct heap_stats *stats);
int heap_check_corruption(void);
void heap_defragment(void);

/* Internal functions */
void heap_expand(size_t additional_size);
struct heap_block *heap_find_free_block(size_t size);
void heap_split_block(struct heap_block *block, size_t size);
void heap_merge_blocks(struct heap_block *block);

#endif /* HEAP_H */