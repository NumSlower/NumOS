#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>

/* Page size constants */
#define PAGE_SIZE           4096
#define PAGE_ENTRIES        512
#define PAGE_SHIFT          12
#define PAGE_MASK           0xFFFFFFFFFFFFF000UL

/* Page table level offsets */
#define PML4_OFFSET(addr)   (((addr) >> 39) & 0x1FF)
#define PDP_OFFSET(addr)    (((addr) >> 30) & 0x1FF)
#define PD_OFFSET(addr)     (((addr) >> 21) & 0x1FF)
#define PT_OFFSET(addr)     (((addr) >> 12) & 0x1FF)

/* Page flags */
#define PAGE_PRESENT        0x001
#define PAGE_WRITABLE       0x002
#define PAGE_USER           0x004
#define PAGE_WRITE_THROUGH  0x008
#define PAGE_CACHE_DISABLED 0x010
#define PAGE_ACCESSED       0x020
#define PAGE_DIRTY          0x040
#define PAGE_HUGE           0x080
#define PAGE_GLOBAL         0x100
#define PAGE_NO_EXECUTE     0x8000000000000000UL

/* Virtual memory layout */
#define KERNEL_VIRTUAL_BASE 0xFFFF800000000000UL
#define USER_VIRTUAL_BASE   0x0000000000400000UL
#define HEAP_START          0xFFFF800010000000UL
#define HEAP_END            0xFFFF8000FFFFFFFFUL

/* Page table entry structure */
typedef uint64_t page_table_entry_t;

/* Page directory structure */
typedef struct {
    page_table_entry_t entries[PAGE_ENTRIES];
} __attribute__((aligned(PAGE_SIZE))) page_table_t;

/* Virtual address structure */
typedef struct {
    uint16_t offset;
    uint16_t pt_index;
    uint16_t pd_index;
    uint16_t pdp_index;
    uint16_t pml4_index;
    uint16_t sign_ext;
} __attribute__((packed)) virtual_addr_t;

/* Physical memory region */
typedef struct memory_region {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    struct memory_region *next;
} memory_region_t;

/* Memory types */
#define MEMORY_AVAILABLE    1
#define MEMORY_RESERVED     2
#define MEMORY_ACPI_RECLAIM 3
#define MEMORY_ACPI_NVS     4
#define MEMORY_BAD          5

/* Function prototypes */
void paging_init(void);
void paging_map_page(uint64_t virtual_addr, uint64_t physical_addr, uint64_t flags);
void paging_unmap_page(uint64_t virtual_addr);
uint64_t paging_get_physical_addr(uint64_t virtual_addr);
uint64_t paging_allocate_page(void);
void paging_free_page(uint64_t physical_addr);
void paging_map_range(uint64_t virtual_start, uint64_t physical_start, uint64_t size, uint64_t flags);
void paging_unmap_range(uint64_t virtual_start, uint64_t size);

/* Page table management */
page_table_t *paging_get_current_directory(void);
void paging_switch_directory(page_table_t *dir);
page_table_t *paging_create_directory(void);
void paging_destroy_directory(page_table_t *dir);

/* Memory region management */
void paging_add_memory_region(uint64_t base, uint64_t length, uint32_t type);
memory_region_t *paging_get_memory_regions(void);
uint64_t paging_get_total_memory(void);
uint64_t paging_get_available_memory(void);

/* Page fault handling */
void page_fault_handler(void);

/* Assembly functions */
extern void paging_load_directory(uint64_t dir_physical);
extern uint64_t paging_read_cr3(void);
extern void paging_invalidate_page(uint64_t virtual_addr);
extern void paging_flush_tlb(void);

/* Utility functions */
uint64_t virt_to_phys(uint64_t virtual_addr);
uint64_t phys_to_virt(uint64_t physical_addr);
int is_page_aligned(uint64_t addr);
uint64_t align_up(uint64_t addr, uint64_t alignment);
uint64_t align_down(uint64_t addr, uint64_t alignment);

#endif /* PAGING_H */