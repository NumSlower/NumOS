/* NumOS — cpuid.c — CPUID detection */
#include "cpu/cpuid.h"

static char vendor_string[13];
static char brand_string[49];
static uint32_t max_leaf;
static uint32_t max_ext_leaf;
static struct cpuid_result leaf1;
static struct cpuid_result ext_leaf1;

extern void kprintf(const char* fmt, ...);

void cpuid_init(void) {
    struct cpuid_result res;

    // Leaf 0
    cpuid(0, 0, &res);
    max_leaf = res.eax;
    *(uint32_t*)&vendor_string[0] = res.ebx;
    *(uint32_t*)&vendor_string[4] = res.edx;
    *(uint32_t*)&vendor_string[8] = res.ecx;
    vendor_string[12] = '\0';

    if (max_leaf >= 1) {
        cpuid(1, 0, &leaf1);
    }

    cpuid(0x80000000, 0, &res);
    max_ext_leaf = res.eax;

    if (max_ext_leaf >= 0x80000001) {
        cpuid(0x80000001, 0, &ext_leaf1);
    }

    if (max_ext_leaf >= 0x80000004) {
        cpuid(0x80000002, 0, (struct cpuid_result*)&brand_string[0]);
        cpuid(0x80000003, 0, (struct cpuid_result*)&brand_string[16]);
        cpuid(0x80000004, 0, (struct cpuid_result*)&brand_string[32]);
    }
    brand_string[48] = '\0';

    kprintf("CPU Vendor: %s\n", vendor_string);
    kprintf("CPU Brand: %s\n", brand_string);
}

const char* cpuid_vendor(void) { return vendor_string; }
const char* cpuid_brand(void) { return brand_string; }

bool cpuid_has_apic(void) { return (leaf1.edx & (1 << 9)) != 0; }
bool cpuid_has_x2apic(void) { return (leaf1.ecx & (1 << 21)) != 0; }
bool cpuid_has_msr(void) { return (leaf1.edx & (1 << 5)) != 0; }
bool cpuid_has_sse(void) { return (leaf1.edx & (1 << 25)) != 0; }
bool cpuid_has_sse2(void) { return (leaf1.edx & (1 << 26)) != 0; }
bool cpuid_has_fpu(void) { return (leaf1.edx & (1 << 0)) != 0; }
bool cpuid_has_tsc(void) { return (leaf1.edx & (1 << 4)) != 0; }
bool cpuid_has_pae(void) { return (leaf1.edx & (1 << 6)) != 0; }

bool cpuid_has_1gb_pages(void) { return (ext_leaf1.edx & (1 << 26)) != 0; }
bool cpuid_has_nx(void) { return (ext_leaf1.edx & (1 << 20)) != 0; }
bool cpuid_has_long_mode(void) { return (ext_leaf1.edx & (1 << 29)) != 0; }
