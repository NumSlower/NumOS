/* NumOS — vmm.c — Virtual Memory Manager */
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "libs/string.h"

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

/* ---- address-space operations (Phase 10) ---- */

uint64_t vmm_get_cr3(void) {
    uint64_t cr3;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
    return cr3;
}

void vmm_set_cr3(uint64_t phys) {
    __asm__ volatile("mov %0, %%cr3" : : "r"(phys) : "memory");
}

/* Copies every present table page down to the PD level so a child address
 * space can diverge (map user pages, split huge pages) without touching
 * kernel mappings. Leaf entries (2MB/1GB pages, PTEs) are copied by value.
 * All tables live in identity-mapped low memory, so physical page numbers
 * double as virtual pointers. */
static void copy_pdpt(uint64_t* dst_pdpt, const uint64_t* src_pdpt);
static void copy_pd(uint64_t* dst_pd, const uint64_t* src_pd);

static void copy_pd(uint64_t* dst_pd, const uint64_t* src_pd) {
    memcpy(dst_pd, src_pd, 4096);
}

static void copy_pdpt(uint64_t* dst_pdpt, const uint64_t* src_pdpt) {
    for (uint32_t i = 0; i < 512; i++) {
        uint64_t e = src_pdpt[i];
        if ((e & PAGE_PRESENT) && !(e & PAGE_HUGE)) {
            uint64_t* src_pd = (uint64_t*)(e & PTE_ADDR_MASK);
            uint64_t* dst_pd = (uint64_t*)pmm_alloc_frame();
            if (!dst_pd) return;
            memset(dst_pd, 0, 4096);
            copy_pd(dst_pd, src_pd);
            dst_pdpt[i] = ((uint64_t)dst_pd & PTE_ADDR_MASK) | (e & 0xFFF)
                        | PAGE_USER | PAGE_WRITE;
        } else {
            dst_pdpt[i] = e;
        }
    }
}

uint64_t* vmm_clone_kernel_space(void) {
    uint64_t* pml4 = (uint64_t*)pmm_alloc_frame();
    if (!pml4) return NULL;
    memset(pml4, 0, 4096);

    for (uint32_t i = 0; i < 512; i++) {
        uint64_t e = boot_pml4[i];
        if (!(e & PAGE_PRESENT)) continue;
        if (e & PAGE_HUGE) {
            pml4[i] = e;
            continue;
        }
        uint64_t* src_pdpt = (uint64_t*)(e & PTE_ADDR_MASK);
        uint64_t* dst_pdpt = (uint64_t*)pmm_alloc_frame();
        if (!dst_pdpt) return NULL;
        memset(dst_pdpt, 0, 4096);
        copy_pdpt(dst_pdpt, src_pdpt);
        pml4[i] = ((uint64_t)dst_pdpt & PTE_ADDR_MASK) | (e & 0xFFF)
                | PAGE_USER | PAGE_WRITE;
    }
    return pml4;
}

void vmm_free_address_space(uint64_t* pml4) {
    if (!pml4) return;
    for (uint32_t i = 0; i < 512; i++) {
        uint64_t e = pml4[i];
        if (!(e & PAGE_PRESENT) || (e & PAGE_HUGE)) continue;
        uint64_t* pdpt = (uint64_t*)(e & PTE_ADDR_MASK);
        for (uint32_t j = 0; j < 512; j++) {
            uint64_t pe = pdpt[j];
            if ((pe & PAGE_PRESENT) && !(pe & PAGE_HUGE)) {
                pmm_free_frame(pe & PTE_ADDR_MASK);
            }
        }
        pmm_free_frame((uint64_t)pdpt);
    }
    pmm_free_frame((uint64_t)pml4);
}

/* Maps one 4K page into a specific address space. Splits a covering 2MB
 * huge page leaf into a 4K page table first (preserving the other 511
 * mappings), so user regions can coexist with the boot identity map. */
void vmm_map_in(uint64_t* pml4, uint64_t virt, uint64_t phys, uint64_t flags) {
    uint16_t pml4_idx = (virt >> 39) & 0x1FF;
    uint16_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint16_t pd_idx   = (virt >> 21) & 0x1FF;
    uint16_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & PTE_ADDR_MASK);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT) || (pdpt[pdpt_idx] & PAGE_HUGE)) return;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & PTE_ADDR_MASK);

    if (pd[pd_idx] & PAGE_HUGE) {
        uint64_t leaf = pd[pd_idx];
        uint64_t leaf_phys = leaf & 0x000FFFFFFFE00000ULL;
        uint64_t leaf_flags = (leaf & 0xFFF & ~PAGE_HUGE) & ~PAGE_HUGE;
        uint64_t* pt = (uint64_t*)pmm_alloc_frame();
        if (!pt) return;
        memset(pt, 0, 4096);
        for (uint32_t i = 0; i < 512; i++) {
            pt[i] = (leaf_phys + (uint64_t)i * 4096) | leaf_flags | PAGE_PRESENT;
        }
        pd[pd_idx] = ((uint64_t)pt & PTE_ADDR_MASK) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    if (!(pd[pd_idx] & PAGE_PRESENT)) return;
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & PTE_ADDR_MASK);
    pt[pt_idx] = (phys & PTE_ADDR_MASK) | flags | PAGE_PRESENT;
}

/* True when `virt` maps to a USER page in the given address space (a
 * supervisor identity copy from a leaf split does not count). */
bool vmm_is_user_mapped(uint64_t* pml4, uint64_t virt) {
    uint16_t pml4_idx = (virt >> 39) & 0x1FF;
    uint16_t pdpt_idx = (virt >> 30) & 0x1FF;
    uint16_t pd_idx   = (virt >> 21) & 0x1FF;
    uint16_t pt_idx   = (virt >> 12) & 0x1FF;

    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return false;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & PTE_ADDR_MASK);
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT) || (pdpt[pdpt_idx] & PAGE_HUGE)) return false;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & PTE_ADDR_MASK);
    if (!(pd[pd_idx] & PAGE_PRESENT) || (pd[pd_idx] & PAGE_HUGE)) return false;
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & PTE_ADDR_MASK);
    return (pt[pt_idx] & PAGE_PRESENT) && (pt[pt_idx] & PAGE_USER);
}
