/* NumOS — vmm.h — Virtual Memory Manager */
#ifndef NUMOS_VMM_H
#define NUMOS_VMM_H

#include "libs/types.h"
#include "cpu/paging.h"

void vmm_init(void);
void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags);
void vmm_unmap(uint64_t virt);
uint64_t vmm_virt_to_phys(uint64_t virt);
bool vmm_is_mapped(uint64_t virt);

#endif
