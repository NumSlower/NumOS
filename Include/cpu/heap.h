#ifndef HEAP_H
#define HEAP_H

#include "lib/base.h"

/* Heap Configuration Constants */
#define HEAP_START          0xFFFFFFFF90000000UL  /* Kernel heap start address */
#define HEAP_SIZE           (64 * 1024 * 1024)    /* 64MB heap size */
#define HEAP_MIN_SIZE       16                     /* Minimum allocation size */
#define HEAP_ALIGNMENT      16                     /* Memory alignment (16-byte for 64-bit) */

/* Block Magic Numbers for Validation */
#define HEAP_MAGIC_ALLOC    0xDEADBEEFDEADBEEFUL  /* Allocated block magic (64-bit) */
#define HEAP_MAGIC_FREE     0xFEEDFACEFEEDFACEUL  /* Free block magic (64-bit) */

/* Block Status Flags */
#define HEAP_FLAG_FREE      0x01                  /* Block is free */
#define HEAP_FLAG_USED      0x02                  /* Block is allocated */
#define HEAP_FLAG_FIRST     0x04                  /* First block in heap */
#define HEAP_FLAG_LAST      0x08                  /* Last block in heap */

/* Heap Block Header Structure (aligned to 16 bytes for 64-bit) */
struct heap_block {
    uint64_t magic;                /* Magic number for corruption detection */
    uint64_t size;                 /* Block size including header */
    uint32_t flags;                /* Block status flags */
    uint32_t checksum;             /* Simple integrity checksum */
    struct heap_block *prev;       /* Previous block in linked list */
    struct heap_block *next;       /* Next block in linked list */
} __attribute__((packed, aligned(16)));

/* Heap Statistics Structure */
struct heap_stats {
    uint64_t total_size;           /* Total heap size */
    uint64_t used_size;            /* Currently allocated memory */
    uint64_t free_size;            /* Currently free memory */
    uint32_t total_blocks;         /* Total number of blocks */
    uint32_t used_blocks;          /* Number of allocated blocks */
    uint32_t free_blocks;          /* Number of free blocks */
    uint32_t allocations;          /* Total allocation count */
    uint32_t deallocations;        /* Total deallocation count */
    uint32_t allocation_failures;  /* Failed allocation attempts */
    uint32_t corruptions;          /* Detected corruption count */
    uint64_t largest_free;         /* Largest free block size */
    uint64_t smallest_free;        /* Smallest free block size */
};

/* Core Heap Functions */
void heap_init(void);
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void* krealloc(void* ptr, size_t new_size);
void kfree(void* ptr);

/* Aligned Memory Allocation */
void* kmalloc_aligned(size_t size, size_t alignment);
void* kzalloc_aligned(size_t size, size_t alignment);

/* Array and String Allocation Helpers */
void* kcalloc(size_t count, size_t size);
char* kstrdup(const char* str);

/* Page-based Allocation */
void* kmalloc_pages(size_t pages);
void kfree_pages(void* ptr, size_t pages);

/* Heap Information and Debugging */
struct heap_stats heap_get_stats(void);
void heap_print_stats(void);
void heap_print_blocks(void);
int heap_validate(void);
void heap_defragment(void);
size_t heap_get_block_size(void* ptr);

/* Heap Integrity Functions */
int heap_check_corruption(void);
void heap_enable_guards(int enable);

#endif /* HEAP_H */