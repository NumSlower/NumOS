/* NumOS — sse.c — FPU/SSE init */
#include "cpu/sse.h"
#include "libs/types.h"

void sse_init(void) {
    uint64_t cr0, cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2); // EM
    cr0 |= (1ULL << 1);  // MP
    cr0 |= (1ULL << 5);  // NE
    cr0 &= ~(1ULL << 3); // TS
    __asm__ volatile("mov %0, %%cr0" :: "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);  // OSFXSR
    cr4 |= (1ULL << 10); // OSXMMEXCPT
    __asm__ volatile("mov %0, %%cr4" :: "r"(cr4));

    __asm__ volatile("fninit");

    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("ldmxcsr %0" :: "m"(mxcsr));
}
