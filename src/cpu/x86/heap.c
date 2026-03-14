/*
 * heap.c - Kernel heap allocator
 *
 * Best-fit allocation with block coalescing and optional memory guards.
 * All heap memory is sourced from the virtual memory manager (vmm_alloc_pages).
 *
 * Block layout (each allocation):
 *   [heap_block header][user data ...]
 *
 * Free list is a singly-linked list of free blocks.
 * The full block list is doubly-linked for coalescing.
 */

#include "cpu/heap.h"
#include "kernel/kernel.h"
#include "cpu/paging.h"
#include "drivers/vga.h"

/* =========================================================================
 * Module state
 * ======================================================================= */

static struct heap_block *heap_start     = NULL;  /* First block in the heap */
static struct heap_block *heap_free_list = NULL;  /* Head of the free list   */
static struct heap_stats  heap_stats     = {0};   /* Usage statistics        */
static void              *heap_end       = NULL;  /* One past the last byte  */
static int                heap_initialized = 0;   /* Init guard              */
static int                guards_enabled   = 1;   /* Enable checksums/wipes  */

/* =========================================================================
 * Internal helpers (forward declarations)
 * ======================================================================= */

static uint32_t       heap_calculate_checksum(struct heap_block *block);
static int            heap_validate_block(struct heap_block *block);
static struct heap_block *heap_find_best_fit(size_t size);
static struct heap_block *heap_split_block(struct heap_block *block, size_t size);
static void           heap_coalesce_free_blocks(void);
static void           heap_add_to_free_list(struct heap_block *block);
static void           heap_remove_from_free_list(struct heap_block *block);
static void           heap_update_stats(void);

/* =========================================================================
 * Checksum helpers
 * ======================================================================= */

/*
 * heap_calculate_checksum - derive a 32-bit integrity tag from a block header.
 * XORs the magic, size, flags, and neighbor pointers together so that any
 * single-field corruption changes the tag.
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
 * heap_validate_block - return 1 if the block header looks sane, 0 otherwise.
 * Checks magic number, optional checksum, size alignment, and size bounds.
 */
static int heap_validate_block(struct heap_block *block) {
    if (!block) return 0;

    if (block->magic != HEAP_MAGIC_ALLOC && block->magic != HEAP_MAGIC_FREE) {
        return 0;
    }

    if (guards_enabled) {
        uint32_t expected = heap_calculate_checksum(block);
        if (block->checksum != expected) {
            return 0;
        }
    }

    if (block->size == 0 || (block->size % HEAP_ALIGNMENT) != 0) {
        return 0;
    }

    if (block->size > HEAP_SIZE) {
        return 0;
    }

    return 1;
}

/* =========================================================================
 * Free list management
 * ======================================================================= */

/*
 * heap_add_to_free_list - prepend block to the head of the free list.
 */
static void heap_add_to_free_list(struct heap_block *block) {
    block->next = heap_free_list;
    if (heap_free_list) {
        heap_free_list->prev = block;
    }
    heap_free_list = block;
    block->prev    = NULL;
}

/*
 * heap_remove_from_free_list - unlink block from the free list.
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

/* =========================================================================
 * Allocation helpers
 * ======================================================================= */

/*
 * heap_find_best_fit - scan the free list for the smallest block >= size.
 * Returns NULL if no suitable block exists.
 */
static struct heap_block *heap_find_best_fit(size_t size) {
    struct heap_block *current  = heap_free_list;
    struct heap_block *best_fit = NULL;
    size_t             best_size = (size_t)-1;

    while (current) {
        if ((current->flags & HEAP_FLAG_FREE) && current->size >= size) {
            if (current->size < best_size) {
                best_fit  = current;
                best_size = current->size;
                if (current->size == size) break;  /* perfect fit */
            }
        }
        current = current->next;
    }

    return best_fit;
}

/*
 * heap_split_block - carve a smaller allocation out of a larger free block.
 * Leaves a new free block for the remainder if enough space exists.
 * Returns the (now-sized) original block.
 */
static struct heap_block *heap_split_block(struct heap_block *block, size_t size) {
    size_t required = size + sizeof(struct heap_block) + HEAP_MIN_SIZE;

    if (block->size < required) {
        return block;  /* not enough tail space to split */
    }

    /* Carve a new block from the tail */
    struct heap_block *new_block = (struct heap_block *)((uint8_t *)block + size);
    new_block->magic    = HEAP_MAGIC_FREE;
    new_block->size     = block->size - size;
    new_block->flags    = HEAP_FLAG_FREE;
    new_block->prev     = block;
    new_block->next     = block->next;
    new_block->checksum = heap_calculate_checksum(new_block);

    /* Update the original block */
    block->size  = size;
    block->next  = new_block;
    block->flags &= ~HEAP_FLAG_LAST;
    block->checksum = heap_calculate_checksum(block);

