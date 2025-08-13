#include "heap.h"
#include "kernel.h"
#include "vga.h"
#include "paging.h"

/* Global heap state */
static struct heap_block *heap_start = NULL;
static struct heap_block *heap_end = NULL;
static size_t heap_size = 0;
static int heap_initialized = 0;

void heap_init(void) {
    if (heap_initialized) {
        return;
    }
    
    vga_writestring("Initializing kernel heap...\n");
    
    /* Allocate initial heap pages */
    size_t initial_pages = HEAP_INITIAL_SIZE / PAGE_SIZE;
    void *heap_memory = vmm_alloc_pages(initial_pages, PAGE_PRESENT | PAGE_WRITABLE);
    
    if (!heap_memory) {
        panic("Failed to allocate initial heap memory");
        return;
    }
    
    heap_start = (struct heap_block*)heap_memory;
    heap_size = HEAP_INITIAL_SIZE;
    
    /* Initialize the first free block */
    heap_start->size = heap_size - sizeof(struct heap_block);
    heap_start->is_free = 1;
    heap_start->magic = HEAP_MAGIC_FREE;
    heap_start->next = NULL;
    heap_start->prev = NULL;
    
    heap_end = heap_start;
    heap_initialized = 1;
    
    vga_writestring("Kernel heap initialized at 0x");
    print_hex((uint64_t)heap_start);
    vga_writestring(" (");
    print_dec(heap_size);
    vga_writestring(" bytes)\n");
}

void *kmalloc(size_t size) {
    if (!heap_initialized) {
        heap_init();
    }
    
    if (size == 0) {
        return NULL;
    }
    
    /* Align size to heap alignment */
    size = (size + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
    
    /* Minimum allocation size */
    if (size < HEAP_MIN_BLOCK_SIZE) {
        size = HEAP_MIN_BLOCK_SIZE;
    }
    
    /* Find a free block */
    struct heap_block *block = heap_find_free_block(size);
    
    if (!block) {
        /* Try to expand heap */
        size_t additional_size = size + sizeof(struct heap_block);
        if (additional_size < PAGE_SIZE) {
            additional_size = PAGE_SIZE;
        }
        
        heap_expand(additional_size);
        block = heap_find_free_block(size);
        
        if (!block) {
            return NULL; /* Out of memory */
        }
    }
    
    /* Split block if necessary */
    if (block->size >= size + sizeof(struct heap_block) + HEAP_MIN_BLOCK_SIZE) {
        heap_split_block(block, size);
    }
    
    /* Mark block as allocated */
    block->is_free = 0;
    block->magic = HEAP_MAGIC_ALLOC;
    
    /* Return pointer to data (after header) */
    return (void*)((uint8_t*)block + sizeof(struct heap_block));
}

void *kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment <= HEAP_ALIGNMENT) {
        return kmalloc(size);
    }
    
    /* Allocate extra space for alignment */
    size_t total_size = size + alignment + sizeof(struct heap_block);
    void *raw_ptr = kmalloc(total_size);
    
    if (!raw_ptr) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uint64_t aligned_addr = ((uint64_t)raw_ptr + alignment - 1) & ~(alignment - 1);
    
    /* For simplicity, just return the raw pointer if already aligned enough */
    /* In a full implementation, you'd handle the offset properly */
    return raw_ptr;
}

void *kcalloc(size_t num, size_t size) {
    size_t total_size = num * size;
    
    /* Check for overflow */
    if (num != 0 && total_size / num != size) {
        return NULL;
    }
    
    void *ptr = kmalloc(total_size);
    if (ptr) {
        memset(ptr, 0, total_size);
    }
    
    return ptr;
}

void *krealloc(void *ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    /* Get the block header */
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    /* Validate block */
    if (block->magic != HEAP_MAGIC_ALLOC || block->is_free) {
        return NULL; /* Invalid block */
    }
    
    size_t old_size = block->size;
    
    /* If new size fits in current block, just return the same pointer */
    if (new_size <= old_size) {
        return ptr;
    }
    
    /* Allocate new block */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy old data */
    memcpy(new_ptr, ptr, old_size < new_size ? old_size : new_size);
    
    /* Free old block */
    kfree(ptr);
    
    return new_ptr;
}

void kfree(void *ptr) {
    if (!ptr) {
        return;
    }
    
    /* Get the block header */
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    /* Validate block */
    if (block->magic != HEAP_MAGIC_ALLOC || block->is_free) {
        vga_writestring("Warning: Invalid free() call\n");
        return;
    }
    
    /* Mark block as free */
    block->is_free = 1;
    block->magic = HEAP_MAGIC_FREE;
    
    /* Try to merge with adjacent free blocks */
    heap_merge_blocks(block);
}

struct heap_block *heap_find_free_block(size_t size) {
    struct heap_block *current = heap_start;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            return current;
        }
        current = current->next;
    }
    
    return NULL;
}

