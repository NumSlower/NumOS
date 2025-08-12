#include "gdt.h"
#include "kernel.h"
#include "vga.h"

/* GDT entries array */
static struct gdt_entry gdt_entries[6];
static struct gdt_ptr gdt_pointer;
static struct tss_entry tss;

void gdt_init(void) {
    gdt_pointer.limit = (sizeof(struct gdt_entry) * 6) - 1;
    gdt_pointer.base = (uint64_t)&gdt_entries;

    /* Null descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);
    
    /* Kernel code segment (64-bit) */
    gdt_set_gate(1, 0, 0xFFFFFFFF, 
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM | GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
                 GDT_GRAN_64BIT | GDT_GRAN_4K);
    
    /* Kernel data segment */
    gdt_set_gate(2, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | GDT_ACCESS_SYSTEM | GDT_ACCESS_RW,
                 GDT_GRAN_64BIT | GDT_GRAN_4K);
    
    /* User code segment (64-bit) */
    gdt_set_gate(3, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM | GDT_ACCESS_EXECUTABLE | GDT_ACCESS_RW,
                 GDT_GRAN_64BIT | GDT_GRAN_4K);
    
    /* User data segment */
    gdt_set_gate(4, 0, 0xFFFFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_RING3 | GDT_ACCESS_SYSTEM | GDT_ACCESS_RW,
                 GDT_GRAN_64BIT | GDT_GRAN_4K);

    /* Initialize TSS */
    memset(&tss, 0, sizeof(tss));
    tss.rsp0 = 0;  /* Will be set later when we have proper stack management */
    tss.iomap_base = sizeof(tss);

    /* TSS descriptor (takes 2 GDT entries in 64-bit mode) */
    uint64_t tss_base = (uint64_t)&tss;
    uint64_t tss_limit = sizeof(tss) - 1;
    
    /* Low part of TSS descriptor */
    gdt_entries[5].limit_low = tss_limit & 0xFFFF;
    gdt_entries[5].base_low = tss_base & 0xFFFF;
    gdt_entries[5].base_middle = (tss_base >> 16) & 0xFF;
    gdt_entries[5].access = GDT_ACCESS_PRESENT | GDT_ACCESS_RING0 | 0x09; /* Available TSS */
    gdt_entries[5].granularity = ((tss_limit >> 16) & 0x0F);
    gdt_entries[5].base_high = (tss_base >> 24) & 0xFF;

    /* Load the new GDT */
    gdt_flush((uint64_t)&gdt_pointer);
    
    /* Load the TSS */
    tss_flush();
}

void gdt_set_gate(int32_t num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran) {
    gdt_entries[num].base_low = (base & 0xFFFF);
    gdt_entries[num].base_middle = (base >> 16) & 0xFF;
    gdt_entries[num].base_high = (base >> 24) & 0xFF;

    gdt_entries[num].limit_low = (limit & 0xFFFF);
    gdt_entries[num].granularity = (limit >> 16) & 0x0F;

    gdt_entries[num].granularity |= gran & 0xF0;
    gdt_entries[num].access = access;
}

void tss_set_stack(uint64_t stack) {
    tss.rsp0 = stack;
}