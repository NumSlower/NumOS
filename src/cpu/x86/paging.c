/*
 * paging.c - Virtual and physical memory management
 *
 * Implements:
 *   - 4-level page table walk/map/unmap (PML4 -> PDPT -> PD -> PT)
 *   - Physical memory manager (PMM): frame allocator
 *   - Virtual memory manager (VMM): region-based virtual address allocator
 *   - VM region tracking
 *   - Page fault handler
 *
 * Boot identity-maps the first 1 GB via 2 MB huge pages.
 * New mappings use single 4 KB pages allocated from the PMM.
 */

#include "cpu/paging.h"
#include "kernel/kernel.h"
#include "kernel/scheduler.h"
#include "drivers/graphices/vga.h"
#include "cpu/heap.h"

/* =========================================================================
 * Boot page tables (set up in boot.asm, used read/write here)
 * ======================================================================= */

extern uint8_t p4_table[];  /* PML4 */
extern uint8_t p3_table[];  /* PDPT */
extern uint8_t p2_table[];  /* PD   */

/* Active PML4 table (identity-mapped physical == virtual in low memory) */
static struct page_table *current_pml4 = (struct page_table *)p4_table;
static uint64_t kernel_cr3 = 0;
static uint64_t current_cr3 = 0;

extern char _kernel_start;
extern char _kernel_end;

/* =========================================================================
 * Physical memory manager state
 * ======================================================================= */

static struct page_frame *free_frames    = NULL;      /* freed-frame reuse list */
static uint64_t           total_frames   = 0;         /* total frames in system  */
static uint64_t           used_frames    = 0;         /* frames currently in use */
static uint64_t           next_frame_addr = 0x200000; /* bump allocator cursor   */

/* Saved copy of the memory layout provided by the bootloader */
static struct physical_memory_info memory_info;

/* =========================================================================
 * Paging statistics
 * ======================================================================= */

static struct paging_stats paging_stats = {0};

/* =========================================================================
 * Virtual memory region list
 * ======================================================================= */

static struct vm_region *vm_regions = NULL;

/* =========================================================================
 * Internal helpers (not exposed in the header)
 * ======================================================================= */

/*
 * paging_map_page_advanced - map virtual_addr -> physical_addr with flags.
 * If overwrite == 0 and the page is already present, returns -1.
 */
static int paging_map_page_advanced(uint64_t virtual_addr,
                                    uint64_t physical_addr,
                                    uint64_t flags,
                                    int      overwrite) {
    virtual_addr  = paging_align_down(virtual_addr,  PAGE_SIZE);
    physical_addr = paging_align_down(physical_addr, PAGE_SIZE);

    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (entry && (*entry & PAGE_PRESENT) && !overwrite) {
        return -1;
    }

    entry = paging_get_page_entry(virtual_addr, 1);
    if (!entry) {
        paging_stats.allocation_failures++;
        return -1;
    }

    *entry = physical_addr | flags | PAGE_PRESENT;
    paging_flush_page(virtual_addr);
    paging_stats.pages_mapped++;
    return 0;
}

/*
 * paging_unmap_page_advanced - unmap virtual_addr and optionally free the frame.
 */
static int paging_unmap_page_advanced(uint64_t virtual_addr, int free_physical) {
    virtual_addr = paging_align_down(virtual_addr, PAGE_SIZE);

    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (!entry || !(*entry & PAGE_PRESENT)) return -1;

    uint64_t physical_addr = *entry & ~(uint64_t)0xFFF;
    *entry = 0;

    if (free_physical && physical_addr) {
        pmm_free_frame(physical_addr);
    }

    paging_flush_page(virtual_addr);
    paging_stats.pages_unmapped++;
    return 0;
}

/* =========================================================================
 * Public initialisation
 * ======================================================================= */

/*
 * paging_init - set up the PMM, VMM, and initial VM region descriptors.
 * Called once during kernel_init() before the heap is available.
 */
