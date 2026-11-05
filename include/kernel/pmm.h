/* NumOS — pmm.h — Physical Memory Manager */
#ifndef NUMOS_PMM_H
#define NUMOS_PMM_H

#include "libs/types.h"
#include "kernel/multiboot2.h"

void pmm_init(struct mb2_tag_mmap* mmap, uint64_t mb2_addr);
uint64_t pmm_alloc_frame(void);
void pmm_free_frame(uint64_t phys);
uint64_t pmm_get_free_count(void);
uint64_t pmm_get_total_count(void);

#endif
