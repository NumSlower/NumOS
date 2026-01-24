/*
 * heap.c - Enhanced kernel heap allocator with proper memory management
 * Features: block coalescing, best-fit allocation, memory guards, statistics
 */

#include "cpu/heap.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"
#include "drivers/vga.h"

/* Heap state */
static struct heap_block *heap_start = NULL;
static struct heap_block *heap_free_list = NULL;
static struct heap_stats heap_stats = {0};
static int heap_initialized = 0;
static int guards_enabled = 1;
static void *heap_end = NULL;

/* Mutex for thread safety (not implemented yet, placeholder) */
static volatile int heap_lock = 0;

/* Helper functions */
static uint32_t heap_calculate_checksum(struct heap_block *block);
static int heap_validate_block(struct heap_block *block);
static struct heap_block* heap_find_best_fit(size_t size);
static struct heap_block* heap_split_block(struct heap_block *block, size_t size);
static void heap_coalesce_free_blocks(void);
static void heap_add_to_free_list(struct heap_block *block);
static void heap_remove_from_free_list(struct heap_block *block);
static void heap_update_stats(void);

/*
 * Calculate checksum for a heap block
 */
static uint32_t heap_calculate_checksum(struct heap_block *block) {
    uint32_t checksum = 0;
    checksum ^= (uint32_t)(block->magic >> 32);
    checksum ^= (uint32_t)(block->magic & 0xFFFFFFFF);
    checksum ^= (uint32_t)(block->size >> 32);
    checksum ^= (uint32_t)(block->size & 0xFFFFFFFF);
    checksum ^= block->flags;
    checksum ^= (uint32_t)(uintptr_t)block->prev;
    checksum ^= (uint32_t)(uintptr_t)block->next;
    return checksum;
}

/*
 * Validate a heap block
 */
static int heap_validate_block(struct heap_block *block) {
    if (!block) return 0;
    
    /* Check magic numbers */
    if (block->magic != HEAP_MAGIC_ALLOC && block->magic != HEAP_MAGIC_FREE) {
        return 0;
    }
    
    /* Check checksum */
    uint32_t expected = heap_calculate_checksum(block);
    if (guards_enabled && block->checksum != expected) {
        return 0;
    }
    
    /* Check size alignment */
    if (block->size == 0 || (block->size % HEAP_ALIGNMENT) != 0) {
        return 0;
    }
    
    /* Check size bounds */
    if (block->size > HEAP_SIZE) {
        return 0;
    }
    
    return 1;
}

/*
 * Initialize the heap
 */
void heap_init(void) {
    if (heap_initialized) {
        vga_writestring("Heap: Already initialized\n");
        return;
    }
    
    vga_writestring("Heap: Initializing allocator...\n");
    
    /* Allocate heap pages from virtual memory manager */
    size_t heap_pages = HEAP_SIZE / PAGE_SIZE;
    void *heap_memory = vmm_alloc_pages(heap_pages, PAGE_PRESENT | PAGE_WRITABLE);
    
    if (!heap_memory) {
        panic("Failed to allocate heap memory");
        return;
    }
    
    vga_writestring("Heap: Allocated ");
    print_dec(heap_pages);
    vga_writestring(" pages at 0x");
    print_hex((uint64_t)heap_memory);
    vga_writestring("\n");
    
    /* Initialize heap boundaries */
    heap_start = (struct heap_block*)heap_memory;
    heap_end = (void*)((uint8_t*)heap_memory + HEAP_SIZE);
    
    /* Initialize first free block */
    heap_start->magic = HEAP_MAGIC_FREE;
    heap_start->size = HEAP_SIZE - sizeof(struct heap_block);
    heap_start->flags = HEAP_FLAG_FREE | HEAP_FLAG_FIRST | HEAP_FLAG_LAST;
    heap_start->prev = NULL;
    heap_start->next = NULL;
    heap_start->checksum = heap_calculate_checksum(heap_start);
    
    /* Initialize free list */
    heap_free_list = heap_start;
    
    /* Initialize statistics */
    memset(&heap_stats, 0, sizeof(struct heap_stats));
    heap_stats.total_size = HEAP_SIZE;
    heap_stats.free_size = heap_start->size;
    heap_stats.used_size = 0;
    heap_stats.total_blocks = 1;
    heap_stats.free_blocks = 1;
    heap_stats.used_blocks = 0;
    heap_stats.largest_free = heap_start->size;
    heap_stats.smallest_free = heap_start->size;
    
    heap_initialized = 1;
    
    vga_writestring("Heap: Initialized ");
    print_dec(HEAP_SIZE / 1024);
    vga_writestring(" KB\n");
}

