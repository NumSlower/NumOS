#ifndef HEAP_H
#define HEAP_H

#include <stdint.h>
#include <stddef.h>

/* Heap configuration */
#define HEAP_START          0xFFFFFFFF90000000UL  // Kernel heap start
#define HEAP_SIZE (16 * 1024 * 1024)  // 16 KB           // 16MB initial heap
#define HEAP_MIN_SIZE       16                     // Minimum allocation size
#define HEAP_ALIGNMENT      16                     // Memory alignment
#define HEAP_MAGIC_ALLOC    0xDEADBEEF            // Allocated block magic
#define HEAP_MAGIC_FREE     0xFEEDFACE            // Free block magic

/* Block header flags */
#define HEAP_FLAG_FREE      0x01
#define HEAP_FLAG_USED      0x02
#define HEAP_FLAG_FIRST     0x04
#define HEAP_FLAG_LAST      0x08

/* Heap block header structure */
struct heap_block {
    uint32_t magic;         // Magic number for validation
    uint32_t size;          // Size of the block (including header)
    uint32_t flags;         // Block flags
    uint32_t checksum;      // Simple checksum for corruption detection
    struct heap_block *prev; // Previous block
    struct heap_block *next; // Next block
} __attribute__((packed));

/* Heap statistics */
struct heap_stats {
    uint64_t total_size;
    uint64_t used_size;
    uint64_t free_size;
    uint32_t total_blocks;
    uint32_t used_blocks;
    uint32_t free_blocks;
    uint32_t allocations;
    uint32_t deallocations;
    uint32_t corruptions;
    uint64_t largest_free;
    uint64_t smallest_free;
};

/* Heap initialization and management */
void heap_init(void);
void* kmalloc(size_t size);
void* kzalloc(size_t size);
void* krealloc(void* ptr, size_t new_size);
void kfree(void* ptr);

/* Aligned allocation */
void* kmalloc_aligned(size_t size, size_t alignment);
void* kzalloc_aligned(size_t size, size_t alignment);

/* Array allocation helpers */
void* kcalloc(size_t count, size_t size);
char* kstrdup(const char* str);

/* Heap debugging and statistics */
struct heap_stats heap_get_stats(void);
void heap_print_stats(void);
void heap_print_blocks(void);
int heap_validate(void);
void heap_defragment(void);

/* Heap corruption detection */
int heap_check_corruption(void);
void heap_enable_guards(int enable);

/* Advanced heap operations */
void* kmalloc_pages(size_t pages);
void kfree_pages(void* ptr, size_t pages);
size_t heap_get_block_size(void* ptr);

/* Memory leak detection (debug builds) */
#ifdef HEAP_DEBUG
void heap_dump_allocations(void);
void heap_track_allocation(void* ptr, size_t size, const char* file, int line);
void heap_track_deallocation(void* ptr);
#define kmalloc(size) heap_debug_malloc(size, __FILE__, __LINE__)
#define kfree(ptr) heap_debug_free(ptr, __FILE__, __LINE__)
void* heap_debug_malloc(size_t size, const char* file, int line);
void heap_debug_free(void* ptr, const char* file, int line);
#endif

#endif /* HEAP_H */