#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

/* Page size constants */
#define PAGE_SIZE       4096
#define LARGE_PAGE_SIZE (2 * 1024 * 1024)  // 2MB pages
#define PAGE_ENTRIES    512

/* Page table entry flags */
#define PAGE_PRESENT    0x001
#define PAGE_WRITABLE   0x002
#define PAGE_USER       0x004
#define PAGE_WRITETHROUGH 0x008
#define PAGE_CACHE_DISABLE 0x010
#define PAGE_ACCESSED   0x020
#define PAGE_DIRTY      0x040
#define PAGE_HUGE       0x080
#define PAGE_GLOBAL     0x100
#define PAGE_NX         0x8000000000000000UL

/* Virtual address layout */
#define KERNEL_VIRTUAL_BASE 0xFFFFFFFF80000000UL
#define USER_VIRTUAL_BASE   0x0000000000000000UL
#define KERNEL_HEAP_START   0xFFFFFFFF90000000UL

/* Page table structures */
typedef uint64_t page_entry_t;

struct page_table {
    page_entry_t entries[PAGE_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

/* Physical memory info */
struct physical_memory_info {
    uint64_t total_memory;
    uint64_t available_memory;
    uint64_t kernel_start;
    uint64_t kernel_end;
};

/* Page frame allocator */
struct page_frame {
    uint64_t address;
    uint8_t flags;
    struct page_frame *next;
};

#define FRAME_FREE      0x00
#define FRAME_USED      0x01
#define FRAME_KERNEL    0x02

/* Function prototypes */
void paging_init(void);
void paging_enable(void);
uint64_t paging_get_physical_address(uint64_t virtual_addr);
int paging_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
int paging_unmap_page(uint64_t virtual_addr);
void paging_flush_tlb(void);
void paging_flush_page(uint64_t virtual_addr);

/* Physical memory management */
void pmm_init(struct physical_memory_info *mem_info);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t frame_addr);
uint64_t pmm_get_total_frames(void);
uint64_t pmm_get_free_frames(void);
uint64_t pmm_get_used_frames(void);

/* Virtual memory management */
void vmm_init(void);
void* vmm_alloc_pages(size_t num_pages, uint64_t flags);
void vmm_free_pages(void* virtual_addr, size_t num_pages);

/* Page table management */
struct page_table* paging_get_page_table(uint64_t virtual_addr, int create);
page_entry_t* paging_get_page_entry(uint64_t virtual_addr, int create);
struct page_table* paging_create_page_table(void);
void paging_destroy_page_table(struct page_table* table);

/* Utility functions */
uint64_t paging_align_up(uint64_t addr, uint64_t alignment);
uint64_t paging_align_down(uint64_t addr, uint64_t alignment);
int paging_is_aligned(uint64_t addr, uint64_t alignment);

/* Page fault handler */
void page_fault_handler(uint64_t error_code, uint64_t fault_addr);

/* Constants for page table indices */
#define PML4_INDEX(addr) (((addr) >> 39) & 0x1FF)
#define PDPT_INDEX(addr) (((addr) >> 30) & 0x1FF)
#define PD_INDEX(addr)   (((addr) >> 21) & 0x1FF)
#define PT_INDEX(addr)   (((addr) >> 12) & 0x1FF)
#define PAGE_OFFSET(addr) ((addr) & 0xFFF)

/* External variables from boot */
extern struct page_table *kernel_pml4;
extern uint64_t kernel_physical_start;
extern uint64_t kernel_physical_end;

#endif /* PAGING_H */