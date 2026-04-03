#include "cpu/paging.h"
#include "kernel/arm64.h"

#define ARM64_PTE_VALID        (1ULL << 0)
#define ARM64_PTE_TABLE        (1ULL << 1)
#define ARM64_PTE_PAGE         (1ULL << 1)
#define ARM64_PTE_ATTRIDX(n)   ((uint64_t)(n) << 2)
#define ARM64_PTE_USER         (1ULL << 6)
#define ARM64_PTE_RO           (1ULL << 7)
#define ARM64_PTE_SH_INNER     (3ULL << 8)
#define ARM64_PTE_AF           (1ULL << 10)
#define ARM64_PTE_PXN          (1ULL << 53)
#define ARM64_PTE_UXN          (1ULL << 54)
#define ARM64_ATTR_DEVICE      0
#define ARM64_ATTR_NORMAL      1
#define ARM64_L2_BLOCK_SIZE    (2ULL * 1024ULL * 1024ULL)
#define ARM64_MAX_PAGE_TABLES 128

static struct paging_stats paging_stats_data;
static struct pmm_stats pmm_stats_data;
static struct page_table kernel_root __attribute__((aligned(PAGE_SIZE)));
static struct page_table *active_root = &kernel_root;
static struct page_table page_table_pool[ARM64_MAX_PAGE_TABLES]
    __attribute__((aligned(PAGE_SIZE)));
static size_t page_table_pool_used = 0;
static uint64_t current_ttbr0 = 0;
static int paging_mmu_enabled = 0;

static struct page_table *alloc_page_table(void) {
    if (page_table_pool_used >= ARM64_MAX_PAGE_TABLES) {
        paging_stats_data.allocation_failures++;
        return 0;
    }

    struct page_table *table = &page_table_pool[page_table_pool_used++];
    for (size_t i = 0; i < PAGE_ENTRIES; i++) table->entries[i] = 0;
    return table;
}

static struct page_table *arm64_get_next_table(struct page_table *table,
                                               size_t index,
                                               int create) {
    if (!table || index >= PAGE_ENTRIES) return 0;

    uint64_t entry = table->entries[index];
    if (entry & ARM64_PTE_VALID) {
        if (!(entry & ARM64_PTE_TABLE)) return 0;
        return (struct page_table *)(uintptr_t)PAGE_ENTRY_ADDR(entry);
    }

    if (!create) return 0;

    struct page_table *next = alloc_page_table();
    if (!next) return 0;

    table->entries[index] = ((uint64_t)(uintptr_t)next & 0x0000FFFFFFFFF000ULL) |
                            ARM64_PTE_VALID |
                            ARM64_PTE_TABLE;
    return next;
}

static uint64_t arm64_make_page_desc(uint64_t physical_addr, uint64_t flags) {
    uint64_t desc = (physical_addr & 0x0000FFFFFFFFF000ULL) |
                    ARM64_PTE_VALID |
                    ARM64_PTE_PAGE |
                    ARM64_PTE_AF |
                    ARM64_PTE_SH_INNER |
                    ARM64_PTE_ATTRIDX(ARM64_ATTR_NORMAL);

    if (flags & PAGE_USER) desc |= ARM64_PTE_USER;
    if (!(flags & PAGE_WRITABLE)) desc |= ARM64_PTE_RO;
    if (flags & PAGE_NX) desc |= ARM64_PTE_PXN | ARM64_PTE_UXN;
    return desc;
}

static uint64_t arm64_make_block_desc(uint64_t physical_addr, int device) {
    uint64_t desc = (physical_addr & 0x0000FFFFFFE00000ULL) |
                    ARM64_PTE_VALID |
                    ARM64_PTE_AF |
                    ARM64_PTE_SH_INNER |
                    ARM64_PTE_ATTRIDX(device ? ARM64_ATTR_DEVICE
                                             : ARM64_ATTR_NORMAL);

    if (device) desc |= ARM64_PTE_PXN | ARM64_PTE_UXN;
    return desc;
}

static void arm64_identity_map_block_range(uint64_t start,
                                           uint64_t end,
                                           int device) {
    for (uint64_t addr = start; addr < end; addr += ARM64_L2_BLOCK_SIZE) {
        struct page_table *l1 = arm64_get_next_table(&kernel_root,
                                                     PML4_INDEX(addr),
                                                     1);
        struct page_table *l2 = arm64_get_next_table(l1,
                                                     PDPT_INDEX(addr),
                                                     1);
        if (!l2) return;
        l2->entries[PD_INDEX(addr)] = arm64_make_block_desc(addr, device);
    }
}

static void arm64_enable_mmu(uint64_t root_phys) {
    uint64_t mair = 0x00ULL | (0xFFULL << 8);
    uint64_t tcr = (16ULL << 0) |
                   (1ULL << 8) |
                   (1ULL << 10) |
                   (3ULL << 12) |
                   (16ULL << 16) |
                   (1ULL << 24) |
                   (1ULL << 26) |
                   (3ULL << 28) |
                   (2ULL << 30);
    uint64_t sctlr = 0;

    __asm__ volatile("dsb sy\n\tisb" ::: "memory");
    __asm__ volatile("msr mair_el1, %0" :: "r"(mair) : "memory");
    __asm__ volatile("msr tcr_el1, %0" :: "r"(tcr) : "memory");
    __asm__ volatile("msr ttbr0_el1, %0" :: "r"(root_phys) : "memory");
    __asm__ volatile("msr ttbr1_el1, %0" :: "r"(root_phys) : "memory");
    __asm__ volatile("isb" ::: "memory");
    __asm__ volatile("mrs %0, sctlr_el1" : "=r"(sctlr));
    sctlr |= (1ULL << 0);
    sctlr |= (1ULL << 2);
    sctlr |= (1ULL << 12);
    __asm__ volatile("msr sctlr_el1, %0\n\tisb" :: "r"(sctlr) : "memory");
    paging_mmu_enabled = 1;
}

