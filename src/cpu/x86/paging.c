#include "cpu/paging.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/heap.h"

/* Page table pointers from boot (now properly declared as extern) */
extern uint8_t p4_table[];
extern uint8_t p3_table[];
extern uint8_t p2_table[];

/* Current page directory - cast to page_table structure */
static struct page_table *current_pml4 = (struct page_table*)p4_table;

/* Physical memory manager */
static struct page_frame *free_frames = NULL;
static uint64_t total_frames = 0;
static uint64_t used_frames = 0;
static uint64_t next_frame_addr = 0x200000; // Start after 2MB (skip low memory)

/* Memory layout */
static struct physical_memory_info memory_info;

/* Enhanced paging statistics */
struct paging_stats {
    uint64_t page_faults;
    uint64_t pages_mapped;
    uint64_t pages_unmapped;
    uint64_t tlb_flushes;
    uint64_t allocation_failures;
};

static struct paging_stats paging_stats = {0};

static struct vm_region *vm_regions = NULL;

void paging_init(void) {
    // Set up memory info (simplified - in real OS you'd parse multiboot info)
    memory_info.total_memory = 128 * 1024 * 1024; // 128MB for now
    memory_info.available_memory = 120 * 1024 * 1024; // 120MB available
    memory_info.kernel_start = 0x100000; // 1MB
    memory_info.kernel_end = 0x400000;   // 4MB (estimated)
    
    // Initialize physical memory manager
    pmm_init(&memory_info);
    
    // Initialize virtual memory manager  
    vmm_init();
    
    // Create initial VM regions
    paging_create_vm_region(KERNEL_VIRTUAL_BASE, KERNEL_VIRTUAL_BASE + 0x400000, 
                           PAGE_PRESENT | PAGE_WRITABLE);
    paging_create_vm_region(KERNEL_HEAP_START, KERNEL_HEAP_START + (16 * 1024 * 1024),
                           PAGE_PRESENT | PAGE_WRITABLE);
    
    vga_writestring("Enhanced paging system initialized\n");
}

void paging_enable(void) {
    // Paging is already enabled by boot code, but we can verify
    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    
    if (!(cr0 & (1UL << 31))) {
        vga_writestring("Warning: Paging not enabled!\n");
    } else {
        vga_writestring("Paging is active\n");
    }
}

/* Enhanced virtual memory region management */
int paging_create_vm_region(uint64_t start, uint64_t end, uint64_t flags) {
    struct vm_region *region = (struct vm_region*)kmalloc(sizeof(struct vm_region));
    if (!region) {
        return -1;
    }
    
    region->start = paging_align_down(start, PAGE_SIZE);
    region->end = paging_align_up(end, PAGE_SIZE);
    region->flags = flags;
    region->next = vm_regions;
    vm_regions = region;
    
    return 0;
}

void paging_destroy_vm_region(uint64_t start, uint64_t end) {
    struct vm_region **current = &vm_regions;
    
    while (*current) {
        struct vm_region *region = *current;
        if (region->start == start && region->end == end) {
            *current = region->next;
            kfree(region);
            return;
        }
        current = &region->next;
    }
}

/* Find VM region containing the given address */
struct vm_region* paging_find_vm_region(uint64_t addr) {
    struct vm_region *region = vm_regions;
    
    while (region) {
        if (addr >= region->start && addr < region->end) {
            return region;
        }
        region = region->next;
    }
    
    return NULL; // Not found
}

/* Enhanced page mapping with better error handling */
int paging_map_page_advanced(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags, int overwrite) {
    // Align addresses
    virtual_addr = paging_align_down(virtual_addr, PAGE_SIZE);
    physical_addr = paging_align_down(physical_addr, PAGE_SIZE);
    
    // Check if page is already mapped
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (entry && (*entry & PAGE_PRESENT) && !overwrite) {
        vga_writestring("Warning: Page already mapped at ");
        print_hex(virtual_addr);
        vga_writestring("\n");
        return -1;
    }
    
    // Get or create page entry
    entry = paging_get_page_entry(virtual_addr, 1);
    if (!entry) {
        paging_stats.allocation_failures++;
        return -1; // Failed to get/create page entry
    }
    
    // Map the page
    *entry = physical_addr | flags | PAGE_PRESENT;
    
    // Flush TLB for this page
    paging_flush_page(virtual_addr);
    paging_stats.pages_mapped++;
    
    return 0;
}

