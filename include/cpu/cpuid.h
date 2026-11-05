/* NumOS — cpuid.h — CPUID detection */
#ifndef NUMOS_CPUID_H
#define NUMOS_CPUID_H

#include "libs/types.h"

struct cpuid_result {
    uint32_t eax, ebx, ecx, edx;
};

static inline void cpuid(uint32_t leaf, uint32_t subleaf, struct cpuid_result* out) {
    __asm__ volatile("cpuid"
        : "=a"(out->eax), "=b"(out->ebx), "=c"(out->ecx), "=d"(out->edx)
        : "a"(leaf), "c"(subleaf));
}

void cpuid_init(void);
const char* cpuid_vendor(void);
const char* cpuid_brand(void);
bool cpuid_has_apic(void);
bool cpuid_has_x2apic(void);
bool cpuid_has_msr(void);
bool cpuid_has_sse(void);
bool cpuid_has_sse2(void);
bool cpuid_has_fpu(void);
bool cpuid_has_tsc(void);
bool cpuid_has_pae(void);
bool cpuid_has_1gb_pages(void);
bool cpuid_has_nx(void);
bool cpuid_has_long_mode(void);

#endif
