#ifndef PAGING_H
#define PAGING_H

#include "lib/base.h"

/* Page Size Constants */
#define PAGE_SIZE           4096                   /* Standard page size (4KB) */
#define LARGE_PAGE_SIZE     (2 * 1024 * 1024)    /* Large page size (2MB) */
#define PAGE_ENTRIES        512                    /* Entries per page table */

/* Page Table Entry Flags (64-bit mode) */
#define PAGE_PRESENT        0x001                  /* Page is present in memory */
#define PAGE_WRITABLE       0x002                  /* Page is writable */
#define PAGE_USER           0x004                  /* Page is user-accessible */
#define PAGE_WRITETHROUGH   0x008                  /* Page uses write-through caching */
#define PAGE_CACHE_DISABLE  0x010                  /* Page caching is disabled */
#define PAGE_ACCESSED       0x020                  /* Page has been accessed */
#define PAGE_DIRTY          0x040                  /* Page has been written to */
#define PAGE_HUGE           0x080                  /* Large/huge page (2MB/1GB) */
#define PAGE_GLOBAL         0x100                  /* Global page (not flushed on CR3 reload) */
#define PAGE_NX             0x8000000000000000UL   /* No-execute bit (bit 63) */

/* Virtual Memory Layout Constants (Canonical addresses for 64-bit) */
#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000UL   /* -2GB (canonical high) */
#define USER_VIRTUAL_BASE   0x0000000000400000UL   /* 4MB (user space start) */
#define KERNEL_HEAP_START   0xFFFFFFFF90000000UL   /* Kernel heap start */
#define USER_STACK_TOP      0x0000000000800000UL   /* 8MB (user stack) */

/* Page Table Entry Type */
typedef uint64_t page_entry_t;

/* Page Table Structure (4KB aligned, 512 entries of 8 bytes = 4KB) */
struct page_table {
    page_entry_t entries[PAGE_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

/* Physical Memory Information */
struct physical_memory_info {
    uint64_t total_memory;         /* Total physical memory */
    uint64_t available_memory;     /* Available physical memory */
    uint64_t kernel_start;         /* Kernel start address */
    uint64_t kernel_end;           /* Kernel end address */
};

/* Page Frame Status Flags */
#define FRAME_FREE          0x00                   /* Frame is available */
#define FRAME_USED          0x01                   /* Frame is in use */
#define FRAME_KERNEL        0x02                   /* Frame reserved for kernel */
#define FRAME_RESERVED      0x04                   /* Frame is reserved (hardware) */

/* Page Frame Structure */
struct page_frame {
    uint64_t address;              /* Physical frame address */
    uint8_t flags;                 /* Frame status flags */
    uint32_t ref_count;            /* Reference counter */
    struct page_frame *next;       /* Next frame in list */
};

/* Virtual Memory Region */
struct vm_region {
    uint64_t start;                /* Region start address */
    uint64_t end;                  /* Region end address */
    uint64_t flags;                /* Region access flags */
    struct vm_region *next;        /* Next region in list */
};

/* Core Paging Functions */
void paging_init(void);
void paging_enable(void);
void paging_flush_tlb(void);
void paging_flush_page(uint64_t virtual_addr);

/* Page Mapping Functions */
int paging_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
int paging_unmap_page(uint64_t virtual_addr);
int paging_map_range(uint64_t virtual_start, uint64_t physical_start, size_t pages, uint64_t flags);
int paging_unmap_range(uint64_t virtual_start, size_t pages, int free_physical);
int paging_change_protection(uint64_t virtual_addr, uint64_t new_flags);
int paging_is_mapped(uint64_t virtual_addr);
uint64_t paging_get_physical_address(uint64_t virtual_addr);

/* Virtual Memory Region Management */
int paging_create_vm_region(uint64_t start, uint64_t end, uint64_t flags);
void paging_destroy_vm_region(uint64_t start, uint64_t end);
struct vm_region* paging_find_vm_region(uint64_t addr);

/* Page Table Management */
struct page_table* paging_get_page_table(uint64_t virtual_addr, int create);
page_entry_t* paging_get_page_entry(uint64_t virtual_addr, int create);
struct page_table* paging_create_page_table(void);
void paging_destroy_page_table(struct page_table* table);

/* Physical Memory Manager */
void pmm_init(struct physical_memory_info *mem_info);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t frame_addr);
uint64_t pmm_get_total_frames(void);
uint64_t pmm_get_free_frames(void);
uint64_t pmm_get_used_frames(void);

/* Virtual Memory Manager */
void vmm_init(void);
void* vmm_alloc_pages(size_t num_pages, uint64_t flags);
void vmm_free_pages(void* virtual_addr, size_t num_pages);

/* Utility Functions */
uint64_t paging_align_up(uint64_t addr, uint64_t alignment);
uint64_t paging_align_down(uint64_t addr, uint64_t alignment);
int paging_is_aligned(uint64_t addr, uint64_t alignment);
int paging_validate_range(uint64_t virtual_start, size_t pages);

/* Debug and Information Functions */
void paging_print_stats(void);
void paging_print_vm_regions(void);

/* Exception Handler */
void page_fault_handler(uint64_t error_code, uint64_t fault_addr);

/* Page Table Index Extraction Macros (for 4-level paging) */
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)   /* Bits 47-39 */
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)   /* Bits 38-30 */
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)   /* Bits 29-21 */
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)   /* Bits 20-12 */
#define PAGE_OFFSET(addr) ((addr) & 0xFFF)          /* Bits 11-0 */

/* Extract physical address from page entry (mask out flags) */
#define PAGE_ENTRY_ADDR(entry) ((entry) & 0x000FFFFFFFFFF000UL)

#endif /* PAGING_H */