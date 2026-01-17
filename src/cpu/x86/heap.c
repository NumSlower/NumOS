#include "cpu/heap.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"
#include "drivers/vga.h"

/* Heap state */
static struct heap_block *heap_start = NULL;
static struct heap_block *heap_end = NULL;
static struct heap_stats heap_stats = {0};
static int heap_initialized = 0;
static int guards_enabled = 0;

/* Helper functions */
static uint32_t heap_calculate_checksum(struct heap_block *block);
static int is_block_valid(struct heap_block *block);
static struct heap_block* find_free_block(size_t size);
static struct heap_block* split_block(struct heap_block *block, size_t size);
static void merge_free_blocks(void);
static void update_stats(void);

void heap_init(void) {
    if (heap_initialized) {
        return;
    }
    
    /* Allocate initial heap pages */
    size_t heap_pages = HEAP_SIZE / PAGE_SIZE;
    void *heap_memory = vmm_alloc_pages(heap_pages, PAGE_PRESENT | PAGE_WRITABLE);
    
    if (!heap_memory) {
        panic("Failed to allocate heap memory");
        return;
    }
    
    /* Initialize heap structure */
    heap_start = (struct heap_block*)heap_memory;
    heap_end = (struct heap_block*)((uint8_t*)heap_memory + HEAP_SIZE - sizeof(struct heap_block));
    
    /* Set up initial free block */
    heap_start->magic = HEAP_MAGIC_FREE;
    heap_start->size = HEAP_SIZE - sizeof(struct heap_block);
    heap_start->flags = HEAP_FLAG_FREE | HEAP_FLAG_FIRST | HEAP_FLAG_LAST;
    heap_start->checksum = heap_calculate_checksum(heap_start);
    heap_start->prev = NULL;
    heap_start->next = NULL;
    
    /* Initialize statistics */
    heap_stats.total_size = HEAP_SIZE;
    heap_stats.free_size = heap_start->size;
    heap_stats.used_size = 0;
    heap_stats.total_blocks = 1;
    heap_stats.free_blocks = 1;
    heap_stats.used_blocks = 0;
    heap_stats.largest_free = heap_start->size;
    heap_stats.smallest_free = heap_start->size;
    
    heap_initialized = 1;
    
    vga_writestring("Heap allocator initialized: ");
    print_dec(HEAP_SIZE / 1024);
    vga_writestring(" KB\n");
}

void* kmalloc(size_t size) {
    if (!heap_initialized) {
        heap_init();
    }
    
    if (size == 0) {
        return NULL;
    }
    
    /* Align size */
    size = (size + HEAP_ALIGNMENT - 1) & ~(HEAP_ALIGNMENT - 1);
    
    /* Ensure minimum size */
    if (size < HEAP_MIN_SIZE) {
        size = HEAP_MIN_SIZE;
    }
    
    /* Find a suitable free block */
    struct heap_block *block = find_free_block(size + sizeof(struct heap_block));
    if (!block) {
        return NULL; /* Out of memory */
    }
    
    /* Split block if it's too large */
    if (block->size > size + sizeof(struct heap_block) + HEAP_MIN_SIZE) {
        split_block(block, size + sizeof(struct heap_block));
    }
    
    /* Mark block as used */
    block->magic = HEAP_MAGIC_ALLOC;
    block->flags = (block->flags & ~HEAP_FLAG_FREE) | HEAP_FLAG_USED;
    block->checksum = heap_calculate_checksum(block);
    
    /* Update statistics */
    heap_stats.allocations++;
    update_stats();
    
    /* Return pointer to data (after header) */
    return (void*)((uint8_t*)block + sizeof(struct heap_block));
}

void* kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) {
        return kmalloc(new_size);
    }
    
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    /* Get current block */
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    if (!is_block_valid(block)) {
        return NULL; /* Invalid block */
    }
    
    size_t old_size = block->size - sizeof(struct heap_block);
    
    if (new_size <= old_size) {
        return ptr; /* Current block is sufficient */
    }
    
    /* Allocate new block */
    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) {
        return NULL;
    }
    
    /* Copy data */
    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    
    return new_ptr;
}

void kfree(void* ptr) {
    if (!ptr) {
        return;
    }
    
    /* Get block header */
    struct heap_block *block = (struct heap_block*)((uint8_t*)ptr - sizeof(struct heap_block));
    
    if (!is_block_valid(block) || !(block->flags & HEAP_FLAG_USED)) {
        vga_writestring("Warning: Attempting to free invalid or already free block\n");
        return;
    }
    
    /* Mark block as free */
    block->magic = HEAP_MAGIC_FREE;
    block->flags = (block->flags & ~HEAP_FLAG_USED) | HEAP_FLAG_FREE;
    block->checksum = heap_calculate_checksum(block);
    
    /* Clear the data if guards are enabled */
    if (guards_enabled) {
        memset(ptr, 0xDD, block->size - sizeof(struct heap_block));
    }
    
    /* Update statistics */
    heap_stats.deallocations++;
    
    /* Merge adjacent free blocks */
    merge_free_blocks();
    update_stats();
}

void* kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        return NULL; /* Invalid alignment */
    }
    
    size_t aligned_size = size + alignment + sizeof(struct heap_block);
    void *ptr = kmalloc(aligned_size);
    
    if (!ptr) {
        return NULL;
    }
    
    /* Calculate aligned address */
    uintptr_t addr = (uintptr_t)ptr;
    uintptr_t aligned_addr = (addr + alignment - 1) & ~(alignment - 1);
    
    return (void*)aligned_addr;
}