int paging_unmap_page_advanced(uint64_t virtual_addr, int free_physical) {
    // Align address
    virtual_addr = paging_align_down(virtual_addr, PAGE_SIZE);
    
    // Get page entry
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (!entry || !(*entry & PAGE_PRESENT)) {
        return -1; // Not mapped
    }
    
    // Get physical address before unmapping
    uint64_t physical_addr = *entry & ~0xFFF;
    
    // Unmap the page
    *entry = 0;
    
    // Free physical frame if requested
    if (free_physical && physical_addr) {
        pmm_free_frame(physical_addr);
    }
    
    // Flush TLB for this page
    paging_flush_page(virtual_addr);
    paging_stats.pages_unmapped++;
    
    return 0;
}

/* Bulk operations */
int paging_map_range(uint64_t virtual_start, uint64_t physical_start, size_t pages, uint64_t flags) {
    for (size_t i = 0; i < pages; i++) {
        uint64_t virt = virtual_start + (i * PAGE_SIZE);
        uint64_t phys = physical_start + (i * PAGE_SIZE);
        
        if (paging_map_page_advanced(virt, phys, flags, 0) != 0) {
            // Cleanup already mapped pages
            for (size_t j = 0; j < i; j++) {
                paging_unmap_page_advanced(virtual_start + (j * PAGE_SIZE), 1);
            }
            return -1;
        }
    }
    return 0;
}

int paging_unmap_range(uint64_t virtual_start, size_t pages, int free_physical) {
    int success = 0;
    
    for (size_t i = 0; i < pages; i++) {
        uint64_t virt = virtual_start + (i * PAGE_SIZE);
        if (paging_unmap_page_advanced(virt, free_physical) == 0) {
            success++;
        }
    }
    
    return success;
}

/* Protection and attribute changes */
int paging_change_protection(uint64_t virtual_addr, uint64_t new_flags) {
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    if (!entry || !(*entry & PAGE_PRESENT)) {
        return -1; // Page not mapped
    }
    
    // Preserve physical address, update flags
    uint64_t physical = *entry & ~0xFFF;
    *entry = physical | new_flags | PAGE_PRESENT;
    
    paging_flush_page(virtual_addr);
    return 0;
}

int paging_is_mapped(uint64_t virtual_addr) {
    page_entry_t *entry = paging_get_page_entry(virtual_addr, 0);
    return (entry && (*entry & PAGE_PRESENT)) ? 1 : 0;
}

/* Memory information and debugging */
void paging_print_stats(void) {
    vga_writestring("Paging Statistics:\n");
    vga_writestring("  Page faults:        ");
    print_dec(paging_stats.page_faults);
    vga_writestring("\n  Pages mapped:       ");
    print_dec(paging_stats.pages_mapped);
    vga_writestring("\n  Pages unmapped:     ");
    print_dec(paging_stats.pages_unmapped);
    vga_writestring("\n  TLB flushes:        ");
    print_dec(paging_stats.tlb_flushes);
    vga_writestring("\n  Allocation failures: ");
    print_dec(paging_stats.allocation_failures);
    vga_writestring("\n");
}

void paging_print_vm_regions(void) {
    vga_writestring("Virtual Memory Regions:\n");
    struct vm_region *region = vm_regions;
    int count = 0;
    
    while (region) {
        vga_writestring("  Region ");
        print_dec(count++);
        vga_writestring(": ");
        print_hex(region->start);
        vga_writestring(" - ");
        print_hex(region->end);
        vga_writestring(" (flags: ");
        print_hex(region->flags);
        vga_writestring(")\n");
        region = region->next;
    }
}

int paging_validate_range(uint64_t virtual_start, size_t pages) {
    for (size_t i = 0; i < pages; i++) {
        uint64_t addr = virtual_start + (i * PAGE_SIZE);
        if (!paging_is_mapped(addr)) {
            vga_writestring("Invalid page at ");
            print_hex(addr);
            vga_writestring("\n");
            return 0;
        }
    }
    return 1;
}