void paging_init(uint64_t reserved_phys_end) {
    (void)reserved_phys_end;

    for (size_t i = 0; i < PAGE_ENTRIES; i++) {
        kernel_root.entries[i] = 0;
    }

    page_table_pool_used = 0;
    active_root = &kernel_root;
    current_ttbr0 = (uint64_t)(uintptr_t)&kernel_root;

    arm64_identity_map_block_range(0x40000000ULL, USER_VIRTUAL_BASE, 0);
    arm64_identity_map_block_range(USER_STACK_TOP, 0x80000000ULL, 0);
    arm64_identity_map_block_range(NUMOS_ARM64_QEMU_VIRT_UART0_BASE,
                                   NUMOS_ARM64_QEMU_VIRT_UART0_BASE + ARM64_L2_BLOCK_SIZE,
                                   1);
    arm64_enable_mmu(current_ttbr0);
}

void paging_flush_page(uint64_t virtual_addr) {
    __asm__ volatile("tlbi vaae1is, %0\n\tdsb ish\n\tisb"
                     :
                     : "r"(virtual_addr >> 12)
                     : "memory");
    paging_stats_data.tlb_flushes++;
}

int paging_map_page(uint64_t virtual_addr, uint64_t physical_addr,
                    uint64_t flags) {
    struct page_table *l1 = arm64_get_next_table(active_root,
                                                 PML4_INDEX(virtual_addr),
                                                 1);
    struct page_table *l2 = arm64_get_next_table(l1,
                                                 PDPT_INDEX(virtual_addr),
                                                 1);
    if (!l2) return -1;
    if ((l2->entries[PD_INDEX(virtual_addr)] & ARM64_PTE_VALID) &&
        !(l2->entries[PD_INDEX(virtual_addr)] & ARM64_PTE_TABLE)) {
        paging_stats_data.pages_mapped++;
        return 0;
    }
    struct page_table *l3 = arm64_get_next_table(l2,
                                                 PD_INDEX(virtual_addr),
                                                 1);
    if (!l3) return -1;

    l3->entries[PT_INDEX(virtual_addr)] =
        arm64_make_page_desc(physical_addr, flags | PAGE_PRESENT);
    paging_flush_page(virtual_addr);
    paging_stats_data.pages_mapped++;
    return 0;
}

int paging_unmap_page(uint64_t virtual_addr) {
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (!entry || !(*entry & ARM64_PTE_VALID)) return -1;
    *entry = 0;
    paging_flush_page(virtual_addr);
    paging_stats_data.pages_unmapped++;
    return 0;
}

int paging_is_mapped(uint64_t virtual_addr) {
    struct page_table *l1 = arm64_get_next_table(active_root,
                                                 PML4_INDEX(virtual_addr),
                                                 0);
    struct page_table *l2 = arm64_get_next_table(l1,
                                                 PDPT_INDEX(virtual_addr),
                                                 0);
    if (l2 &&
        (l2->entries[PD_INDEX(virtual_addr)] & ARM64_PTE_VALID) &&
        !(l2->entries[PD_INDEX(virtual_addr)] & ARM64_PTE_TABLE)) {
        return 1;
    }
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    return entry && (*entry & ARM64_PTE_VALID);
}

uint64_t paging_get_physical_address(uint64_t virtual_addr) {
    struct page_table *l1 = arm64_get_next_table(active_root,
                                                 PML4_INDEX(virtual_addr),
                                                 0);
    struct page_table *l2 = arm64_get_next_table(l1,
                                                 PDPT_INDEX(virtual_addr),
                                                 0);
    if (l2 &&
        (l2->entries[PD_INDEX(virtual_addr)] & ARM64_PTE_VALID) &&
        !(l2->entries[PD_INDEX(virtual_addr)] & ARM64_PTE_TABLE)) {
        return (l2->entries[PD_INDEX(virtual_addr)] & 0x0000FFFFFFE00000ULL) |
               (virtual_addr & (ARM64_L2_BLOCK_SIZE - 1));
    }
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (!entry || !(*entry & ARM64_PTE_VALID)) return 0;
    return PAGE_ENTRY_ADDR(*entry) | PAGE_OFFSET(virtual_addr);
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
    struct page_table *l1 = arm64_get_next_table(active_root,
                                                 PML4_INDEX(virtual_addr),
                                                 create);
    struct page_table *l2 = arm64_get_next_table(l1,
                                                 PDPT_INDEX(virtual_addr),
                                                 create);
    return arm64_get_next_table(l2, PD_INDEX(virtual_addr), create);
}

page_entry_t *paging_get_page_entry(uint64_t virtual_addr, int create) {
    struct page_table *table = paging_get_page_table(virtual_addr, create);
    if (!table) return 0;
    return &table->entries[PT_INDEX(virtual_addr)];
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
    return current_ttbr0;
}

void paging_switch_to(uint64_t cr3) {
    active_root = (struct page_table *)(uintptr_t)cr3;
    current_ttbr0 = cr3;
    if (paging_mmu_enabled) {
        __asm__ volatile("msr ttbr0_el1, %0\n\tmsr ttbr1_el1, %0\n\tisb"
                         :
                         : "r"(cr3)
                         : "memory");
    }
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