    /* Patch the successor's back-pointer */
    if (new_block->next) {
        new_block->next->prev     = new_block;
        new_block->next->checksum = heap_calculate_checksum(new_block->next);
    } else {
        new_block->flags |= HEAP_FLAG_LAST;
    }

    heap_add_to_free_list(new_block);
    heap_stats.total_blocks++;

    return block;
}

/*
 * heap_coalesce_free_blocks - merge adjacent free blocks to reduce fragmentation.
 * Walks the full block list from heap_start and merges each free block with its
 * free successor.
 */
static void heap_coalesce_free_blocks(void) {
    struct heap_block *current = heap_start;

    while (current && (uint8_t *)current < (uint8_t *)heap_end) {
        if ((current->flags & HEAP_FLAG_FREE) &&
             current->next &&
            (current->next->flags & HEAP_FLAG_FREE)) {

            struct heap_block *next = current->next;
            heap_remove_from_free_list(next);

            current->size += next->size;
            current->next  = next->next;

            if (next->next) {
                next->next->prev     = current;
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

/* =========================================================================
 * Statistics
 * ======================================================================= */

/*
 * heap_update_stats - recompute all counters by walking the full block list.
 * Called after free/coalesce operations to keep stats accurate.
 */
static void heap_update_stats(void) {
    struct heap_block *current = heap_start;

    heap_stats.total_blocks   = 0;
    heap_stats.used_blocks    = 0;
    heap_stats.free_blocks    = 0;
    heap_stats.used_size      = 0;
    heap_stats.free_size      = 0;
    heap_stats.largest_free   = 0;
    heap_stats.smallest_free  = (uint64_t)-1;

    while (current && (uint8_t *)current < (uint8_t *)heap_end) {
        if (!heap_validate_block(current)) {
            heap_stats.corruptions++;
            break;
        }

        heap_stats.total_blocks++;

        if (current->flags & HEAP_FLAG_FREE) {
            heap_stats.free_blocks++;
            heap_stats.free_size += current->size;
            if (current->size > heap_stats.largest_free)
                heap_stats.largest_free = current->size;
            if (current->size < heap_stats.smallest_free)
                heap_stats.smallest_free = current->size;
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

/* =========================================================================
 * Public API
 * ======================================================================= */

/*
 * heap_init - allocate HEAP_SIZE bytes from the VMM and set up the first
 * free block spanning the entire region.
 */
void heap_init(void) {
    if (heap_initialized) {
        vga_writestring("Heap: Already initialized\n");
        return;
    }

    vga_writestring("Heap: Initializing allocator...\n");

    size_t heap_pages  = HEAP_SIZE / PAGE_SIZE;
    void  *heap_memory = vmm_alloc_pages(heap_pages, PAGE_PRESENT | PAGE_WRITABLE);

    if (!heap_memory) {
        panic("Heap: Failed to allocate memory from VMM");
        return;
    }

    vga_writestring("Heap: Allocated ");
    print_dec(heap_pages);
    vga_writestring(" pages at 0x");
    print_hex((uint64_t)heap_memory);
    vga_writestring("\n");

    /* Set up module boundaries */
    heap_start = (struct heap_block *)heap_memory;
    heap_end   = (void *)((uint8_t *)heap_memory + HEAP_SIZE);

    /* Initialise the single spanning free block */
    heap_start->magic    = HEAP_MAGIC_FREE;
    heap_start->size     = HEAP_SIZE - sizeof(struct heap_block);
    heap_start->flags    = HEAP_FLAG_FREE | HEAP_FLAG_FIRST | HEAP_FLAG_LAST;
    heap_start->prev     = NULL;
    heap_start->next     = NULL;
    heap_start->checksum = heap_calculate_checksum(heap_start);

    heap_free_list = heap_start;

    /* Initialise statistics */
    memset(&heap_stats, 0, sizeof(struct heap_stats));
    heap_stats.total_size    = HEAP_SIZE;
    heap_stats.free_size     = heap_start->size;
    heap_stats.total_blocks  = 1;
    heap_stats.free_blocks   = 1;
    heap_stats.largest_free  = heap_start->size;
    heap_stats.smallest_free = heap_start->size;

    heap_initialized = 1;

    vga_writestring("Heap: Initialized ");
    print_dec(HEAP_SIZE / 1024);
    vga_writestring(" KB\n");
}

/*
 * kmalloc - allocate at least size bytes from the kernel heap.
 * Returns NULL on failure (no memory or heap not initialised).
 */
void *kmalloc(size_t size) {
    if (!heap_initialized) {
        heap_init();
    }

    if (size == 0) return NULL;

    /* Round up to a header-aligned total block size */
    size_t total_size = (size + sizeof(struct heap_block) + HEAP_ALIGNMENT - 1)
                        & ~(HEAP_ALIGNMENT - 1);

    if (total_size < sizeof(struct heap_block) + HEAP_MIN_SIZE) {
        total_size = sizeof(struct heap_block) + HEAP_MIN_SIZE;
    }

    struct heap_block *block = heap_find_best_fit(total_size);
    if (!block) {
        heap_stats.allocation_failures++;
        return NULL;
    }

    heap_remove_from_free_list(block);

    /* Split surplus space into a new free block */
    if (block->size > total_size + sizeof(struct heap_block) + HEAP_MIN_SIZE) {
        heap_split_block(block, total_size);
    }

    block->magic    = HEAP_MAGIC_ALLOC;
    block->flags    = (block->flags & ~HEAP_FLAG_FREE) | HEAP_FLAG_USED;
    block->checksum = heap_calculate_checksum(block);

    heap_stats.allocations++;
    heap_stats.used_blocks++;
    heap_stats.free_blocks--;

    /* Return the address immediately after the header */
    return (void *)((uint8_t *)block + sizeof(struct heap_block));
}

/*
 * kzalloc - allocate and zero-initialise size bytes.
 */
void *kzalloc(size_t size) {
    void *ptr = kmalloc(size);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

/*
 * krealloc - resize an existing allocation.
 * Allocates a new block, copies the old data, and frees the original.
 * Returns NULL if allocation fails; the original pointer remains valid.
 */
void *krealloc(void *ptr, size_t new_size) {
    if (!ptr)      return kmalloc(new_size);
    if (!new_size) { kfree(ptr); return NULL; }

    struct heap_block *block =
        (struct heap_block *)((uint8_t *)ptr - sizeof(struct heap_block));

    if (!heap_validate_block(block)) return NULL;

    size_t old_size = block->size - sizeof(struct heap_block);
    if (new_size <= old_size) return ptr;  /* current block is large enough */

    void *new_ptr = kmalloc(new_size);
    if (!new_ptr) return NULL;

    memcpy(new_ptr, ptr, old_size);
    kfree(ptr);
    return new_ptr;
}

/*
 * kfree - release a previously allocated block.
 * Guards against double-free and NULL.
 */
void kfree(void *ptr) {
    if (!ptr) return;

    struct heap_block *block =
        (struct heap_block *)((uint8_t *)ptr - sizeof(struct heap_block));

    if (!heap_validate_block(block)) {
        vga_writestring("Heap: Invalid block at 0x");
        print_hex((uint64_t)ptr);
        vga_writestring("\n");
        heap_stats.corruptions++;
        return;
    }

    if (!(block->flags & HEAP_FLAG_USED)) {
        vga_writestring("Heap: Double-free at 0x");
        print_hex((uint64_t)ptr);
        vga_writestring("\n");
        return;
    }

    block->magic = HEAP_MAGIC_FREE;
    block->flags = (block->flags & ~HEAP_FLAG_USED) | HEAP_FLAG_FREE;

    /* Poison freed memory to catch use-after-free bugs */
    if (guards_enabled) {
        memset(ptr, 0xDD, block->size - sizeof(struct heap_block));
    }

    block->checksum = heap_calculate_checksum(block);
    heap_add_to_free_list(block);

    heap_stats.deallocations++;
    heap_stats.used_blocks--;
    heap_stats.free_blocks++;

    heap_coalesce_free_blocks();
    heap_update_stats();
}

/*
 * kmalloc_aligned - allocate size bytes at an address aligned to alignment.
 * alignment must be a power of two.
 */
void *kmalloc_aligned(size_t size, size_t alignment) {
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) return NULL;

    /* Over-allocate so we can always find an aligned start */
    void *ptr = kmalloc(size + alignment + sizeof(struct heap_block));
    if (!ptr) return NULL;

    uintptr_t addr    = (uintptr_t)ptr;
    uintptr_t aligned = (addr + alignment - 1) & ~(alignment - 1);
    return (void *)aligned;
}

/*
 * kzalloc_aligned - aligned allocation followed by zero-fill.
 */
void *kzalloc_aligned(size_t size, size_t alignment) {
    void *ptr = kmalloc_aligned(size, alignment);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

/*
 * kcalloc - allocate count * size bytes, zero-initialised.
 * Returns NULL on overflow or allocation failure.
 */
void *kcalloc(size_t count, size_t size) {
    if (count != 0 && size > (size_t)-1 / count) return NULL;
    return kzalloc(count * size);
}

/*
 * kstrdup - allocate a copy of str on the heap.
 */
char *kstrdup(const char *str) {
    if (!str) return NULL;
    size_t len  = strlen(str) + 1;
    char  *copy = (char *)kmalloc(len);
    if (copy) memcpy(copy, str, len);
    return copy;
}

/*
 * kmalloc_pages - allocate num_pages pages from the VMM directly.
 * Use for large, page-aligned allocations that bypass the block allocator.
 */
void *kmalloc_pages(size_t pages) {
    return vmm_alloc_pages(pages, PAGE_PRESENT | PAGE_WRITABLE);
}

/*
 * kfree_pages - return pages previously allocated with kmalloc_pages.
 */
void kfree_pages(void *ptr, size_t pages) {
    vmm_free_pages(ptr, pages);
}

/* =========================================================================
 * Information and diagnostics
 * ======================================================================= */

/*
 * heap_get_stats - return a snapshot of current heap usage statistics.
 */
struct heap_stats heap_get_stats(void) {
    heap_update_stats();
    return heap_stats;
}

/*
 * heap_print_stats - write a formatted summary to the VGA console.
 */
void heap_print_stats(void) {
    heap_update_stats();

    vga_writestring("Heap Statistics:\n");

    vga_writestring("  Total size:    ");
    print_dec(heap_stats.total_size / 1024);
    vga_writestring(" KB\n");

    vga_writestring("  Used:          ");
    print_dec(heap_stats.used_size / 1024);
    vga_writestring(" KB (");
    print_dec((heap_stats.used_size * 100) / heap_stats.total_size);
    vga_writestring("%)\n");

    vga_writestring("  Free:          ");
    print_dec(heap_stats.free_size / 1024);
    vga_writestring(" KB (");
    print_dec((heap_stats.free_size * 100) / heap_stats.total_size);
    vga_writestring("%)\n");

    vga_writestring("  Blocks total:  ");  print_dec(heap_stats.total_blocks); vga_writestring("\n");
    vga_writestring("  Blocks used:   ");  print_dec(heap_stats.used_blocks);  vga_writestring("\n");
    vga_writestring("  Blocks free:   ");  print_dec(heap_stats.free_blocks);  vga_writestring("\n");
    vga_writestring("  Allocations:   ");  print_dec(heap_stats.allocations);  vga_writestring("\n");
    vga_writestring("  Deallocations: ");  print_dec(heap_stats.deallocations); vga_writestring("\n");
    vga_writestring("  Failures:      ");  print_dec(heap_stats.allocation_failures); vga_writestring("\n");
    vga_writestring("  Corruptions:   ");  print_dec(heap_stats.corruptions);  vga_writestring("\n");
    vga_writestring("  Largest free:  ");  print_dec(heap_stats.largest_free); vga_writestring(" bytes\n");
}

/*
 * heap_print_blocks - dump every block's address, size, and state.
 */
void heap_print_blocks(void) {
    struct heap_block *current = heap_start;
    int block_num = 0;

    vga_writestring("Heap Blocks:\n");

    while (current && (uint8_t *)current < (uint8_t *)heap_end) {
        vga_writestring("  Block ");
        print_dec((uint64_t)block_num++);
        vga_writestring(" @ 0x");
        print_hex((uint64_t)current);
        vga_writestring(": ");
        print_dec(current->size);
        vga_writestring(" bytes  ");
        vga_writestring((current->flags & HEAP_FLAG_USED) ? "USED" :
                        (current->flags & HEAP_FLAG_FREE) ? "FREE" : "????");
        vga_writestring("\n");
        current = current->next;
    }
}

/*
 * heap_validate - walk every block and verify its checksum/magic.
 * Returns 1 if the heap is intact, 0 if corruption was detected.
 */
int heap_validate(void) {
    struct heap_block *current = heap_start;
    int valid = 1;

    while (current && (uint8_t *)current < (uint8_t *)heap_end) {
        if (!heap_validate_block(current)) {
            vga_writestring("Heap: Corruption detected at 0x");
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
 * heap_defragment - coalesce all adjacent free blocks.
 */
void heap_defragment(void) {
    heap_coalesce_free_blocks();
    heap_update_stats();
}

/*
 * heap_check_corruption - alias for heap_validate; satisfies the public API.
 */
int heap_check_corruption(void) {
    return heap_validate();
}

/*
 * heap_enable_guards - toggle checksum verification and freed-memory poisoning.
 * Pass 1 to enable (default), 0 to disable.
 */
void heap_enable_guards(int enable) {
    guards_enabled = enable;
}

/*
 * heap_get_block_size - return the usable byte count of the block backing ptr.
 * Returns 0 if ptr is NULL or the block is corrupt.
 */
size_t heap_get_block_size(void *ptr) {
    if (!ptr) return 0;

    struct heap_block *block =
        (struct heap_block *)((uint8_t *)ptr - sizeof(struct heap_block));

    if (!heap_validate_block(block)) return 0;

    return block->size - sizeof(struct heap_block);
}