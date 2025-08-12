#include "memory.h"
#include "paging.h"
#include "kernel.h"
#include "vga.h"

/* Memory management globals */
static memory_block_t *heap_start = 0;
static memory_block_t *heap_end = 0;
static uint64_t heap_size = 0;
static uint64_t heap_used = 0;
static memory_stats_t memory_statistics = {0};

/* Memory pools */
static memory_pool_t *small_pool = 0;
static memory_pool_t *medium_pool = 0;
static memory_pool_t *large_pool = 0;

/* Physical memory management */
extern uint64_t placement_address;
static int heap_initialized = 0;

/* Internal helper functions */
static memory_block_t *find_free_block(size_t size);
static memory_block_t *split_block(memory_block_t *block, size_t size);
static void merge_free_blocks(memory_block_t *block);
static void expand_heap(size_t size);
static void *allocate_from_pool(memory_pool_t *pool, size_t size);
static int validate_block(memory_block_t *block);

void memory_init(void) {
    /* Initialize heap boundaries */
    heap_start = (memory_block_t*)HEAP_START;
    heap_size = DEFAULT_HEAP_SIZE;
    heap_end = (memory_block_t*)((uint64_t)heap_start + heap_size);
    heap_used = 0;

    /* Initialize first free block */
    heap_start->size = heap_size - sizeof(memory_block_t);
    heap_start->free = 1;
    heap_start->next = 0;
    heap_start->prev = 0;

    /* Create memory pools */
    small_pool = memory_create_pool(SMALL_POOL_SIZE);
    medium_pool = memory_create_pool(MEDIUM_POOL_SIZE);
    large_pool = memory_create_pool(LARGE_POOL_SIZE);

    /* Initialize statistics */
    memory_statistics.total_physical = paging_get_total_memory();
    memory_statistics.available_physical = paging_get_available_memory();
    memory_statistics.heap_size = heap_size;
    memory_statistics.heap_free = heap_size - sizeof(memory_block_t);
    memory_statistics.allocation_count = 0;
    memory_statistics.deallocation_count = 0;

    heap_initialized = 1;
}

void *kmalloc(size_t size) {
    return kmalloc_flags(size, 0);
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (!heap_initialized) {
        /* Early allocation before heap is set up */
        void *addr = (void*)placement_address;
        placement_address += size;
        placement_address = align_up(placement_address, alignment);
        return addr;
    }

    /* Allocate extra space for alignment */
    size_t total_size = size + alignment + sizeof(memory_block_t);
    void *ptr = kmalloc(total_size);
    if (!ptr) return 0;

    /* Align the pointer */
    uint64_t aligned_addr = align_up((uint64_t)ptr + sizeof(memory_block_t), alignment);
    return (void*)aligned_addr;
}

void *kmalloc_flags(size_t size, uint32_t flags) {
    if (!heap_initialized) {
        /* Early allocation before heap is set up */
        void *addr = (void*)placement_address;
        placement_address += size;
        if (flags & MEMORY_FLAG_ZERO) {
            memset(addr, 0, size);
        }
        return addr;
    }

    if (size == 0) return 0;
    if (size > MAX_BLOCK_SIZE) return 0;

    /* Align size to minimum block size */
    size = align_up(size, MIN_BLOCK_SIZE);

    /* Try to allocate from appropriate pool first */
    void *ptr = 0;
    if (size <= SMALL_POOL_SIZE && small_pool) {
        ptr = allocate_from_pool(small_pool, size);
    } else if (size <= MEDIUM_POOL_SIZE && medium_pool) {
        ptr = allocate_from_pool(medium_pool, size);
    } else if (size <= LARGE_POOL_SIZE && large_pool) {
        ptr = allocate_from_pool(large_pool, size);
    }

    /* If pool allocation failed, use heap */
    if (!ptr) {
        memory_block_t *block = find_free_block(size);
        if (!block) {
            expand_heap(size + sizeof(memory_block_t));
            block = find_free_block(size);
            if (!block) return 0;
        }

        /* Split block if too large */
        if (block->size >= size + sizeof(memory_block_t) + MIN_BLOCK_SIZE) {
            split_block(block, size);
        }

        block->free = 0;
        heap_used += block->size + sizeof(memory_block_t);
        ptr = (void*)((uint64_t)block + sizeof(memory_block_t));
    }

    /* Zero memory if requested */
    if (ptr && (flags & MEMORY_FLAG_ZERO)) {
        memset(ptr, 0, size);
    }

    /* Update statistics */
    memory_statistics.allocation_count++;
    memory_statistics.used_physical += size;
    memory_statistics.heap_used = heap_used;
    memory_statistics.heap_free = heap_size - heap_used;

    return ptr;
}

