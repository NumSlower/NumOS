/* NumOS — src/kernel/gdt.c — GDT and TSS initialization */
#include "kernel/gdt.h"

static struct gdt_entry gdt[7];
static struct tss_entry tss;
static struct gdt_ptr gdtp;

static uint8_t ist1_stack[4096] __attribute__((aligned(16)));
static uint8_t ist2_stack[4096] __attribute__((aligned(16)));
static uint8_t ist3_stack[4096] __attribute__((aligned(16)));

static void set_gdt_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[index].base_low = (base & 0xFFFF);
    gdt[index].base_mid = (base >> 16) & 0xFF;
    gdt[index].base_high = (base >> 24) & 0xFF;
    gdt[index].limit_low = (limit & 0xFFFF);
    gdt[index].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[index].access = access;
}

void gdt_init(void) {
    /* Zero structures */
    for (int i = 0; i < 7; i++) {
        set_gdt_entry(i, 0, 0, 0, 0);
    }
    
    for (size_t i = 0; i < sizeof(tss); i++) {
        ((uint8_t*)&tss)[i] = 0;
    }

    /* 0: Null descriptor */
    /* 1: Kernel Code 64 */
    set_gdt_entry(1, 0, 0, 0x9A, 0x20);
    /* 2: Kernel Data */
    set_gdt_entry(2, 0, 0, 0x92, 0x00);
    /* 3: User Data */
    set_gdt_entry(3, 0, 0, 0xF2, 0x00);
    /* 4: User Code 64 */
    set_gdt_entry(4, 0, 0, 0xFA, 0x20);
    
    /* 5-6: TSS (16 bytes) */
    uint64_t tss_base = (uint64_t)&tss;
    uint32_t tss_limit = sizeof(tss) - 1;
    
    struct gdt_entry_tss* tss_desc = (struct gdt_entry_tss*)&gdt[5];
    tss_desc->limit_low = tss_limit & 0xFFFF;
    tss_desc->base_low = tss_base & 0xFFFF;
    tss_desc->base_mid = (tss_base >> 16) & 0xFF;
    tss_desc->access = 0x89;
    tss_desc->granularity = ((tss_limit >> 16) & 0x0F) | 0x00;
    tss_desc->base_high = (tss_base >> 24) & 0xFF;
    tss_desc->base_upper = tss_base >> 32;
    tss_desc->reserved = 0;
    
    tss.rsp0 = 0;
    tss.ist1 = (uint64_t)ist1_stack + sizeof(ist1_stack);
    tss.ist2 = (uint64_t)ist2_stack + sizeof(ist2_stack);
    tss.ist3 = (uint64_t)ist3_stack + sizeof(ist3_stack);
    tss.iopb_offset = sizeof(tss);

    gdtp.limit = sizeof(gdt) - 1;
    gdtp.base = (uint64_t)&gdt;
    
    gdt_load(&gdtp);
    tss_load(0x28);
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