/*
 * Find best-fit free block
 */
static struct heap_block* heap_find_best_fit(size_t size) {
    struct heap_block *current = heap_free_list;
    struct heap_block *best_fit = NULL;
    size_t best_size = (size_t)-1;
    
    while (current) {
        if ((current->flags & HEAP_FLAG_FREE) && current->size >= size) {
            if (current->size < best_size) {
                best_fit = current;
                best_size = current->size;
                
                /* Perfect fit - stop searching */
                if (current->size == size) {
                    break;
                }
            }
        }
        current = current->next;
    }
    
    return best_fit;
}

/*
 * Split a block if it's too large
 */
static struct heap_block* heap_split_block(struct heap_block *block, size_t size) {
    /* Need enough space for new block header plus minimum block size */
    size_t required = size + sizeof(struct heap_block) + HEAP_MIN_SIZE;
    
    if (block->size < required) {
        return block;  /* Can't split */
    }
    
    /* Create new block */
    struct heap_block *new_block = (struct heap_block*)((uint8_t*)block + size);
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->size = block->size - size;
    new_block->flags = HEAP_FLAG_FREE;
    new_block->prev = block;
    new_block->next = block->next;
    new_block->checksum = heap_calculate_checksum(new_block);
    
    /* Update original block */
    block->size = size;
    block->next = new_block;
    block->flags &= ~HEAP_FLAG_LAST;
    block->checksum = heap_calculate_checksum(block);
    
    /* Update next block's prev pointer */
    if (new_block->next) {
        new_block->next->prev = new_block;
        new_block->next->checksum = heap_calculate_checksum(new_block->next);
    } else {
        new_block->flags |= HEAP_FLAG_LAST;
    }
    
    /* Add new block to free list */
    heap_add_to_free_list(new_block);
    
    heap_stats.total_blocks++;
    
    return block;
}

/*
 * Add block to free list
 */
static void heap_add_to_free_list(struct heap_block *block) {
    block->next = heap_free_list;
    if (heap_free_list) {
        heap_free_list->prev = block;
    }
    heap_free_list = block;
    block->prev = NULL;
}

/*
 * Remove block from free list
 */
static void heap_remove_from_free_list(struct heap_block *block) {
    if (block->prev) {
        block->prev->next = block->next;
    } else {
        heap_free_list = block->next;
    }
    
    if (block->next) {
        block->next->prev = block->prev;
    }
}

/*
 * Coalesce adjacent free blocks
 */
static void heap_coalesce_free_blocks(void) {
    struct heap_block *current = heap_start;
    
    while (current && (uint8_t*)current < (uint8_t*)heap_end) {
        if ((current->flags & HEAP_FLAG_FREE) && current->next && 
            (current->next->flags & HEAP_FLAG_FREE)) {
            
            /* Merge with next block */
            struct heap_block *next = current->next;
            
            /* Remove next from free list */
            heap_remove_from_free_list(next);
            
            /* Merge */
            current->size += next->size;
            current->next = next->next;
            
            if (next->next) {
                next->next->prev = current;
                next->next->checksum = heap_calculate_checksum(next->next);
            } else {
                current->flags |= HEAP_FLAG_LAST;
            }
            
            current->checksum = heap_calculate_checksum(current);
            
            heap_stats.total_blocks--;
        } else {
            current = current->next;
        }
    }
}

/*
 * Allocate memory
 */