void *kcalloc(size_t count, size_t size) {
    size_t total_size = count * size;
    if (total_size / count != size) return 0; /* Overflow check */
    
    return kmalloc_flags(total_size, MEMORY_FLAG_ZERO);
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return 0;
    }

    /* Get old block */
    memory_block_t *old_block = (memory_block_t*)((uint64_t)ptr - sizeof(memory_block_t));
    if (!validate_block(old_block)) return 0;

    size_t old_size = old_block->size;
    if (new_size <= old_size) {
        /* Shrink block if significantly smaller */
        if (old_size - new_size >= MIN_BLOCK_SIZE + sizeof(memory_block_t)) {
            split_block(old_block, new_size);
        }
        return ptr;
    }

    /* Need to allocate new block */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return 0;

    /* Copy old data */
    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);

    return new_ptr;
}

void kfree(void *ptr) {
    if (!ptr || !heap_initialized) return;

    /* Get block header */
    memory_block_t *block = (memory_block_t*)((uint64_t)ptr - sizeof(memory_block_t));
    if (!validate_block(block)) return;

    /* Mark as free */
    block->free = 1;
    heap_used -= block->size + sizeof(memory_block_t);

    /* Merge with adjacent free blocks */
    merge_free_blocks(block);

    /* Update statistics */
    memory_statistics.deallocation_count++;
    memory_statistics.used_physical -= block->size;
    memory_statistics.heap_used = heap_used;
    memory_statistics.heap_free = heap_size - heap_used;
}

/* Physical memory management */
uint64_t pmm_allocate_frame(void) {
    return paging_allocate_page();
}

void pmm_free_frame(uint64_t frame) {
    paging_free_page(frame);
}

uint64_t pmm_allocate_frames(size_t count) {
    if (count == 0) return 0;
    if (count == 1) return pmm_allocate_frame();

    /* For multiple frames, we need to find contiguous blocks */
    uint64_t start_frame = 0;
    uint64_t found_count = 0;

    /* This is a simplified implementation */
    for (uint64_t frame = 0; frame < 0x100000; frame += PAGE_SIZE) {
        if (paging_get_physical_addr(frame) == 0) {
            if (found_count == 0) start_frame = frame;
            found_count++;
            if (found_count == count) {
                /* Allocate all frames */
                for (size_t i = 0; i < count; i++) {
                    paging_allocate_page();
                }
                return start_frame;
            }
        } else {
            found_count = 0;
        }
    }

    return 0; /* No contiguous block found */
}

void pmm_free_frames(uint64_t start_frame, size_t count) {
    for (size_t i = 0; i < count; i++) {
        pmm_free_frame(start_frame + (i * PAGE_SIZE));
    }
}

/* Virtual memory management */
uint64_t vmm_allocate(size_t size) {
    size = align_up(size, PAGE_SIZE);
    size_t pages = size / PAGE_SIZE;

    /* Find free virtual address range */
    uint64_t virtual_addr = HEAP_START;
    for (uint64_t addr = HEAP_START; addr < HEAP_END; addr += PAGE_SIZE) {
        if (paging_get_physical_addr(addr) == 0) {
            /* Check if we have enough consecutive free pages */
            int found = 1;
            for (size_t i = 1; i < pages; i++) {
                if (paging_get_physical_addr(addr + (i * PAGE_SIZE)) != 0) {
                    found = 0;
                    break;
                }
            }
            if (found) {
                virtual_addr = addr;
                break;
            }
        }
    }

    if (virtual_addr >= HEAP_END) return 0;

    /* Allocate physical frames and map them */
    for (size_t i = 0; i < pages; i++) {
        uint64_t frame = pmm_allocate_frame();
        if (frame == 0) {
            /* Cleanup already allocated pages */
            for (size_t j = 0; j < i; j++) {
                paging_unmap_page(virtual_addr + (j * PAGE_SIZE));
            }
            return 0;
        }
        paging_map_page(virtual_addr + (i * PAGE_SIZE), frame, PAGE_PRESENT | PAGE_WRITABLE);
    }

    return virtual_addr;
}

void vmm_free(uint64_t virtual_addr, size_t size) {
    paging_unmap_range(virtual_addr, size);
}

