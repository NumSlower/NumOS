#include "cpu/gdt.h"
#include "kernel/kernel.h"

/* GDT entries - 5 standard entries for 64-bit mode */
#define GDT_ENTRIES 5
static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr gdt_pointer __attribute__((aligned(16)));

/* External assembly function to flush GDT */
extern void gdt_flush_asm(uint64_t);

void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (num >= GDT_ENTRIES) return;
    
    /* For 64-bit mode, base and limit are largely ignored for code/data segments */
    /* They're still used for compatibility and some legacy features */
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_init(void) {
    /* Set up GDT pointer */
    gdt_pointer.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    
    /* Clear GDT */
    memset(&gdt, 0, sizeof(struct gdt_entry) * GDT_ENTRIES);
    
    /* Entry 0: NULL descriptor (required) */
    gdt_set_gate(0, 0, 0, 0, 0);
    
    /* Entry 1: Kernel Code Segment (64-bit) */
    /* In 64-bit mode, the L (long mode) bit is in the granularity byte */
    /* Access: Present, DPL0, System, Code, Readable */
    gdt_set_gate(1, 0, 0xFFFFF, 
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);  // 64-bit code segment
    
    /* Entry 2: Kernel Data Segment (64-bit) */
    /* Access: Present, DPL0, System, Data, Writable */
    gdt_set_gate(2, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);  // Data segment uses 32-bit flag
    
    /* Entry 3: User Code Segment (64-bit) */
    /* Access: Present, DPL3, System, Code, Readable */
    gdt_set_gate(3, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);  // 64-bit code segment
    
    /* Entry 4: User Data Segment (64-bit) */
    /* Access: Present, DPL3, System, Data, Writable */
    gdt_set_gate(4, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_SYSTEM | 
                 GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);  // Data segment uses 32-bit flag
    
    /* Load the GDT */
    gdt_flush_asm((uint64_t)&gdt_pointer);
}