void* kmalloc(size_t size) {
    if (!heap_initialized) {
        heap_init();
    }
    
    if (size == 0) {
        return NULL;
    }
    
    /* Align size */
    size_t total_size = (size + sizeof(struct heap_block) + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
    
    /* Ensure minimum size */
    if (total_size < (sizeof(struct heap_block) + HEAP_MIN_SIZE)) {
        total_size = sizeof(struct heap_block) + HEAP_MIN_SIZE;
    }
    
    /* Find best fit block */
    struct heap_block *block = heap_find_best_fit(total_size);
    if (!block) {
        heap_stats.allocation_failures++;
        return NULL;
    }
    
    /* Remove from free list */
    heap_remove_from_free_list(block);
    
    /* Split if possible */
    if (block->size > total_size + sizeof(struct heap_block) + HEAP_MIN_SIZE) {
        heap_split_block(block, total_size);
    }
    
    /* Mark as used */
    block->magic = HEAP_MAGIC_ALLOC;
    block->flags = (block->flags & ~HEAP_FLAG_FREE) | HEAP_FLAG_USED;
    block->checksum = heap_calculate_checksum(block);
    
    /* Update statistics */
    heap_stats.allocations++;
    heap_stats.used_blocks++;
    heap_stats.free_blocks--;
    
    /* Return pointer to usable memory (after header) */
    return (void*)((uint8_t*)block + sizeof(struct heap_block));
}

/*
 * Allocate and zero memory
 */
void* kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Reallocate memory
 */
void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    /* Get block header */
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    if (!heap_validate_block(block)) {
        return NULL;
    }
    
    size_t old_size = block->size - sizeof(struct heap_block);
    
    /* If new size fits in current block, just return it */
    if (new_size <= old_size) {
        return ptr;
    }
    
    /* Allocate new block */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy data */
    memcpy(new_ptr, ptr, old_size);
    
    /* Free old block */
    kfree(ptr);
    
    return new_ptr;
}

/*
 * Free memory
 */
void kfree(void* ptr) {
    if (!ptr) {
        return;
    }
    
    /* Get block header */
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    /* Validate block */
    if (!heap_validate_block(block)) {
        vga_writestring("Heap: Invalid block at 0x");
        print_hex((uint64_t)ptr);
        vga_writestring("\n");
        heap_stats.corruptions++;
        return;
    }
    
    if (!(block->flags & HEAP_FLAG_USED)) {
        vga_writestring("Heap: Double free at 0x");
        print_hex((uint64_t)ptr);
        vga_writestring("\n");
        return;
    }
    
    /* Mark as free */
    block->magic = HEAP_MAGIC_FREE;
    block->flags = (block->flags & ~HEAP_FLAG_USED) | HEAP_FLAG_FREE;
    
    /* Clear memory if guards enabled */
    if (guards_enabled) {
        memset(ptr, 0xDD, block->size - sizeof(struct heap_block));
    }
    
    block->checksum = heap_calculate_checksum(block);
    
    /* Add to free list */
    heap_add_to_free_list(block);
    
    /* Update statistics */
    heap_stats.deallocations++;
    heap_stats.used_blocks--;
    heap_stats.free_blocks++;
    
    /* Coalesce adjacent free blocks */
    heap_coalesce_free_blocks();
    
    /* Update stats after coalescing */
    heap_update_stats();
}

/*
 * Allocate aligned memory
 */
void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL;  /* Invalid alignment (must be power of 2) */
    }
    
    /* Allocate extra space for alignment */
    size_t alloc_size = size + alignment + sizeof(struct heap_block);
    void *ptr = kmalloc(alloc_size);
    
    if (!ptr) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    
    return (void*)aligned;
}

/*
 * Allocate and zero aligned memory
 */
