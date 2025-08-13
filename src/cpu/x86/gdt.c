#include "gdt.h"
#include "kernel.h"

/* GDT entries - we'll use 5 entries */
#define GDT_ENTRIES 5
static struct gdt_entry gdt[GDT_ENTRIES];
static struct gdt_ptr gdt_pointer;

/* External assembly function to flush GDT */
extern void gdt_flush_asm(uint64_t);

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    /* Base Address */
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    /* Limit */
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    
    /* Granularity and access flags */
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_init(void) {
    /* Set up GDT pointer */
    gdt_pointer.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    
    /* Clear GDT */
    memset(&gdt, 0, sizeof(struct gdt_entry) * GDT_ENTRIES);
    
    /* NULL descriptor (entry 0) */
    gdt_set_gate(0, 0, 0, 0, 0);
    
    /* Kernel Code Segment (entry 1) - 64-bit */
    gdt_set_gate(1, 0, 0xFFFFF, 
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /* Kernel Data Segment (entry 2) - 64-bit */
    gdt_set_gate(2, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /* User Code Segment (entry 3) - 64-bit */
    gdt_set_gate(3, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /* User Data Segment (entry 4) - 64-bit */
    gdt_set_gate(4, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /* Load the GDT */
    gdt_flush_asm((uint64_t)&gdt_pointer);
}