void* kzalloc_aligned(size_t size, size_t alignment) {
    void *ptr = kmalloc_aligned(size, alignment);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void* kcalloc(size_t count, size_t size) {
    size_t total_size = count * size;
    
    /* Check for overflow */
    if (count != 0 && total_size / count != size) {
        return NULL;
    }
    
    return kzalloc(total_size);
}

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

struct heap_stats heap_get_stats(void) {
    update_stats();
    return heap_stats;
}

void heap_print_stats(void) {
    update_stats();
    
    vga_writestring("Heap Statistics:\n");
    vga_writestring("  Total size:    ");
    print_dec(heap_stats.total_size);
    vga_writestring(" bytes\n");
    vga_writestring("  Used size:     ");
    print_dec(heap_stats.used_size);
    vga_writestring(" bytes\n");
    vga_writestring("  Free size:     ");
    print_dec(heap_stats.free_size);
    vga_writestring(" bytes\n");
    vga_writestring("  Total blocks:  ");
    print_dec(heap_stats.total_blocks);
    vga_writestring("\n");
    vga_writestring("  Used blocks:   ");
    print_dec(heap_stats.used_blocks);
    vga_writestring("\n");
    vga_writestring("  Free blocks:   ");
    print_dec(heap_stats.free_blocks);
    vga_writestring("\n");
    vga_writestring("  Allocations:   ");
    print_dec(heap_stats.allocations);
    vga_writestring("\n");
    vga_writestring("  Deallocations: ");
    print_dec(heap_stats.deallocations);
    vga_writestring("\n");
    vga_writestring("  Largest free:  ");
    print_dec(heap_stats.largest_free);
    vga_writestring(" bytes\n");
}

int heap_validate(void) {
    struct heap_block *block = heap_start;
    int valid = 1;
    
    while (block && (uint8_t*)block < (uint8_t*)heap_end) {
        if (!is_block_valid(block)) {
            vga_writestring("Heap corruption detected at block ");
            print_hex((uint64_t)block);
            vga_writestring("\n");
            valid = 0;
            heap_stats.corruptions++;
        }
        
        block = block->next;
    }
    
    return valid;
}

void heap_defragment(void) {
    merge_free_blocks();
    update_stats();
}

int heap_check_corruption(void) {
    return heap_validate();
}

void heap_enable_guards(int enable) {
    guards_enabled = enable;
}

/* Helper function implementations */
static uint32_t heap_calculate_checksum(struct heap_block *block) {
    /* Simple checksum calculation */
    uint32_t checksum = 0;
    checksum ^= block->magic;
    checksum ^= block->size;
    checksum ^= block->flags;
    checksum ^= (uint32_t)(uintptr_t)block->prev;
    checksum ^= (uint32_t)(uintptr_t)block->next;
    return checksum;
}

static int is_block_valid(struct heap_block *block) {
    if (!block) {
        return 0;
    }
    
    /* Check magic numbers */
    if (block->magic != HEAP_MAGIC_ALLOC && block->magic != HEAP_MAGIC_FREE) {
        return 0;
    }
    
    /* Check checksum */
    uint32_t expected = heap_calculate_checksum(block);
    if (block->checksum != expected) {
        return 0;
    }
    
    /* Check size alignment */
    if (block->size % HEAP_ALIGNMENT != 0) {
        return 0;
    }
    
    return 1;
}

static struct heap_block* find_free_block(size_t size) {
    struct heap_block *block = heap_start;
    struct heap_block *best_fit = NULL;
    
    while (block) {
        if ((block->flags & HEAP_FLAG_FREE) && block->size >= size) {
            if (!best_fit || block->size < best_fit->size) {
                best_fit = block;
            }
        }
        block = block->next;
    }
    
    return best_fit;
}

static struct heap_block* split_block(struct heap_block *block, size_t size) {
    if (block->size <= size + sizeof(struct heap_block)) {
        return block; /* Not enough space to split */
    }
    
    /* Create new block for remaining space */
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
    block->checksum = heap_calculate_checksum(block);
    
    /* Update next block's prev pointer */
    if (new_block->next) {
        new_block->next->prev = new_block;
        new_block->next->checksum = heap_calculate_checksum(new_block->next);
    }
    
    return block;
}

static void merge_free_blocks(void) {
    struct heap_block *block = heap_start;
    
    while (block && block->next) {
        if ((block->flags & HEAP_FLAG_FREE) && (block->next->flags & HEAP_FLAG_FREE)) {
            /* Merge with next block */
            struct heap_block *next = block->next;
            block->size += next->size;
            block->next = next->next;
            
            if (next->next) {
                next->next->prev = block;
                next->next->checksum = heap_calculate_checksum(next->next);
            }
            
            block->checksum = heap_calculate_checksum(block);
        } else {
            block = block->next;
        }
    }
}

static void update_stats(void) {
    struct heap_block *block = heap_start;
    
    heap_stats.total_blocks = 0;
    heap_stats.used_blocks = 0;
    heap_stats.free_blocks = 0;
    heap_stats.used_size = 0;
    heap_stats.free_size = 0;
    heap_stats.largest_free = 0;
    heap_stats.smallest_free = HEAP_SIZE;
    
    while (block) {
        heap_stats.total_blocks++;
        
        if (block->flags & HEAP_FLAG_FREE) {
            heap_stats.free_blocks++;
            heap_stats.free_size += block->size;
            
            if (block->size > heap_stats.largest_free) {
                heap_stats.largest_free = block->size;
            }
            if (block->size < heap_stats.smallest_free) {
                heap_stats.smallest_free = block->size;
            }
        } else if (block->flags & HEAP_FLAG_USED) {
            heap_stats.used_blocks++;
            heap_stats.used_size += block->size;
        }
        
        block = block->next;
    }
    
    if (heap_stats.free_blocks == 0) {
        heap_stats.smallest_free = 0;
    }
}