void* kzalloc_aligned(size_t size, size_t alignment) {
    void *ptr = kmalloc_aligned(size, alignment);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

/*
 * Allocate array
 */
void* kcalloc(size_t count, size_t size) {
    /* Check for overflow */
    if (count != 0 && size > (size_t)-1 / count) {
        return NULL;
    }
    
    return kzalloc(count * size);
}

/*
 * Duplicate string
 */
char* kstrdup(const char* str) {
    if (!str) {
        return NULL;
    }
    
    size_t len = strlen(str) + 1;
    char *copy = (char*)kmalloc(len);
    
    if (copy) {
        memcpy(copy, str, len);
    }
    
    return copy;
}

/*
 * Update statistics
 */
static void heap_update_stats(void) {
    struct heap_block *current = heap_start;
    
    heap_stats.total_blocks = 0;
    heap_stats.used_blocks = 0;
    heap_stats.free_blocks = 0;
    heap_stats.used_size = 0;
    heap_stats.free_size = 0;
    heap_stats.largest_free = 0;
    heap_stats.smallest_free = (uint64_t)-1;
    
    while (current && (uint8_t*)current < (uint8_t*)heap_end) {
        if (!heap_validate_block(current)) {
            heap_stats.corruptions++;
            break;
        }
        
        heap_stats.total_blocks++;
        
        if (current->flags & HEAP_FLAG_FREE) {
            heap_stats.free_blocks++;
            heap_stats.free_size += current->size;
            
            if (current->size > heap_stats.largest_free) {
                heap_stats.largest_free = current->size;
            }
            if (current->size < heap_stats.smallest_free) {
                heap_stats.smallest_free = current->size;
            }
        } else if (current->flags & HEAP_FLAG_USED) {
            heap_stats.used_blocks++;
            heap_stats.used_size += current->size;
        }
        
        current = current->next;
    }
    
    if (heap_stats.free_blocks == 0) {
        heap_stats.smallest_free = 0;
    }
}

/*
 * Get heap statistics
 */
struct heap_stats heap_get_stats(void) {
    heap_update_stats();
    return heap_stats;
}

/*
 * Print heap statistics
 */
void heap_print_stats(void) {
    heap_update_stats();
    
    vga_writestring("Heap Statistics:\n");
    vga_writestring("  Total size:      ");
    print_dec(heap_stats.total_size / 1024);
    vga_writestring(" KB\n");
    
    vga_writestring("  Used size:       ");
    print_dec(heap_stats.used_size / 1024);
    vga_writestring(" KB (");
    print_dec((heap_stats.used_size * 100) / heap_stats.total_size);
    vga_writestring("%)\n");
    
    vga_writestring("  Free size:       ");
    print_dec(heap_stats.free_size / 1024);
    vga_writestring(" KB (");
    print_dec((heap_stats.free_size * 100) / heap_stats.total_size);
    vga_writestring("%)\n");
    
    vga_writestring("  Total blocks:    ");
    print_dec(heap_stats.total_blocks);
    vga_writestring("\n");
    
    vga_writestring("  Used blocks:     ");
    print_dec(heap_stats.used_blocks);
    vga_writestring("\n");
    
    vga_writestring("  Free blocks:     ");
    print_dec(heap_stats.free_blocks);
    vga_writestring("\n");
    
    vga_writestring("  Allocations:     ");
    print_dec(heap_stats.allocations);
    vga_writestring("\n");
    
    vga_writestring("  Deallocations:   ");
    print_dec(heap_stats.deallocations);
    vga_writestring("\n");
    
    vga_writestring("  Failures:        ");
    print_dec(heap_stats.allocation_failures);
    vga_writestring("\n");
    
    vga_writestring("  Corruptions:     ");
    print_dec(heap_stats.corruptions);
    vga_writestring("\n");
    
    vga_writestring("  Largest free:    ");
    print_dec(heap_stats.largest_free);
    vga_writestring(" bytes\n");
}

/*
 * Print all blocks (for debugging)
 */
void heap_print_blocks(void) {
    struct heap_block *current = heap_start;
    int block_num = 0;
    
    vga_writestring("Heap Blocks:\n");
    
    while (current && (uint8_t*)current < (uint8_t*)heap_end) {
        vga_writestring("  Block ");
        print_dec(block_num++);
        vga_writestring(" at 0x");
        print_hex((uint64_t)current);
        vga_writestring(": ");
        print_dec(current->size);
        vga_writestring(" bytes, ");
        
        if (current->flags & HEAP_FLAG_USED) {
            vga_writestring("USED");
        } else if (current->flags & HEAP_FLAG_FREE) {
            vga_writestring("FREE");
        } else {
            vga_writestring("????");
        }
        
        vga_writestring("\n");
        
        current = current->next;
    }
}

/*
 * Validate entire heap
 */
int heap_validate(void) {
    struct heap_block *current = heap_start;
    int valid = 1;
    
    while (current && (uint8_t*)current < (uint8_t*)heap_end) {
        if (!heap_validate_block(current)) {
            vga_writestring("Heap: Corruption at 0x");
            print_hex((uint64_t)current);
            vga_writestring("\n");
            valid = 0;
            heap_stats.corruptions++;
        }
        current = current->next;
    }
    
    return valid;
}

/*
 * Defragment heap
 */
void heap_defragment(void) {
    heap_coalesce_free_blocks();
    heap_update_stats();
}

/*
 * Check for corruption
 */
int heap_check_corruption(void) {
    return heap_validate();
}

/*
 * Enable/disable memory guards
 */
void heap_enable_guards(int enable) {
    guards_enabled = enable;
}

/*
 * Get block size
 */
size_t heap_get_block_size(void* ptr) {
    if (!ptr) {
        return 0;
    }
    
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    if (!heap_validate_block(block)) {
        return 0;
    }
    
    return block->size - sizeof(struct heap_block);
}

/*
 * Page-based allocation (convenience functions)
 */
void* kmalloc_pages(size_t pages) {
    return vmm_alloc_pages(pages, PAGE_PRESENT | PAGE_WRITABLE);
}

void kfree_pages(void* ptr, size_t pages) {
    vmm_free_pages(ptr, pages);
}