void paging_init(uint64_t reserved_phys_end) {
    struct physical_memory_info mem_info;

    uint64_t kernel_start = (uint64_t)(uintptr_t)&_kernel_start;
    uint64_t kernel_end   = (uint64_t)(uintptr_t)&_kernel_end;

    uint64_t bump = reserved_phys_end;
    if (bump < kernel_end) bump = kernel_end;
    if (bump < 0x200000)   bump = 0x200000;
    bump = paging_align_up(bump, PAGE_SIZE);

    mem_info.total_memory     = 512UL * 1024 * 1024;  /* 512 MB assumed */
    mem_info.available_memory = 512UL * 1024 * 1024;
    mem_info.kernel_start     = kernel_start;
    mem_info.kernel_end       = bump;

    pmm_init(&mem_info);
    next_frame_addr = bump;

    vmm_init();

    vga_writestring("PMM: Next frame at 0x");
    print_hex(next_frame_addr);
    vga_writestring("\n");

    /* Register the kernel text/data region */
    paging_create_vm_region(KERNEL_VIRTUAL_BASE,
                            KERNEL_VIRTUAL_BASE + 0x400000,
                            PAGE_PRESENT | PAGE_WRITABLE);

    /* Register the kernel heap region */
    paging_create_vm_region(KERNEL_HEAP_START,
                            KERNEL_HEAP_START + (16UL * 1024 * 1024),
                            PAGE_PRESENT | PAGE_WRITABLE);

    kernel_cr3 = (uint64_t)(uintptr_t)p4_table;
    current_cr3 = kernel_cr3;
    current_pml4 = (struct page_table *)(uintptr_t)kernel_cr3;

    vga_writestring("Enhanced paging system initialized\n");
}

uint64_t paging_get_kernel_cr3(void) {
    return kernel_cr3;
}

uint64_t paging_get_current_cr3(void) {
    return current_cr3;
}

struct page_table *paging_get_active_pml4(void) {
    return current_pml4;
}

void paging_set_active_pml4(struct page_table *pml4) {
    if (pml4) current_pml4 = pml4;
}