uint64_t paging_get_physical_address(uint64_t virtual_addr) {
    // Get page table indices
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virtual_addr);
    uint64_t pd_idx = PD_INDEX(virtual_addr);
    uint64_t pt_idx = PT_INDEX(virtual_addr);
    uint64_t offset = PAGE_OFFSET(virtual_addr);
    
    // Walk page tables
    struct page_table *pml4 = current_pml4;
    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT)) {
        return 0; // Not mapped
    }
    
    // Convert physical address to virtual for accessing page tables
    // In our setup, we have identity mapping for low memory
    struct page_table *pdpt = (struct page_table*)(pml4->entries[pml4_idx] & ~0xFFF);
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) {
        return 0; // Not mapped
    }
    
    struct page_table *pd = (struct page_table*)(pdpt->entries[pdpt_idx] & ~0xFFF);
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) {
        return 0; // Not mapped
    }
    
    // Check if it's a huge page (2MB)
    if (pd->entries[pd_idx] & PAGE_HUGE) {
        uint64_t huge_offset = (virtual_addr & 0x1FFFFF); // 2MB offset
        return (pd->entries[pd_idx] & ~0x1FFFFF) + huge_offset;
    }
    
    struct page_table *pt = (struct page_table*)(pd->entries[pd_idx] & ~0xFFF);
    if (!(pt->entries[pt_idx] & PAGE_PRESENT)) {
        return 0; // Not mapped
    }
    
    return (pt->entries[pt_idx] & ~0xFFF) + offset;
}

int paging_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags) {
    return paging_map_page_advanced(virtual_addr, physical_addr, flags, 0);
}

int paging_unmap_page(uint64_t virtual_addr) {
    return paging_unmap_page_advanced(virtual_addr, 1);
}

void paging_flush_tlb(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3) : "memory");
    paging_stats.tlb_flushes++;
}

void paging_flush_page(uint64_t virtual_addr) {
    __asm__ volatile("invlpg (%0)" : : "r"(virtual_addr) : "memory");
}

/* Mark a virtual range as user-accessible.
 * Adds PAGE_USER to PD/PT entries where present. Handles 2MB huge pages
 * by setting the bit in the PD entry and advances by LARGE_PAGE_SIZE.
 */
int paging_set_user_range(uint64_t start, uint64_t size) {
    uint64_t addr = paging_align_down(start, PAGE_SIZE);
    uint64_t end = paging_align_up(start + size, PAGE_SIZE);

    while (addr < end) {
        uint64_t pml4_idx = PML4_INDEX(addr);
        uint64_t pdpt_idx = PDPT_INDEX(addr);
        uint64_t pd_idx = PD_INDEX(addr);

        struct page_table *pml4 = current_pml4;
        if (!(pml4->entries[pml4_idx] & PAGE_PRESENT)) {
            addr += PAGE_SIZE;
            continue;
        }

        struct page_table *pdpt = (struct page_table*)(pml4->entries[pml4_idx] & ~0xFFF);
        if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) {
            addr += PAGE_SIZE;
            continue;
        }

        struct page_table *pd = (struct page_table*)(pdpt->entries[pdpt_idx] & ~0xFFF);
        if (!(pd->entries[pd_idx] & PAGE_PRESENT)) {
            addr += PAGE_SIZE;
            continue;
        }

        /* If PD entry is a huge (2MB) page, set USER bit on PD entry */
        if (pd->entries[pd_idx] & PAGE_HUGE) {
            pd->entries[pd_idx] |= PAGE_USER;
            paging_flush_page(addr);
            addr += LARGE_PAGE_SIZE;
            continue;
        }

        /* Otherwise operate on PT entry */
        struct page_table *pt = (struct page_table*)(pd->entries[pd_idx] & ~0xFFF);
        uint64_t pt_idx = PT_INDEX(addr);
        if (pt->entries[pt_idx] & PAGE_PRESENT) {
            pt->entries[pt_idx] |= PAGE_USER;
            paging_flush_page(addr);
        }

        addr += PAGE_SIZE;
    }

    return 0;
}

