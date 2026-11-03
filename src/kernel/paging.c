/* NumOS — src/kernel/paging.c — Paging initialization and control */
#include "kernel/paging.h"
#include "kernel/io.h"

#define MSR_EFER 0xC0000080
#define EFER_NXE (1ULL << 11)
#define CR0_WP (1ULL << 16)


void paging_init(void) {
    /* Enable EFER.NXE */
    uint64_t efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_NXE);
    
    /* Enable CR0.WP */
    uint64_t cr0 = read_cr0();
    write_cr0(cr0 | CR0_WP);
}

void paging_map_page(uint64_t virt, uint64_t phys, uint64_t flags) {
    (void)virt;
    (void)phys;
    (void)flags;
    /* To be implemented by VMM */
}

void paging_unmap_page(uint64_t virt) {
    (void)virt;
    /* To be implemented by VMM */
}

uint64_t paging_get_phys(uint64_t virt) {
    (void)virt;
    /* To be implemented by VMM */
    return 0;
}

void paging_flush_tlb(uint64_t virt) {
    __asm__ volatile("invlpg (%0)" : : "r" (virt) : "memory");
}

void paging_flush_all(void) {
    write_cr3(read_cr3());
}
