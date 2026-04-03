#include "cpu/paging.h"

static struct paging_stats paging_stats_data;
static struct pmm_stats pmm_stats_data;
static struct page_table kernel_root __attribute__((aligned(PAGE_SIZE)));
static struct page_table *active_root = &kernel_root;

void paging_init(uint64_t reserved_phys_end) {
    (void)reserved_phys_end;
    for (size_t i = 0; i < PAGE_ENTRIES; i++) {
        kernel_root.entries[i] = 0;
    }
    active_root = &kernel_root;
}

void paging_flush_page(uint64_t virtual_addr) {
    (void)virtual_addr;
    __asm__ volatile("dsb ishst\n\tisb" ::: "memory");
}

int paging_map_page(uint64_t virtual_addr, uint64_t physical_addr,
                    uint64_t flags) {
    (void)virtual_addr;
    (void)physical_addr;
    (void)flags;
    paging_stats_data.pages_mapped++;
    return 0;
}

int paging_unmap_page(uint64_t virtual_addr) {
    (void)virtual_addr;
    paging_stats_data.pages_unmapped++;
    return 0;
}

int paging_is_mapped(uint64_t virtual_addr) {
    (void)virtual_addr;
    return 0;
}

uint64_t paging_get_physical_address(uint64_t virtual_addr) {
    return virtual_addr;
}

int paging_create_vm_region(uint64_t start, uint64_t end, uint64_t flags) {
    (void)start;
    (void)end;
    (void)flags;
    return 0;
}

struct vm_region *paging_find_vm_region(uint64_t addr) {
    (void)addr;
    return 0;
}

struct page_table *paging_get_page_table(uint64_t virtual_addr, int create) {
    (void)virtual_addr;
    (void)create;
    return active_root;
}

page_entry_t *paging_get_page_entry(uint64_t virtual_addr, int create) {
    (void)virtual_addr;
    (void)create;
    return 0;
}

void pmm_init(struct physical_memory_info *mem_info) {
    if (!mem_info) return;
    pmm_stats_data.total_memory = mem_info->total_memory;
    pmm_stats_data.available_memory = mem_info->available_memory;
}

uint64_t pmm_alloc_frame(void) {
    paging_stats_data.allocation_failures++;
    return 0;
}

void pmm_free_frame(uint64_t frame_addr) {
    (void)frame_addr;
}

void pmm_get_stats(struct pmm_stats *out) {
    if (!out) return;
    *out = pmm_stats_data;
}

void vmm_init(void) {
}

void *vmm_alloc_pages(size_t num_pages, uint64_t flags) {
    (void)num_pages;
    (void)flags;
    return 0;
}

void vmm_free_pages(void *virtual_addr, size_t num_pages) {
    (void)virtual_addr;
    (void)num_pages;
}

void paging_get_stats(struct paging_stats *out) {
    if (!out) return;
    *out = paging_stats_data;
}

uint64_t paging_get_kernel_cr3(void) {
    return (uint64_t)(uintptr_t)&kernel_root;
}

uint64_t paging_get_current_cr3(void) {
    return (uint64_t)(uintptr_t)active_root;
}

void paging_switch_to(uint64_t cr3) {
    active_root = (struct page_table *)(uintptr_t)cr3;
}

uint64_t paging_create_user_pml4(void) {
    return 0;
}

struct page_table *paging_get_active_pml4(void) {
    return active_root;
}

void paging_set_active_pml4(struct page_table *pml4) {
    active_root = pml4;
}

uint64_t paging_align_up(uint64_t addr, uint64_t alignment) {
    if (alignment == 0) return addr;
    return (addr + alignment - 1) & ~(alignment - 1);
}

uint64_t paging_align_down(uint64_t addr, uint64_t alignment) {
    if (alignment == 0) return addr;
    return addr & ~(alignment - 1);
}

void page_fault_handler(uint64_t error_code, uint64_t fault_addr) {
    (void)error_code;
    (void)fault_addr;
}