/* Physical Memory Manager */
void pmm_init(struct physical_memory_info *mem_info) {
    total_frames = mem_info->available_memory / PAGE_SIZE;
    used_frames = 0;
    free_frames = NULL;
    
    // Mark kernel frames as used
    uint64_t kernel_frames = (mem_info->kernel_end - mem_info->kernel_start + PAGE_SIZE - 1) / PAGE_SIZE;
    used_frames = kernel_frames;
    
    vga_writestring("Physical Memory Manager initialized\n");
}

uint64_t pmm_alloc_frame(void) {
    if (free_frames) {
        // Use freed frame
        struct page_frame *frame = free_frames;
        free_frames = frame->next;
        uint64_t addr = frame->address;
        // Note: In a real implementation, we'd have a proper allocator for page_frame structs
        used_frames++;
        return addr;
    } else {
        // Allocate new frame
        if (next_frame_addr + PAGE_SIZE > memory_info.total_memory) {
            return 0; // Out of memory
        }
        
        uint64_t addr = next_frame_addr;
        next_frame_addr += PAGE_SIZE;
        used_frames++;
        return addr;
    }
}

void pmm_free_frame(uint64_t frame_addr) {
    // In a real implementation, we'd add this frame back to the free list
    // For now, just decrement the used counter
    (void)frame_addr; // Suppress unused parameter warning
    if (used_frames > 0) {
        used_frames--;
    }
}

uint64_t pmm_get_total_frames(void) {
    return total_frames;
}

uint64_t pmm_get_free_frames(void) {
    return total_frames - used_frames;
}

uint64_t pmm_get_used_frames(void) {
    return used_frames;
}

/* Virtual Memory Manager */
void vmm_init(void) {
    vga_writestring("Virtual Memory Manager initialized\n");
}

void* vmm_alloc_pages(size_t num_pages, uint64_t flags) {
    // Simple implementation - in real OS this would be more sophisticated
    static uint64_t next_virtual = KERNEL_HEAP_START;
    
    uint64_t virtual_start = next_virtual;
    
    for (size_t i = 0; i < num_pages; i++) {
        uint64_t physical = pmm_alloc_frame();
        if (!physical) {
            // Cleanup already allocated pages
            for (size_t j = 0; j < i; j++) {
                uint64_t cleanup_addr = virtual_start + j * PAGE_SIZE;
                uint64_t cleanup_phys = paging_get_physical_address(cleanup_addr);
                if (cleanup_phys) {
                    pmm_free_frame(cleanup_phys);
                }
                paging_unmap_page(cleanup_addr);
            }
            return NULL;
        }
        
        if (paging_map_page(virtual_start + i * PAGE_SIZE, physical, flags) != 0) {
            // Cleanup
            pmm_free_frame(physical);
            for (size_t j = 0; j < i; j++) {
                uint64_t cleanup_addr = virtual_start + j * PAGE_SIZE;
                uint64_t cleanup_phys = paging_get_physical_address(cleanup_addr);
                if (cleanup_phys) {
                    pmm_free_frame(cleanup_phys);
                }
                paging_unmap_page(cleanup_addr);
            }
            return NULL;
        }
    }
    
    next_virtual += num_pages * PAGE_SIZE;
    return (void*)virtual_start;
}

void vmm_free_pages(void* virtual_addr, size_t num_pages) {
    uint64_t addr = (uint64_t)virtual_addr;
    
    for (size_t i = 0; i < num_pages; i++) {
        uint64_t physical = paging_get_physical_address(addr + i * PAGE_SIZE);
        if (physical) {
            pmm_free_frame(physical);
            paging_unmap_page(addr + i * PAGE_SIZE);
        }
    }
}

/* Page Table Management */
struct page_table* paging_get_page_table(uint64_t virtual_addr, int create) {
    uint64_t pml4_idx = PML4_INDEX(virtual_addr);
    uint64_t pdpt_idx = PDPT_INDEX(virtual_addr);
    uint64_t pd_idx = PD_INDEX(virtual_addr);
    
    struct page_table *pml4 = current_pml4;
    
    // Get or create PDPT
    if (!(pml4->entries[pml4_idx] & PAGE_PRESENT)) {
        if (!create) return NULL;
        
        uint64_t physical = pmm_alloc_frame();
        if (!physical) return NULL;
        
        pml4->entries[pml4_idx] = physical | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Clear the new table
        struct page_table *new_table = (struct page_table*)physical;
        memset(new_table, 0, sizeof(struct page_table));
    }
    