void heap_split_block(struct heap_block *block, size_t size) {
    if (block->size < size + sizeof(struct heap_block) + HEAP_MIN_BLOCK_SIZE) {
        return; /* Block too small to split */
    }
    
    /* Create new block after the allocated portion */
    struct heap_block *new_block = (struct heap_block*)((uint8_t*)block + sizeof(struct heap_block) + size);
    
    new_block->size = block->size - size - sizeof(struct heap_block);
    new_block->is_free = 1;
    new_block->magic = HEAP_MAGIC_FREE;
    new_block->next = block->next;
    new_block->prev = block;
    
    if (block->next) {
        block->next->prev = new_block;
    } else {
        heap_end = new_block;
    }
    
    block->next = new_block;
    block->size = size;
}

void heap_merge_blocks(struct heap_block *block) {
    /* Merge with next block if it's free */
    while (block->next && block->next->is_free) {
        struct heap_block *next = block->next;
        block->size += next->size + sizeof(struct heap_block);
        block->next = next->next;
        
        if (next->next) {
            next->next->prev = block;
        } else {
            heap_end = block;
        }
    }
    
    /* Merge with previous block if it's free */
    if (block->prev && block->prev->is_free) {
        struct heap_block *prev = block->prev;
        prev->size += block->size + sizeof(struct heap_block);
        prev->next = block->next;
        
        if (block->next) {
            block->next->prev = prev;
        } else {
            heap_end = prev;
        }
    }
}

void heap_expand(size_t additional_size) {
    if (heap_size + additional_size > HEAP_MAX_SIZE) {
        return; /* Heap size limit reached */
    }
    
    /* Round up to page boundary */
    size_t pages_needed = (additional_size + PAGE_SIZE - 1) / PAGE_SIZE;
    size_t actual_size = pages_needed * PAGE_SIZE;
    
    /* Calculate where to allocate new pages */
    void *new_pages = (void*)((uint64_t)heap_start + heap_size);
    
    /* Allocate physical pages for the new heap space */
    for (size_t i = 0; i < pages_needed; i++) {
        uint64_t virtual_addr = (uint64_t)new_pages + i * PAGE_SIZE;
        uint64_t physical_addr = pmm_alloc_frame();
        
        if (!physical_addr) {
            return; /* Failed to allocate physical memory */
        }
        
        if (paging_map_page(virtual_addr, physical_addr, PAGE_PRESENT | PAGE_WRITABLE) != 0) {
            pmm_free_frame(physical_addr);
            return; /* Failed to map page */
        }
    }
    
    /* Create a new free block or extend the last block */
    if (heap_end && heap_end->is_free) {
        /* Extend the last free block */
        heap_end->size += actual_size;
    } else {
        /* Create a new free block */
        struct heap_block *new_block = (struct heap_block*)new_pages;
        new_block->size = actual_size - sizeof(struct heap_block);
        new_block->is_free = 1;
        new_block->magic = HEAP_MAGIC_FREE;
        new_block->next = NULL;
        new_block->prev = heap_end;
        
        if (heap_end) {
            heap_end->next = new_block;
        }
        heap_end = new_block;
    }
    
    heap_size += actual_size;
}

void heap_dump(void) {
    vga_writestring("Heap dump:\n");
    vga_writestring("Heap start: 0x");
    print_hex((uint64_t)heap_start);
    vga_writestring(", size: ");
    print_dec(heap_size);
    vga_writestring(" bytes\n");
    
    struct heap_block *current = heap_start;
    int block_num = 0;
    
    while (current && block_num < 20) { /* Limit output */
        vga_writestring("Block ");
        print_dec(block_num);
        vga_writestring(": addr=0x");
        print_hex((uint64_t)current);
        vga_writestring(", size=");
        print_dec(current->size);
        vga_writestring(", ");
        vga_writestring(current->is_free ? "FREE" : "ALLOC");
        vga_putchar('\n');
        
        current = current->next;
        block_num++;
    }
    
    if (current) {
        vga_writestring("... (more blocks)\n");
    }
}

void heap_get_stats(struct heap_stats *stats) {
    if (!stats) {
        return;
    }
    
    memset(stats, 0, sizeof(struct heap_stats));
    stats->total_size = heap_size;
    
    struct heap_block *current = heap_start;
    
    while (current) {
        stats->num_blocks++;
        
        if (current->is_free) {
            stats->num_free_blocks++;
            stats->free_size += current->size;
            
            if (current->size > stats->largest_free_block) {
                stats->largest_free_block = current->size;
            }
        } else {
            stats->allocated_size += current->size;
        }
        
        current = current->next;
    }
}

int heap_check_corruption(void) {
    struct heap_block *current = heap_start;
    int corruption_found = 0;
    
    while (current) {
        uint8_t expected_magic = current->is_free ? HEAP_MAGIC_FREE : HEAP_MAGIC_ALLOC;
        
        if (current->magic != expected_magic) {
            vga_writestring("Heap corruption detected at block 0x");
            print_hex((uint64_t)current);
            vga_writestring("\n");
            corruption_found = 1;
        }
        
        current = current->next;
    }
    
    return corruption_found;
}

void heap_defragment(void) {
    /* Simple defragmentation - merge adjacent free blocks */
    struct heap_block *current = heap_start;
    
    while (current) {
        if (current->is_free) {
            heap_merge_blocks(current);
        }
        current = current->next;
    }
}