void paging_switch_to(uint64_t cr3) {
    if (!cr3) return;
    current_cr3 = cr3;
    current_pml4 = (struct page_table *)(uintptr_t)cr3;
    __asm__ volatile("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

uint64_t paging_create_user_pml4(void) {
    uint64_t pml4_phys = pmm_alloc_frame();
    if (!pml4_phys) return 0;
    memset((void *)(uintptr_t)pml4_phys, 0, PAGE_SIZE);

    uint64_t pdpt_phys = pmm_alloc_frame();
    if (!pdpt_phys) {
        pmm_free_frame(pml4_phys);
        return 0;
    }
    memset((void *)(uintptr_t)pdpt_phys, 0, PAGE_SIZE);

    struct page_table *new_pml4 = (struct page_table *)(uintptr_t)pml4_phys;
    struct page_table *new_pdpt = (struct page_table *)(uintptr_t)pdpt_phys;
    struct page_table *kernel_pml4 = (struct page_table *)(uintptr_t)kernel_cr3;

    for (int i = 256; i < PAGE_ENTRIES; i++) {
        new_pml4->entries[i] = kernel_pml4->entries[i];
    }

    new_pml4->entries[0] = pdpt_phys | PAGE_PRESENT | PAGE_WRITABLE;
    if (kernel_pml4->entries[0] & PAGE_PRESENT) {
        struct page_table *kernel_pdpt =
            (struct page_table *)(uintptr_t)(kernel_pml4->entries[0] & ~(uint64_t)0xFFF);
        for (int i = 0; i < PAGE_ENTRIES; i++) {
            new_pdpt->entries[i] = kernel_pdpt->entries[i];
        }
    }

    return pml4_phys;
}

/* =========================================================================
 * VM region management
 * ======================================================================= */

/*
 * paging_create_vm_region - register a virtual address range with given flags.
 * Used by the page fault handler to decide whether to satisfy a fault.
 */
int paging_create_vm_region(uint64_t start, uint64_t end, uint64_t flags) {
    struct vm_region *region =
        (struct vm_region *)kmalloc(sizeof(struct vm_region));
    if (!region) return -1;

    region->start  = paging_align_down(start, PAGE_SIZE);
    region->end    = paging_align_up(end,   PAGE_SIZE);
    region->flags  = flags;
    region->next   = vm_regions;
    vm_regions     = region;
    return 0;
}

/*
 * paging_find_vm_region - return the region containing addr, or NULL.
 */
struct vm_region *paging_find_vm_region(uint64_t addr) {
    struct vm_region *region = vm_regions;
    while (region) {
        if (addr >= region->start && addr < region->end) return region;
        region = region->next;
    }
    return NULL;
}

/* =========================================================================
 * Page mapping / unmapping
 * ======================================================================= */

/*
 * paging_map_page - map virtual_addr -> physical_addr with flags.
 * Will not overwrite an existing present mapping.
 */
int paging_map_page(uint64_t virtual_addr,
                    uint64_t physical_addr,
                    uint64_t flags) {
    return paging_map_page_advanced(virtual_addr, physical_addr, flags, 0);
}

/*
 * paging_unmap_page - unmap virtual_addr and free its physical frame.
 */
int paging_unmap_page(uint64_t virtual_addr) {
    return paging_unmap_page_advanced(virtual_addr, 1);
}

/*
 * paging_is_mapped - return 1 if virtual_addr has a present mapping, 0 if not.
 */
int paging_is_mapped(uint64_t virtual_addr) {
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    return (entry && (*entry & PAGE_PRESENT)) ? 1 : 0;
}

/*
 * paging_get_physical_address - walk the page tables to find the physical
 * address backing virtual_addr.  Returns 0 if not mapped.
 */
uint64_t paging_get_physical_address(uint64_t virtual_addr) {
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virtual_addr);
    uint64_t pd_idx   = PD_INDEX(virtual_addr);
    uint64_t pt_idx   = PT_INDEX(virtual_addr);
    uint64_t offset   = PAGE_OFFSET(virtual_addr);

    struct page_table *pml4 = current_pml4;
    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT)) return 0;

    struct page_table *pdpt =
        (struct page_table *)(pml4->entries[pml4_idx] & ~(uint64_t)0xFFF);
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) return 0;

    struct page_table *pd =
        (struct page_table *)(pdpt->entries[pdpt_idx] & ~(uint64_t)0xFFF);
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) return 0;

    /* 2 MB huge page: offset spans 21 bits */
    if (pd->entries[pd_idx] & PAGE_HUGE) {
        uint64_t huge_offset = virtual_addr & 0x1FFFFF;
        return (pd->entries[pd_idx] & ~(uint64_t)0x1FFFFF) + huge_offset;
    }

    struct page_table *pt =
        (struct page_table *)(pd->entries[pd_idx] & ~(uint64_t)0xFFF);
    if (!(pt->entries[pt_idx] & PAGE_PRESENT)) return 0;

    return (pt->entries[pt_idx] & ~(uint64_t)0xFFF) + offset;
}

/* =========================================================================
 * TLB management
 * ======================================================================= */

/*
 * paging_flush_page - invalidate the single TLB entry for virtual_addr.
 */
void paging_flush_page(uint64_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

/* =========================================================================
 * Page table walk / allocation
 * ======================================================================= */

/*
 * paging_get_page_table - return the PT for virtual_addr.
 * If create != 0, missing intermediate tables are allocated from the PMM.
 * Returns NULL if the mapping does not exist and create == 0.
 */
struct page_table *paging_get_page_table(uint64_t virtual_addr, int create) {
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virtual_addr);
    uint64_t pd_idx   = PD_INDEX(virtual_addr);

    /* User-space mappings need the PAGE_USER bit set on all levels */
    int user_mapping = (virtual_addr >= USER_VIRTUAL_BASE &&
                        virtual_addr <  USER_STACK_TOP) ? 1 : 0;

    struct page_table *pml4 = current_pml4;

    /* PML4 -> PDPT */
    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return NULL;
        uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (user_mapping) flags |= PAGE_USER;
        pml4->entries[pml4_idx] = phys | flags;
        memset((void *)phys, 0, sizeof(struct page_table));
    } else if (user_mapping) {
        pml4->entries[pml4_idx] |= PAGE_USER;
    }

    struct page_table *pdpt =
        (struct page_table *)(pml4->entries[pml4_idx] & ~(uint64_t)0xFFF);
    if (!pdpt) return NULL;

    /* PDPT -> PD */
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return NULL;
        uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (user_mapping) flags |= PAGE_USER;
        pdpt->entries[pdpt_idx] = phys | flags;
        memset((void *)phys, 0, sizeof(struct page_table));
    } else if (user_mapping) {
        pdpt->entries[pdpt_idx] |= PAGE_USER;
    }

    struct page_table *pd =
        (struct page_table *)(pdpt->entries[pdpt_idx] & ~(uint64_t)0xFFF);
    if (!pd) return NULL;

    /* PD -> PT */
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) {
        if (!create) return NULL;
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return NULL;
        uint64_t flags = PAGE_PRESENT | PAGE_WRITABLE;
        if (user_mapping) flags |= PAGE_USER;
        pd->entries[pd_idx] = phys | flags;
        memset((void *)phys, 0, sizeof(struct page_table));
    } else if (user_mapping) {
        pd->entries[pd_idx] |= PAGE_USER;
    }

    return (struct page_table *)(pd->entries[pd_idx] & ~(uint64_t)0xFFF);
}

