#include "paging.h"
#include "memory.h"
#include "kernel.h"
#include "vga.h"

/* Current page directory */
static page_table_t *current_directory = 0;
static page_table_t *kernel_directory = 0;

/* Physical memory management */
static uint64_t *page_frame_bitmap = 0;
static uint64_t total_frames = 0;
static uint64_t used_frames = 0;
static uint64_t next_free_frame = 0;

/* Memory regions */
static memory_region_t *memory_regions = 0;
static uint64_t total_memory = 0;
static uint64_t available_memory = 0;

/* Kernel heap boundaries */
extern char _kernel_end;
static uint64_t placement_address = 0;

/* Utility functions */
static void set_frame(uint64_t frame_addr);
static void clear_frame(uint64_t frame_addr);
static uint64_t test_frame(uint64_t frame_addr);
static uint64_t first_free_frame(void);
static page_table_t *get_page_table(uint64_t virtual_addr, page_table_t *dir, int make);

void paging_init(void) {
    /* Calculate placement address */
    placement_address = (uint64_t)&_kernel_end;
    placement_address = align_up(placement_address, PAGE_SIZE);

    /* Initialize memory regions */
    paging_add_memory_region(0x100000, 0x10000000, MEMORY_AVAILABLE); /* 256MB for now */
    
    /* Calculate total frames */
    total_frames = 0x10000000 / PAGE_SIZE; /* Adjust based on actual memory */
    
    /* Allocate frame bitmap */
    uint64_t bitmap_size = total_frames / 8;
    if (total_frames % 8) bitmap_size++;
    bitmap_size = align_up(bitmap_size, PAGE_SIZE);
    
    page_frame_bitmap = (uint64_t*)placement_address;
    placement_address += bitmap_size;
    
    /* Clear bitmap */
    memset(page_frame_bitmap, 0, bitmap_size);
    
    /* Mark frames as used up to placement address */
    for (uint64_t addr = 0; addr < placement_address; addr += PAGE_SIZE) {
        set_frame(addr);
        used_frames++;
    }
    
    /* Create kernel page directory */
    kernel_directory = (page_table_t*)placement_address;
    placement_address += sizeof(page_table_t);
    memset(kernel_directory, 0, sizeof(page_table_t));
    
    /* Identity map the first 4MB */
    for (uint64_t addr = 0; addr < 0x400000; addr += PAGE_SIZE) {
        paging_map_page(addr, addr, PAGE_PRESENT | PAGE_WRITABLE);
    }
    
    /* Map kernel heap */
    for (uint64_t addr = HEAP_START; addr < HEAP_START + 0x1000000; addr += PAGE_SIZE) {
        uint64_t frame = paging_allocate_page();
        if (frame != 0) {
            paging_map_page(addr, frame, PAGE_PRESENT | PAGE_WRITABLE);
        }
    }
    
    /* Set current directory */
    current_directory = kernel_directory;
    paging_switch_directory(kernel_directory);
}

void paging_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags) {
    virtual_addr &= PAGE_MASK;
    physical_addr &= PAGE_MASK;
    
    /* Get page table */
    page_table_t *table = get_page_table(virtual_addr, current_directory, 1);
    if (!table) return;
    
    /* Get page table entry index */
    uint32_t index = PT_OFFSET(virtual_addr);
    
    /* Set page entry */
    table->entries[index] = physical_addr | flags;
    
    /* Invalidate TLB entry */
    paging_invalidate_page(virtual_addr);
}

void paging_unmap_page(uint64_t virtual_addr) {
    virtual_addr &= PAGE_MASK;
    
    /* Get page table */
    page_table_t *table = get_page_table(virtual_addr, current_directory, 0);
    if (!table) return;
    
    /* Get page table entry index */
    uint32_t index = PT_OFFSET(virtual_addr);
    
    /* Get physical address */
    uint64_t physical_addr = table->entries[index] & PAGE_MASK;
    
    /* Clear page entry */
    table->entries[index] = 0;
    
    /* Free physical frame */
    if (physical_addr) {
        paging_free_page(physical_addr);
    }
    
    /* Invalidate TLB entry */
    paging_invalidate_page(virtual_addr);
}

uint64_t paging_get_physical_addr(uint64_t virtual_addr) {
    /* Get page table */
    page_table_t *table = get_page_table(virtual_addr, current_directory, 0);
    if (!table) return 0;
    
    /* Get page table entry index */
    uint32_t index = PT_OFFSET(virtual_addr);
    
    /* Get physical address */
    uint64_t physical_addr = table->entries[index] & PAGE_MASK;
    return physical_addr + (virtual_addr & 0xFFF);
}

uint64_t paging_allocate_page(void) {
    if (used_frames >= total_frames) {
        return 0; /* Out of memory */
    }
    
    uint64_t frame = first_free_frame();
    if (frame == (uint64_t)-1) {
        return 0; /* No free frames */
    }
    
    set_frame(frame);
    used_frames++;
    
    return frame;
}

void paging_free_page(uint64_t physical_addr) {
    physical_addr &= PAGE_MASK;
    
    if (test_frame(physical_addr)) {
        clear_frame(physical_addr);
        used_frames--;
    }
}

void paging_map_range(uint64_t virtual_start, uint64_t physical_start, uint64_t size, uint64_t flags) {
    virtual_start = align_down(virtual_start, PAGE_SIZE);
    physical_start = align_down(physical_start, PAGE_SIZE);
    size = align_up(size, PAGE_SIZE);
    
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        paging_map_page(virtual_start + offset, physical_start + offset, flags);
    }
}