    struct page_table *pdpt = (struct page_table*)(pml4->entries[pml4_idx] & ~0xFFF);
    
    // Get or create PD
    if (!(pdpt->entries[pdpt_idx] & PAGE_PRESENT)) {
        if (!create) return NULL;
        
        uint64_t physical = pmm_alloc_frame();
        if (!physical) return NULL;
        
        pdpt->entries[pdpt_idx] = physical | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Clear the new table
        struct page_table *new_table = (struct page_table*)physical;
        memset(new_table, 0, sizeof(struct page_table));
    }
    
    struct page_table *pd = (struct page_table*)(pdpt->entries[pdpt_idx] & ~0xFFF);
    
    // Get or create PT
    if (!(pd->entries[pd_idx] & PAGE_PRESENT)) {
        if (!create) return NULL;
        
        uint64_t physical = pmm_alloc_frame();
        if (!physical) return NULL;
        
        pd->entries[pd_idx] = physical | PAGE_PRESENT | PAGE_WRITABLE;
        
        // Clear the new table
        struct page_table *new_table = (struct page_table*)physical;
        memset(new_table, 0, sizeof(struct page_table));
    }
    
    return (struct page_table*)(pd->entries[pd_idx] & ~0xFFF);
}

page_entry_t* paging_get_page_entry(uint64_t virtual_addr, int create) {
    struct page_table *pt = paging_get_page_table(virtual_addr, create);
    if (!pt) return NULL;
    
    uint64_t pt_idx = PT_INDEX(virtual_addr);
    return &pt->entries[pt_idx];
}

struct page_table* paging_create_page_table(void) {
    uint64_t physical = pmm_alloc_frame();
    if (!physical) return NULL;
    
    struct page_table *table = (struct page_table*)physical;
    memset(table, 0, sizeof(struct page_table));
    
    return table;
}

void paging_destroy_page_table(struct page_table* table) {
    if (table) {
        pmm_free_frame((uint64_t)table);
    }
}

/* Utility Functions */
uint64_t paging_align_up(uint64_t addr, uint64_t alignment) {
    return (addr + alignment - 1) & ~(alignment - 1);
}

uint64_t paging_align_down(uint64_t addr, uint64_t alignment) {
    return addr & ~(alignment - 1);
}

int paging_is_aligned(uint64_t addr, uint64_t alignment) {
    return (addr & (alignment - 1)) == 0;
}

void page_fault_handler(uint64_t error_code, uint64_t fault_addr) {
    paging_stats.page_faults++;
    
    // Check if it's in a valid VM region
    struct vm_region *region = paging_find_vm_region(fault_addr);
    
    if (region) {
        // Try to handle the fault by allocating a new page
        if (!(error_code & 1)) { // Page not present
            uint64_t physical = pmm_alloc_frame();
            if (physical) {
                uint64_t page_addr = paging_align_down(fault_addr, PAGE_SIZE);
                if (paging_map_page_advanced(page_addr, physical, region->flags, 0) == 0) {
                    vga_writestring("Page fault handled: allocated page at ");
                    print_hex(page_addr);
                    vga_writestring("\n");
                    return; // Successfully handled
                }
                pmm_free_frame(physical); // Free if mapping failed
            }
        }
    }
    
    // Unhandled page fault - display error
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\nPAGE FAULT!\n");
    
    vga_writestring("Fault Address: 0x");
    print_hex(fault_addr);
    vga_writestring("\nError Code: 0x");
    print_hex(error_code);
    vga_writestring("\n");
    
    if (error_code & 1) {
        vga_writestring("- Page protection violation\n");
    } else {
        vga_writestring("- Page not present\n");
    }
    
    if (error_code & 2) {
        vga_writestring("- Write operation\n");
    } else {
        vga_writestring("- Read operation\n");
    }
    
    if (error_code & 4) {
        vga_writestring("- User mode access\n");
    } else {
        vga_writestring("- Kernel mode access\n");
    }
    
    if (region) {
        vga_writestring("- Within valid VM region\n");
    } else {
        vga_writestring("- Outside valid VM regions\n");
    }
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("\nSystem halted due to unhandled page fault.\n");
    
    hang();
}