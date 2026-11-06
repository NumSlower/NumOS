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

/* Address-space operations (Phase 10: Ring 3 isolation) */
uint64_t vmm_get_cr3(void);
void vmm_set_cr3(uint64_t phys);
uint64_t* vmm_clone_kernel_space(void);
void vmm_free_address_space(uint64_t* pml4);
void vmm_map_in(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags);
bool vmm_is_user_mapped(uint64_t* pml4, uint64_t virt);

#endif