void paging_unmap_range(uint64_t virtual_start, uint64_t size) {
    virtual_start = align_down(virtual_start, PAGE_SIZE);
    size = align_up(size, PAGE_SIZE);
    
    for (uint64_t offset = 0; offset < size; offset += PAGE_SIZE) {
        paging_unmap_page(virtual_start + offset);
    }
}

page_table_t *paging_get_current_directory(void) {
    return current_directory;
}

void paging_switch_directory(page_table_t *dir) {
    current_directory = dir;
    paging_load_directory(virt_to_phys((uint64_t)dir));
}

page_table_t *paging_create_directory(void) {
    page_table_t *dir = (page_table_t*)kmalloc(sizeof(page_table_t));
    if (!dir) return 0;
    
    memset(dir, 0, sizeof(page_table_t));
    
    /* Copy kernel mappings */
    for (int i = 256; i < PAGE_ENTRIES; i++) {
        dir->entries[i] = kernel_directory->entries[i];
    }
    
    return dir;
}

void paging_destroy_directory(page_table_t *dir) {
    if (!dir || dir == kernel_directory) return;
    
    /* Free user page tables */
    for (int i = 0; i < 256; i++) {
        if (dir->entries[i] & PAGE_PRESENT) {
            page_table_t *table = (page_table_t*)phys_to_virt(dir->entries[i] & PAGE_MASK);
            kfree(table);
        }
    }
    
    kfree(dir);
}

void paging_add_memory_region(uint64_t base, uint64_t length, uint32_t type) {
    memory_region_t *region = (memory_region_t*)kmalloc(sizeof(memory_region_t));
    if (!region) return;
    
    region->base = base;
    region->length = length;
    region->type = type;
    region->next = memory_regions;
    memory_regions = region;
    
    total_memory += length;
    if (type == MEMORY_AVAILABLE) {
        available_memory += length;
    }
}

memory_region_t *paging_get_memory_regions(void) {
    return memory_regions;
}

uint64_t paging_get_total_memory(void) {
    return total_memory;
}

uint64_t paging_get_available_memory(void) {
    return available_memory - (used_frames * PAGE_SIZE);
}

/* Utility function implementations */
static void set_frame(uint64_t frame_addr) {
    uint64_t frame = frame_addr / PAGE_SIZE;
    uint64_t idx = frame / 64;
    uint64_t off = frame % 64;
    page_frame_bitmap[idx] |= (1ULL << off);
}

static void clear_frame(uint64_t frame_addr) {
    uint64_t frame = frame_addr / PAGE_SIZE;
    uint64_t idx = frame / 64;
    uint64_t off = frame % 64;
    page_frame_bitmap[idx] &= ~(1ULL << off);
}

static uint64_t test_frame(uint64_t frame_addr) {
    uint64_t frame = frame_addr / PAGE_SIZE;
    uint64_t idx = frame / 64;
    uint64_t off = frame % 64;
    return (page_frame_bitmap[idx] & (1ULL << off)) ? 1 : 0;
}

static uint64_t first_free_frame(void) {
    uint64_t total_qwords = total_frames / 64;
    if (total_frames % 64) total_qwords++;
    
    for (uint64_t i = next_free_frame / 64; i < total_qwords; i++) {
        if (page_frame_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            for (int j = 0; j < 64; j++) {
                if (!(page_frame_bitmap[i] & (1ULL << j))) {
                    uint64_t frame = i * 64 + j;
                    next_free_frame = frame + 1;
                    return frame * PAGE_SIZE;
                }
            }
        }
    }
    
    /* Wrap around and search from beginning */
    for (uint64_t i = 0; i < next_free_frame / 64; i++) {
        if (page_frame_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            for (int j = 0; j < 64; j++) {
                if (!(page_frame_bitmap[i] & (1ULL << j))) {
                    uint64_t frame = i * 64 + j;
                    next_free_frame = frame + 1;
                    return frame * PAGE_SIZE;
                }
            }
        }
    }
    
    return (uint64_t)-1; /* No free frames */
}

static page_table_t *get_page_table(uint64_t virtual_addr, page_table_t *dir, int make) {
    uint32_t pml4_index = PML4_OFFSET(virtual_addr);
    uint32_t pdp_index = PDP_OFFSET(virtual_addr);
    uint32_t pd_index = PD_OFFSET(virtual_addr);
    
    /* For simplicity, we're treating this as a simple 2-level page table */
    /* In a full 4-level implementation, you'd need to traverse all levels */
    
    if (!(dir->entries[pml4_index] & PAGE_PRESENT)) {
        if (!make) return 0;
        
        /* Allocate new page table */
        page_table_t *table = (page_table_t*)kmalloc(sizeof(page_table_t));
        if (!table) return 0;
        
        memset(table, 0, sizeof(page_table_t));
        dir->entries[pml4_index] = virt_to_phys((uint64_t)table) | PAGE_PRESENT | PAGE_WRITABLE;
    }
    
    return (page_table_t*)phys_to_virt(dir->entries[pml4_index] & PAGE_MASK);
}

uint64_t virt_to_phys(uint64_t virtual_addr) {
    if (virtual_addr >= KERNEL_VIRTUAL_BASE) {
        return virtual_addr - KERNEL_VIRTUAL_BASE;
    }
    return virtual_addr;
}

uint64_t phys_to_virt(uint64_t physical_addr) {
    return physical_addr + KERNEL_VIRTUAL_BASE;
}

int is_page_aligned(uint64_t addr) {
    return (addr & (PAGE_SIZE - 1)) == 0;
}

uint64_t align_up(uint64_t addr, uint64_t alignment) {
    return (addr + alignment - 1) & ~(alignment - 1);
}

uint64_t align_down(uint64_t addr, uint64_t alignment) {
    return addr & ~(alignment - 1);
}