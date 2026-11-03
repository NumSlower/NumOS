/* NumOS — pmm.c — Physical Memory Manager */
#include "kernel/pmm.h"
#include "kernel/string.h"
#include "kernel/spinlock.h"

extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

static uint8_t* bitmap;
static uint64_t total_frames = 0;
static uint64_t used_frames = 0;
static spinlock_t pmm_lock = {0};

static inline void set_bit(uint64_t bit) {
    bitmap[bit / 8] |= (1 << (bit % 8));
}

static inline void clear_bit(uint64_t bit) {
    bitmap[bit / 8] &= ~(1 << (bit % 8));
}

static inline bool test_bit(uint64_t bit) {
    return (bitmap[bit / 8] & (1 << (bit % 8))) != 0;
}

void pmm_init(struct mb2_tag_mmap* mmap) {
    if (!mmap) return;
    
    uint64_t highest_addr = 0;
    
    struct mb2_mmap_entry* entry = mmap->entries;
    uint32_t num_entries = (mmap->size - sizeof(struct mb2_tag_mmap)) / mmap->entry_size;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        uint64_t end_addr = entry[i].base_addr + entry[i].length;
        if (end_addr > highest_addr) {
            highest_addr = end_addr;
        }
    }
    
    total_frames = highest_addr / PAGE_SIZE;
    uint64_t bitmap_size = total_frames / 8;
    
    bitmap = (uint8_t*)ALIGN_UP((uint64_t)_kernel_end, PAGE_SIZE);
    
    memset(bitmap, 0xFF, bitmap_size);
    used_frames = total_frames;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        if (entry[i].type == 1) { // Available
            uint64_t start_frame = entry[i].base_addr / PAGE_SIZE;
            uint64_t end_frame = (entry[i].base_addr + entry[i].length) / PAGE_SIZE;
            
            for (uint64_t f = start_frame; f < end_frame; f++) {
                if (f < total_frames) {
                    clear_bit(f);
                    used_frames--;
                }
            }
        }
    }
    
    // Mark first 1MB as used
    for (uint64_t f = 0; f < 0x100000 / PAGE_SIZE; f++) {
        if (!test_bit(f)) {
            set_bit(f);
            used_frames++;
        }
    }
    
    // Mark kernel as used
    uint64_t kernel_start_frame = (uint64_t)_kernel_start / PAGE_SIZE;
    uint64_t kernel_end_frame = ALIGN_UP((uint64_t)bitmap + bitmap_size, PAGE_SIZE) / PAGE_SIZE;
    
    for (uint64_t f = kernel_start_frame; f < kernel_end_frame; f++) {
        if (!test_bit(f)) {
            set_bit(f);
            used_frames++;
        }
    }
}

uint64_t pmm_alloc_frame(void) {
    spin_lock(&pmm_lock);
    
    for (uint64_t i = 0; i < total_frames; i++) {
        if (!test_bit(i)) {
            set_bit(i);
            used_frames++;
            spin_unlock(&pmm_lock);
            return i * PAGE_SIZE;
        }
    }
    
    spin_unlock(&pmm_lock);
    return 0; // OOM
}

void pmm_free_frame(uint64_t phys) {
    uint64_t frame = phys / PAGE_SIZE;
    
    spin_lock(&pmm_lock);
    if (frame < total_frames && test_bit(frame)) {
        clear_bit(frame);
        used_frames--;
    }
    spin_unlock(&pmm_lock);
}

uint64_t pmm_get_free_count(void) {
    return total_frames - used_frames;
}

uint64_t pmm_get_total_count(void) {
    return total_frames;
}
