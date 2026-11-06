/* NumOS — heap.c — Kernel Heap */
#include "cpu/heap.h"
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "libs/string.h"
#include "kernel/spinlock.h"
#include "libs/kdebug.h"
#include "drivers/vga.h"

struct heap_block {
    uint64_t size;
    bool is_free;
    struct heap_block* next;
};

/* Header is naturally 24 bytes, keeping returned data 8-byte aligned. */
#define HEAP_HEADER_SIZE sizeof(struct heap_block)

static struct heap_block* heap_head = NULL;
static spinlock_t heap_lock = {0};

void heap_init(uint64_t start, size_t size) {
    kdebug("Heap region: 0x%lx - 0x%lx (%lu KB)", start, start + size, size / 1024);
    uint64_t mapped = 0;
    uint64_t skipped = 0;
    for (uint64_t addr = start; addr < start + size; addr += 4096) {
        if (vmm_is_mapped(addr)) {
            skipped++;
            continue;  /* Already identity-mapped by boot (2MB huge pages) */
        }
        uint64_t phys = pmm_alloc_frame();
        if (phys) {
            vmm_map(addr, phys, PAGE_WRITE);
            mapped++;
        }
    }
    kdebug("Heap pages: %lu mapped, %lu already present", mapped, skipped);
    
    heap_head = (struct heap_block*)start;
    heap_head->size = size - sizeof(struct heap_block);
    heap_head->is_free = true;
    heap_head->next = NULL;
    kdebug("Heap usable: %lu bytes (%lu KB)", heap_head->size, heap_head->size / 1024);
}

static inline size_t align_size(size_t size) {
    return (size + 15) & ~15;
}

void* kmalloc(size_t size) {
    if (size == 0) return NULL;
    size = align_size(size);
    
    spin_lock(&heap_lock);
    struct heap_block* current = heap_head;
    
    while (current) {
        if (current->is_free && current->size >= size) {
            if (current->size > size + sizeof(struct heap_block) + 16) {
                struct heap_block* new_block = (struct heap_block*)((uint8_t*)current + HEAP_HEADER_SIZE + size);
                new_block->size = current->size - size - sizeof(struct heap_block);
                new_block->is_free = true;
                new_block->next = current->next;
                
                current->size = size;
                current->next = new_block;
            }
            
            current->is_free = false;
            spin_unlock(&heap_lock);
            return (void*)((uint8_t*)current + HEAP_HEADER_SIZE);
        }
        current = current->next;
    }
    
    spin_unlock(&heap_lock);
    return NULL;
}

void kfree(void* ptr) {
    if (!ptr) return;
    
    spin_lock(&heap_lock);
    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - HEAP_HEADER_SIZE);
    block->is_free = true;
    
    struct heap_block* current = heap_head;
    while (current && current->next) {
        if (current->is_free && current->next->is_free) {
            current->size += sizeof(struct heap_block) + current->next->size;
            current->next = current->next->next;
        } else {
            current = current->next;
        }
    }
    
    spin_unlock(&heap_lock);
}

void* kcalloc(size_t count, size_t size) {
    void* ptr = kmalloc(count * size);
    if (ptr) memset(ptr, 0, count * size);
    return ptr;
}

void* krealloc(void* ptr, size_t new_size) {
    if (!ptr) return kmalloc(new_size);
    if (new_size == 0) {
        kfree(ptr);
        return NULL;
    }
    
    struct heap_block* block = (struct heap_block*)((uint8_t*)ptr - HEAP_HEADER_SIZE);
    if (block->size >= new_size) {
        return ptr;
    }
    
    void* new_ptr = kmalloc(new_size);
    if (new_ptr) {
        memcpy(new_ptr, ptr, block->size);
        kfree(ptr);
    }
    return new_ptr;
}