/*
 * paging_get_page_entry - return a pointer to the PTE for virtual_addr.
 * If create != 0, missing intermediate tables are allocated.
 */
page_entry_t *paging_get_page_entry(uint64_t virtual_addr, int create) {
    struct page_table *pt = paging_get_page_table(virtual_addr, create);
    if (!pt) return NULL;
    return &pt->entries[PT_INDEX(virtual_addr)];
}

/* =========================================================================
 * Physical memory manager
 * ======================================================================= */

/*
 * pmm_init - initialise the PMM from a memory layout descriptor.
 * Stores the layout for bounds-checking in pmm_alloc_frame.
 */
void pmm_init(struct physical_memory_info *mem_info) {
    /* Save the full layout so pmm_alloc_frame can check total_memory */
    memory_info = *mem_info;

    total_frames = mem_info->available_memory / PAGE_SIZE;
    used_frames  = 0;
    free_frames  = NULL;

    /* Mark kernel frames as already used */
    uint64_t kernel_frames = (mem_info->kernel_end - mem_info->kernel_start
                              + PAGE_SIZE - 1) / PAGE_SIZE;
    used_frames = kernel_frames;

    vga_writestring("Physical Memory Manager initialized\n");
}

/*
 * pmm_alloc_frame - return the physical address of one free 4 KB frame.
 * First tries the freed-frame reuse list, then the bump allocator.
 * Returns 0 on failure.
 */
uint64_t pmm_alloc_frame(void) {
    if (free_frames) {
        struct page_frame *frame = free_frames;
        free_frames = frame->next;
        uint64_t addr = frame->address;
        used_frames++;
        return addr;
    }

    if (next_frame_addr + PAGE_SIZE > memory_info.total_memory) {
        return 0;  /* out of physical memory */
    }

    uint64_t addr = next_frame_addr;
    next_frame_addr += PAGE_SIZE;
    used_frames++;
    return addr;
}

/*
 * pmm_free_frame - mark a physical frame as available for reuse.
 * Note: the current implementation only decrements the counter; the
 * freed-frame list reuse path requires a slab or buddy allocator to
 * store page_frame structs themselves.
 */
void pmm_free_frame(uint64_t frame_addr) {
    (void)frame_addr;  /* suppressed until slab allocator is available */
    if (used_frames > 0) {
        used_frames--;
    }
}

void pmm_get_stats(struct pmm_stats *out) {
    if (!out) return;
    out->total_memory     = memory_info.total_memory;
    out->available_memory = memory_info.available_memory;
    out->total_frames     = total_frames;
    out->used_frames      = used_frames;
    out->free_frames      = (total_frames >= used_frames)
                            ? (total_frames - used_frames)
                            : 0;
}

/* =========================================================================
 * Virtual memory manager
 * ======================================================================= */

/* Next available virtual address for vmm_alloc_pages */
static uint64_t next_virtual = KERNEL_HEAP_START;

/*
 * vmm_init - initialise the virtual memory manager.
 * Currently a no-op beyond the log message; state is in next_virtual.
 */
