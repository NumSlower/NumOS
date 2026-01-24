/*
 * gdt.c - Enhanced Global Descriptor Table for 64-bit mode
 * Provides proper segmentation setup for long mode
 */

#include "cpu/gdt.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"

/* GDT entries - 5 standard entries for 64-bit mode */
#define GDT_ENTRIES 5

/* Aligned GDT structure */
static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr gdt_pointer __attribute__((aligned(16)));

/* External assembly function to flush GDT */
extern void gdt_flush_asm(uint64_t);

/*
 * Set a GDT entry
 * In 64-bit mode, base and limit are ignored for code/data segments
 * but still used for system segments (TSS)
 */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (num >= GDT_ENTRIES) {
        vga_writestring("GDT: Warning - Invalid entry number\n");
        return;
    }
    
    /* Set base address */
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    /* Set limit */
    gdt[num].limit_low = (limit & 0xFFFF);
    
    /* Set granularity (contains upper 4 bits of limit) */
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    
    /* Set access flags */
    gdt[num].access = access;
}

/*
 * Initialize the GDT for 64-bit long mode
 */
void gdt_init(void) {
    /* Set up GDT pointer structure */
    gdt_pointer.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    
    /* Clear entire GDT */
    memset(&gdt, 0, sizeof(struct gdt_entry) * GDT_ENTRIES);
    
    /*
     * Entry 0: NULL Descriptor (Required by x86/x64 architecture)
     * Must be all zeros
     */
    gdt_set_gate(0, 0, 0, 0, 0);
    
    /*
     * Entry 1: Kernel Code Segment (64-bit)
     * 
     * Base: 0x00000000 (ignored in 64-bit mode)
     * Limit: 0xFFFFF (ignored in 64-bit mode)
     * 
     * Access byte (0x9A = 10011010b):
     *   Bit 7 (P):  1 = Present
     *   Bit 6-5 (DPL): 00 = Ring 0 (kernel privilege)
     *   Bit 4 (S):  1 = Code/Data segment
     *   Bit 3 (E):  1 = Executable (code segment)
     *   Bit 2 (DC): 0 = Grows up
     *   Bit 1 (RW): 1 = Readable
     *   Bit 0 (A):  0 = Not accessed yet
     * 
     * Granularity byte (0xA0 = 10100000b):
     *   Bit 7 (G):  1 = 4KB granularity (ignored)
     *   Bit 6 (D/B): 0 = Must be 0 for 64-bit
     *   Bit 5 (L):  1 = 64-bit code segment (Long mode)
     *   Bit 4 (AVL): 0 = Available for system use
     *   Bits 3-0:   0000 = Upper 4 bits of limit (ignored)
     */
    gdt_set_gate(1, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_SYSTEM |
                 GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /*
     * Entry 2: Kernel Data Segment
     * 
     * Access byte (0x92 = 10010010b):
     *   Bit 7 (P):  1 = Present
     *   Bit 6-5 (DPL): 00 = Ring 0 (kernel privilege)
     *   Bit 4 (S):  1 = Code/Data segment
     *   Bit 3 (E):  0 = Not executable (data segment)
     *   Bit 2 (DC): 0 = Grows up
     *   Bit 1 (RW): 1 = Writable
     *   Bit 0 (A):  0 = Not accessed yet
     * 
     * Granularity byte (0xC0 = 11000000b):
     *   Bit 7 (G):  1 = 4KB granularity
     *   Bit 6 (D/B): 1 = 32-bit operands
     *   Bit 5 (L):  0 = Not 64-bit (data segments are always 32-bit mode)
     *   Bit 4 (AVL): 0 = Available for system use
     */
    gdt_set_gate(2, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | GDT_ACCESS_SYSTEM |
                 GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    /*
     * Entry 3: User Code Segment (64-bit)
     * 
     * Access byte (0xFA = 11111010b):
     *   Bit 7 (P):  1 = Present
     *   Bit 6-5 (DPL): 11 = Ring 3 (user privilege)
     *   Bit 4 (S):  1 = Code/Data segment
     *   Bit 3 (E):  1 = Executable (code segment)
     *   Bit 2 (DC): 0 = Grows up
     *   Bit 1 (RW): 1 = Readable
     *   Bit 0 (A):  0 = Not accessed yet
     */
    gdt_set_gate(3, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_SYSTEM |
                 GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /*
     * Entry 4: User Data Segment
     * 
     * Access byte (0xF2 = 11110010b):
     *   Bit 7 (P):  1 = Present
     *   Bit 6-5 (DPL): 11 = Ring 3 (user privilege)
     *   Bit 4 (S):  1 = Code/Data segment
     *   Bit 3 (E):  0 = Not executable (data segment)
     *   Bit 2 (DC): 0 = Grows up
     *   Bit 1 (RW): 1 = Writable
     *   Bit 0 (A):  0 = Not accessed yet
     */
    gdt_set_gate(4, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | GDT_ACCESS_SYSTEM |
                 GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    /* Load the new GDT */
    gdt_flush_asm((uint64_t)&gdt_pointer);
    
    vga_writestring("GDT: Loaded with ");
    print_dec(GDT_ENTRIES);
    vga_writestring(" entries\n");
}

/*
 * Print GDT information for debugging
 */
void gdt_print_info(void) {
    vga_writestring("GDT Information:\n");
    vga_writestring("  Base: 0x");
    print_hex(gdt_pointer.base);
    vga_writestring("\n  Limit: ");
    print_dec(gdt_pointer.limit + 1);
    vga_writestring(" bytes\n");
    vga_writestring("  Entries: ");
    print_dec(GDT_ENTRIES);
    vga_writestring("\n\n");
    
    for (int i = 0; i < GDT_ENTRIES; i++) {
        vga_writestring("  Entry ");
        print_dec(i);
        vga_writestring(": ");
        
        switch(i) {
            case 0: vga_writestring("NULL Descriptor"); break;
            case 1: vga_writestring("Kernel Code (64-bit)"); break;
            case 2: vga_writestring("Kernel Data"); break;
            case 3: vga_writestring("User Code (64-bit)"); break;
            case 4: vga_writestring("User Data"); break;
            default: vga_writestring("Unknown"); break;
        }
        
        vga_writestring("\n    Access: 0x");
        print_hex32(gdt[i].access);
        vga_writestring(", Granularity: 0x");
        print_hex32(gdt[i].granularity);
        vga_writestring("\n");
    }
}