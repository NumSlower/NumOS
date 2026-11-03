/* NumOS — vmm.c — Virtual Memory Manager */
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "kernel/string.h"

#define PTE_ADDR_MASK 0x000FFFFFFFFFF000ULL

static uint64_t* boot_pml4;

void vmm_init(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    boot_pml4 = (uint64_t*)(cr3 & PTE_ADDR_MASK);
}

static inline void invlpg(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
}

void vmm_map(uint64_t virt, uint64_t phys, uint64_t flags) {
    uint16_t pml4_idx = (virt >> 39) & 0x1FF;
    uint16_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint16_t pd_idx   = (virt >> 21) & 0x1FF;
    uint16_t pt_idx   = (virt >> 12) & 0x1FF;
    
    uint64_t* pdpt;
    if (boot_pml4[pml4_idx] & PAGE_PRESENT) {
        pdpt = (uint64_t*)(boot_pml4[pml4_idx] & PTE_ADDR_MASK);
    } else {
        pdpt = (uint64_t*)pmm_alloc_frame();
        if (!pdpt) return;
        memset(pdpt, 0, 4096);
        boot_pml4[pml4_idx] = (uint64_t)pdpt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    
    /* Check for 1GB huge page at PDPT level */
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        return; /* Already covered by a 1GB huge page, skip */
    }

    uint64_t* pd;
    if (pdpt[pdpt_idx] & PAGE_PRESENT) {
        pd = (uint64_t*)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    } else {
        pd = (uint64_t*)pmm_alloc_frame();
        if (!pd) return;
        memset(pd, 0, 4096);
        pdpt[pdpt_idx] = (uint64_t)pd | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    
    /* Check for 2MB huge page at PD level — boot maps 4GB this way */
    if (pd[pd_idx] & PAGE_HUGE) {
        return; /* Already covered by a 2MB huge page, skip */
    }

    uint64_t* pt;
    if (pd[pd_idx] & PAGE_PRESENT) {
        pt = (uint64_t*)(pd[pd_idx] & PTE_ADDR_MASK);
    } else {
        pt = (uint64_t*)pmm_alloc_frame();
        if (!pt) return;
        memset(pt, 0, 4096);
        pd[pd_idx] = (uint64_t)pt | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }
    
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags | PAGE_PRESENT;
    invlpg(virt);
}

void vmm_unmap(uint64_t virt) {
    uint16_t pml4_idx = (virt >> 39) & 0x1FF;
    uint16_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint16_t pd_idx   = (virt >> 21) & 0x1FF;
    uint16_t pt_idx   = (virt >> 12) & 0x1FF;
    
    if (!(boot_pml4[pml4_idx] & PAGE_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)(boot_pml4[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return;
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & PTE_ADDR_MASK);
    
    pt[pt_idx] = 0;
    invlpg(virt);
}

uint64_t vmm_virt_to_phys(uint64_t virt) {
    uint16_t pml4_idx = (virt >> 39) & 0x1FF;
    uint16_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint16_t pd_idx   = (virt >> 21) & 0x1FF;
    uint16_t pt_idx   = (virt >> 12) & 0x1FF;
    
    if (!(boot_pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)(boot_pml4[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    if (pdpt[pdpt_idx] & PAGE_HUGE) {
        /* 1GB huge page */
        return (pdpt[pdpt_idx] & 0x000FFFFFC0000000ULL) | (virt & 0x3FFFFFFF);
    }
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    if (pd[pd_idx] & PAGE_HUGE) {
        /* 2MB huge page */
        return (pd[pd_idx] & 0x000FFFFFFFE00000ULL) | (virt & 0x1FFFFF);
    }
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & PTE_ADDR_MASK);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    return (pt[pt_idx] & PTE_ADDR_MASK) | (virt & 0xFFF);
}

bool vmm_is_mapped(uint64_t virt) {
    uint16_t pml4_idx = (virt >> 39) & 0x1FF;
    uint16_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint16_t pd_idx   = (virt >> 21) & 0x1FF;
    uint16_t pt_idx   = (virt >> 12) & 0x1FF;
    
    if (!(boot_pml4[pml4_idx] & PAGE_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(boot_pml4[pml4_idx] & PTE_ADDR_MASK);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return false;
    if (pdpt[pdpt_idx] & PAGE_HUGE) return true;  /* 1GB huge page */
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return false;
    if (pd[pd_idx] & PAGE_HUGE) return true;  /* 2MB huge page */
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & PTE_ADDR_MASK);
    
    return (pt[pt_idx] & PAGE_PRESENT) != 0;
}