void vmm_init(void) {
    vga_writestring("Virtual Memory Manager initialized\n");
}

/*
 * vmm_alloc_pages - allocate num_pages virtual pages backed by fresh frames.
 * Maps them with the given flags. Rolls back on any failure.
 */
void *vmm_alloc_pages(size_t num_pages, uint64_t flags) {
    uint64_t virtual_start = next_virtual;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t physical = pmm_alloc_frame();
        if (!physical) {
            /* Roll back successfully mapped pages */
            for (size_t j = 0; j < i; j++) {
                uint64_t v = virtual_start + j * PAGE_SIZE;
                uint64_t p = paging_get_physical_address(v);
                if (p) pmm_free_frame(p);
                paging_unmap_page(v);
            }
            return NULL;
        }

        if (paging_map_page(virtual_start + i * PAGE_SIZE, physical, flags) != 0) {
            pmm_free_frame(physical);
            for (size_t j = 0; j < i; j++) {
                uint64_t v = virtual_start + j * PAGE_SIZE;
                uint64_t p = paging_get_physical_address(v);
                if (p) pmm_free_frame(p);
                paging_unmap_page(v);
            }
            return NULL;
        }
    }

    next_virtual += num_pages * PAGE_SIZE;
    return (void *)virtual_start;
}

/*
 * vmm_free_pages - unmap num_pages pages starting at virtual_addr and
 * return the backing physical frames to the PMM.
 */
void vmm_free_pages(void *virtual_addr, size_t num_pages) {
    uint64_t addr = (uint64_t)virtual_addr;

    for (size_t i = 0; i < num_pages; i++) {
        uint64_t physical = paging_get_physical_address(addr + i * PAGE_SIZE);
        if (physical) {
            pmm_free_frame(physical);
            paging_unmap_page(addr + i * PAGE_SIZE);
        }
    }
}

void paging_get_stats(struct paging_stats *out) {
    if (!out) return;
    *out = paging_stats;
}

/* =========================================================================
 * Utility functions
 * ======================================================================= */

uint64_t paging_align_up(uint64_t addr, uint64_t alignment) {
    return (addr + alignment - 1) & ~(alignment - 1);
}

uint64_t paging_align_down(uint64_t addr, uint64_t alignment) {
    return addr & ~(alignment - 1);
}

/* =========================================================================
 * Page fault handler
 * ======================================================================= */

/*
 * page_fault_handler - called from the IDT exception handler for vector 14.
 * Attempts demand-paging if the faulting address is inside a known VM region.
 * User stack growth also stays active during syscalls, because the kernel may
 * touch a user buffer before that stack page has been committed.
 * Halts the kernel for unhandled faults.
 */
void page_fault_handler(uint64_t error_code, uint64_t fault_addr) {
    paging_stats.page_faults++;

    if (!(error_code & 1) &&
        scheduler_handle_user_page_fault(fault_addr)) {
        return;
    }

    struct vm_region *region = paging_find_vm_region(fault_addr);

    /* Try demand allocation: page not present inside a valid region */
    if (region && !(error_code & 1)) {
        uint64_t physical = pmm_alloc_frame();
        if (physical) {
            uint64_t page_addr = paging_align_down(fault_addr, PAGE_SIZE);
            if (paging_map_page_advanced(page_addr, physical,
                                         region->flags, 0) == 0) {
                return;  /* fault satisfied */
            }
            pmm_free_frame(physical);
        }
    }

    /* Unhandled page fault: display diagnostics and halt */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\nPAGE FAULT!\n");
    vga_writestring("Fault Address: 0x"); print_hex(fault_addr); vga_writestring("\n");
    vga_writestring("Error Code:    0x"); print_hex(error_code); vga_writestring("\n");

    vga_writestring(error_code & 1 ? "- Protection violation\n" : "- Page not present\n");
    vga_writestring(error_code & 2 ? "- Write\n"                : "- Read\n");
    vga_writestring(error_code & 4 ? "- User mode\n"            : "- Kernel mode\n");
    vga_writestring(region          ? "- In valid VM region\n"  : "- Outside VM regions\n");

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    hang();
}
