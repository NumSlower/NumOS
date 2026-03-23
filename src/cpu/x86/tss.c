#include "cpu/tss.h"
#include "kernel/kernel.h"

struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;
    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed));

static struct tss64 tss __attribute__((aligned(16)));
static uint8_t ist1_stack[16384] __attribute__((aligned(16)));

void tss_init(void) {
    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = (uint16_t)sizeof(tss);
    tss.ist1 = (uint64_t)(uintptr_t)(ist1_stack + sizeof(ist1_stack));

    uint64_t rsp_now = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_now));
    tss.rsp0 = rsp_now;
}

void tss_set_kernel_stack(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}

void tss_get_descriptor(uint64_t *base, uint32_t *limit) {
    if (base) *base = (uint64_t)(uintptr_t)&tss;
    if (limit) *limit = (uint32_t)(sizeof(tss) - 1);
}
