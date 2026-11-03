/* NumOS — include/kernel/paging.h — Paging header */
#ifndef NUMOS_KERNEL_PAGING_H
#define NUMOS_KERNEL_PAGING_H

#include "kernel/types.h"

#define PAGE_PRESENT      (1ULL << 0)
#define PAGE_WRITE        (1ULL << 1)
#define PAGE_USER         (1ULL << 2)
#define PAGE_WRITETHROUGH (1ULL << 3)
#define PAGE_NOCACHE      (1ULL << 4)
#define PAGE_ACCESSED     (1ULL << 5)
#define PAGE_DIRTY        (1ULL << 6)
#define PAGE_HUGE         (1ULL << 7)
#define PAGE_GLOBAL       (1ULL << 8)
#define PAGE_NX           (1ULL << 63)

void paging_init(void);
void paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags);
void paging_unmap_page(uint64_t virt);
uint64_t paging_get_phys(uint64_t virt);
void paging_flush_tlb(uint64_t virt);
void paging_flush_all(void);

#endif