/* Memory pool management */
memory_pool_t *memory_create_pool(size_t size) {
    memory_pool_t *pool = (memory_pool_t*)kmalloc(sizeof(memory_pool_t));
    if (!pool) return 0;

    pool->start = vmm_allocate(size);
    if (pool->start == 0) {
        kfree(pool);
        return 0;
    }

    pool->end = pool->start + size;
    pool->total_size = size;
    pool->used_size = 0;
    pool->free_size = size;
    pool->free_list = 0;
    pool->next = 0;

    return pool;
}

void memory_destroy_pool(memory_pool_t *pool) {
    if (!pool) return;
    
    vmm_free(pool->start, pool->total_size);
    kfree(pool);
}

void memory_get_stats(memory_stats_t *stats) {
    if (!stats) return;
    
    memory_statistics.available_physical = paging_get_available_memory();
    *stats = memory_statistics;
}

uint64_t memory_get_total(void) {
    return paging_get_total_memory();
}

uint64_t memory_get_available(void) {
    return paging_get_available_memory();
}

uint64_t memory_get_used(void) {
    return memory_statistics.used_physical;
}

/* Helper function implementations */
static memory_block_t *find_free_block(size_t size) {
    memory_block_t *current = heap_start;
    
    while (current) {
        if (current->free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    
    return 0;
}

static memory_block_t *split_block(memory_block_t *block, size_t size) {
    if (block->size < size + sizeof(memory_block_t) + MIN_BLOCK_SIZE) {
        return block; /* Cannot split */
    }

    memory_block_t *new_block = (memory_block_t*)((uint64_t)block + sizeof(memory_block_t) + size);
    new_block->size = block->size - size - sizeof(memory_block_t);
    new_block->free = 1;
    new_block->next = block->next;
    new_block->prev = block;

    if (block->next) {
        block->next->prev = new_block;
    }

    block->next = new_block;
    block->size = size;

    return block;
}

static void merge_free_blocks(memory_block_t *block) {
    /* Merge with next block */
    if (block->next && block->next->free) {
        block->size += block->next->size + sizeof(memory_block_t);
        if (block->next->next) {
            block->next->next->prev = block;
        }
        block->next = block->next->next;
    }

    /* Merge with previous block */
    if (block->prev && block->prev->free) {
        block->prev->size += block->size + sizeof(memory_block_t);
        if (block->next) {
            block->next->prev = block->prev;
        }
        block->prev->next = block->next;
    }
}

static void expand_heap(size_t size) {
    size = align_up(size, PAGE_SIZE);
    
    /* Allocate new virtual memory */
    uint64_t new_memory = vmm_allocate(size);
    if (new_memory == 0) return;

    /* Create new block at the end of heap */
    memory_block_t *new_block = (memory_block_t*)new_memory;
    new_block->size = size - sizeof(memory_block_t);
    new_block->free = 1;
    new_block->next = 0;
    new_block->prev = 0;

    /* Link to existing heap if possible */
    if (heap_end) {
        memory_block_t *last_block = heap_start;
        while (last_block->next) {
            last_block = last_block->next;
        }
        last_block->next = new_block;
        new_block->prev = last_block;
        
        /* Try to merge if adjacent */
        if ((uint64_t)last_block + sizeof(memory_block_t) + last_block->size == (uint64_t)new_block) {
            merge_free_blocks(last_block);
        }
    }

    heap_size += size;
    heap_end = (memory_block_t*)((uint64_t)heap_end + size);
}

static void *allocate_from_pool(memory_pool_t *pool, size_t size) {
    /* Simple pool allocation - can be improved */
    if (pool->free_size < size) return 0;
    
    void *ptr = (void*)(pool->start + pool->used_size);
    pool->used_size += size;
    pool->free_size -= size;
    
    return ptr;
}

static int validate_block(memory_block_t *block) {
    if (!block) return 0;
    if ((uint64_t)block < (uint64_t)heap_start) return 0;
    if ((uint64_t)block >= (uint64_t)heap_end) return 0;
    if (block->size == 0) return 0;
    if (block->size > heap_size) return 0;
    
    return 1;
}

void memory_barrier(void) {
    __asm__ volatile("mfence" ::: "memory");
}

void memory_read_barrier(void) {
    __asm__ volatile("lfence" ::: "memory");
}

void memory_write_barrier(void) {
    __asm__ volatile("sfence" ::: "memory");
}