#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stddef.h>

/* Memory allocation flags */
#define MEMORY_FLAG_ZERO        0x01
#define MEMORY_FLAG_DMA         0x02
#define MEMORY_FLAG_HIGH        0x04
#define MEMORY_FLAG_ATOMIC      0x08

/* Memory block header */
typedef struct memory_block {
    size_t size;
    int free;
    struct memory_block *next;
    struct memory_block *prev;
} memory_block_t;

/* Memory pool structure */
typedef struct memory_pool {
    uint64_t start;
    uint64_t end;
    size_t total_size;
    size_t used_size;
    size_t free_size;
    memory_block_t *free_list;
    struct memory_pool *next;
} memory_pool_t;

/* Memory statistics */
typedef struct memory_stats {
    uint64_t total_physical;
    uint64_t available_physical;
    uint64_t used_physical;
    uint64_t total_virtual;
    uint64_t used_virtual;
    uint64_t heap_size;
    uint64_t heap_used;
    uint64_t heap_free;
    uint32_t allocation_count;
    uint32_t deallocation_count;
} memory_stats_t;

/* Allocation sizes */
#define MIN_BLOCK_SIZE      16
#define MAX_BLOCK_SIZE      (1024 * 1024 * 1024)  /* 1GB */
#define DEFAULT_HEAP_SIZE   (16 * 1024 * 1024)    /* 16MB */

/* Memory pool sizes */
#define SMALL_POOL_SIZE     (4 * 1024)     /* 4KB blocks */
#define MEDIUM_POOL_SIZE    (64 * 1024)    /* 64KB blocks */
#define LARGE_POOL_SIZE     (1024 * 1024)  /* 1MB blocks */

/* Function prototypes */

/* Core allocation functions */
void memory_init(void);
void *kmalloc(size_t size);
void *kmalloc_aligned(size_t size, size_t alignment);
void *kmalloc_flags(size_t size, uint32_t flags);
void *kcalloc(size_t count, size_t size);
void *krealloc(void *ptr, size_t new_size);
void kfree(void *ptr);

/* Physical memory management */
uint64_t pmm_allocate_frame(void);
void pmm_free_frame(uint64_t frame);
uint64_t pmm_allocate_frames(size_t count);
void pmm_free_frames(uint64_t start_frame, size_t count);
void pmm_mark_used(uint64_t frame);
void pmm_mark_free(uint64_t frame);
int pmm_is_free(uint64_t frame);

/* Virtual memory management */
uint64_t vmm_allocate(size_t size);
void vmm_free(uint64_t virtual_addr, size_t size);
uint64_t vmm_map(uint64_t virtual_addr, uint64_t physical_addr, size_t size, uint64_t flags);
void vmm_unmap(uint64_t virtual_addr, size_t size);

/* Memory pool management */
memory_pool_t *memory_create_pool(size_t size);
void memory_destroy_pool(memory_pool_t *pool);
void *memory_pool_alloc(memory_pool_t *pool, size_t size);
void memory_pool_free(memory_pool_t *pool, void *ptr);

/* Heap management */
void heap_init(uint64_t start, uint64_t end);
void heap_expand(size_t size);
void heap_shrink(size_t size);
void *heap_alloc(size_t size);
void heap_free(void *ptr);
void heap_coalesce(void);

/* Memory information */
void memory_get_stats(memory_stats_t *stats);
uint64_t memory_get_total(void);
uint64_t memory_get_available(void);
uint64_t memory_get_used(void);
void memory_print_stats(void);

/* Memory utility functions */
void memory_set_region(uint64_t start, uint64_t length, uint32_t type);
memory_block_t *memory_find_free_block(size_t size);
memory_block_t *memory_split_block(memory_block_t *block, size_t size);
void memory_merge_blocks(memory_block_t *block);
int memory_validate_pointer(void *ptr);

/* Memory debugging */
#ifdef DEBUG_MEMORY
void memory_debug_print_blocks(void);
void memory_debug_check_heap(void);
int memory_debug_validate_heap(void);
#endif

/* Memory barriers and synchronization */
void memory_barrier(void);
void memory_read_barrier(void);
void memory_write_barrier(void);

/* Cache management */
void memory_flush_cache(void);
void memory_invalidate_cache_range(uint64_t start, size_t size);
void memory_flush_cache_range(uint64_t start, size_t size);

/* DMA memory management */
void *dma_alloc_coherent(size_t size, uint64_t *dma_handle);
void dma_free_coherent(size_t size, void *vaddr, uint64_t dma_handle);
uint64_t dma_map_single(void *ptr, size_t size);
void dma_unmap_single(uint64_t dma_addr, size_t size);

#endif /* MEMORY_H */