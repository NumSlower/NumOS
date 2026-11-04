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

void pmm_init(struct mb2_tag_mmap* mmap, uint64_t mb2_addr) {
    if (!mmap) return;

    uint32_t num_entries = (mmap->size - sizeof(struct mb2_tag_mmap)) / mmap->entry_size;
    #define MMAP_ENTRY(i) ((struct mb2_mmap_entry*)((uint8_t*)mmap->entries + (i) * mmap->entry_size))

    /* 1. Highest address of *usable* RAM only */
    uint64_t highest_addr = 0;
    for (uint32_t i = 0; i < num_entries; i++) {
        struct mb2_mmap_entry* e = MMAP_ENTRY(i);
        if (e->type == 1 && e->base_addr + e->length > highest_addr)
            highest_addr = e->base_addr + e->length;
    }

    total_frames = highest_addr / PAGE_SIZE;
    uint64_t bitmap_size = (total_frames + 7) / 8;

    /* 2. Place bitmap after kernel AND after the multiboot2 info block */
    uint64_t mb2_end  = mb2_addr + *(uint32_t*)mb2_addr;
    uint64_t place    = (uint64_t)_kernel_end;
    if (mb2_end > place) place = mb2_end;
    bitmap = (uint8_t*)ALIGN_UP(place, PAGE_SIZE);

    memset(bitmap, 0xFF, bitmap_size);
    used_frames = total_frames;

    /* 3. Free usable regions */
    for (uint32_t i = 0; i < num_entries; i++) {
        struct mb2_mmap_entry* e = MMAP_ENTRY(i);
        if (e->type != 1) continue;
        uint64_t start = ALIGN_UP(e->base_addr, PAGE_SIZE) / PAGE_SIZE;
        uint64_t end   = (e->base_addr + e->length) / PAGE_SIZE;
        for (uint64_t f = start; f < end && f < total_frames; f++) {
            clear_bit(f);
            used_frames--;
        }
    }

    /* 4. Reserve: low 1 MiB, kernel, mb2 info, bitmap */
    uint64_t reserve_end = ALIGN_UP((uint64_t)bitmap + bitmap_size, PAGE_SIZE) / PAGE_SIZE;
    for (uint64_t f = 0; f < reserve_end && f < total_frames; f++) {
        if (f >= 0x100000 / PAGE_SIZE && f < (uint64_t)_kernel_start / PAGE_SIZE)
            continue;                /* gap between 1 MiB and kernel stays free */
        if (!test_bit(f)) { set_bit(f); used_frames++; }
    }
    #undef MMAP_ENTRY
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
