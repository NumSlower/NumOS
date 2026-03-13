/*
 * gdt.c - Global Descriptor Table for x86-64
 * Implements proper segmentation for 64-bit long mode with TSS
 * FIXED: User segments swapped for sysret compatibility
 */

#include "cpu/gdt.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"

/* 64-bit Task State Segment (TSS) structure */
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

/* Number of GDT entries
 * 0: NULL descriptor
 * 1: Kernel Code
 * 2: Kernel Data
 * 3: User Data   <- SWAPPED for sysret!
 * 4: User Code   <- SWAPPED for sysret!
 * 5-6: TSS (takes 2 entries = 16 bytes in 64-bit mode)
 */
#define GDT_ENTRIES 7

/* GDT structure */
static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr gdt_pointer __attribute__((aligned(16)));

/* External assembly function to load GDT */
extern void gdt_flush_asm(uint64_t gdt_ptr);

/* TSS and IST stack (single CPU) */
static struct tss64 tss __attribute__((aligned(16)));
static uint8_t ist1_stack[16384] __attribute__((aligned(16)));

static void gdt_set_tss_descriptor(uint64_t base, uint32_t limit) {
    struct tss_entry desc;
    memset(&desc, 0, sizeof(desc));

    desc.length       = (uint16_t)(limit & 0xFFFF);
    desc.base_low16   = (uint16_t)(base & 0xFFFF);
    desc.base_mid8    = (uint8_t)((base >> 16) & 0xFF);
    desc.flags1       = 0x89; /* Present, DPL0, type=0x9 (available 64-bit TSS) */
    desc.flags2       = (uint8_t)((limit >> 16) & 0x0F);
    desc.base_high8   = (uint8_t)((base >> 24) & 0xFF);
    desc.base_upper32 = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    desc.reserved     = 0;

    /* TSS descriptor occupies two 8-byte GDT slots (entries 5 and 6) */
    memcpy(&gdt[5], &desc, sizeof(desc));
}

static void tss_load_tr(void) {
    uint16_t sel = GDT_TSS_SELECTOR;
    __asm__ volatile("ltr %w0" : : "r"(sel) : "memory");
}

/*
 * Set a GDT entry
 */
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    if (num >= GDT_ENTRIES) {
        return;
    }
    
    /* Base address */
    gdt[num].base_low = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    
    /* Limit */
    gdt[num].limit_low = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    
    /* Access flags */
    gdt[num].access = access;
}

/*
 * Initialize GDT
 */
void gdt_init(void) {
    vga_writestring("GDT: Starting initialization...\n");
    
    /* Set up GDT pointer */
    gdt_pointer.limit = (sizeof(struct gdt_entry) * GDT_ENTRIES) - 1;
    gdt_pointer.base = (uint64_t)&gdt;
    
    vga_writestring("GDT: Clearing GDT array (");
    print_dec(GDT_ENTRIES);
    vga_writestring(" entries)...\n");
    
    /* Clear GDT */
    memset(&gdt, 0, sizeof(struct gdt_entry) * GDT_ENTRIES);
    
    vga_writestring("GDT: Setting up descriptors...\n");
    
    /*
     * Entry 0: NULL descriptor (required by x86 architecture)
     */
    gdt_set_gate(0, 0, 0, 0, 0);
    
    /*
     * Entry 1: Kernel Code Segment (selector 0x08)
     * Base: 0, Limit: 0xFFFFF (ignored in 64-bit)
     * Access: 0x9A = Present, Ring 0, Code, Executable, Readable
     * Granularity: 0xA0 = 4KB granularity, 64-bit code segment
     */
    gdt_set_gate(1, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | 
                 GDT_ACCESS_SYSTEM | GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);
    
    /*
     * Entry 2: Kernel Data Segment (selector 0x10)
     * Access: 0x92 = Present, Ring 0, Data, Writable
     * Granularity: 0xC0 = 4KB granularity, 32-bit
     */
    gdt_set_gate(2, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 | 
                 GDT_ACCESS_SYSTEM | GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    /*
     * Entry 3: User Data Segment (selector 0x18 | 3 = 0x1B)
     * Access: 0xF2 = Present, Ring 3, Data, Writable
     * CRITICAL: Data must be at index 3 for sysret compatibility!
     * sysret loads SS = (STAR[63:48] + 8) | 3 = 0x18 | 3 = 0x1B
     */
    gdt_set_gate(3, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | 
                 GDT_ACCESS_SYSTEM | GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);
    
    /*
     * Entry 4: User Code Segment (selector 0x20 | 3 = 0x23)
     * Access: 0xFA = Present, Ring 3, Code, Executable, Readable
     * CRITICAL: Code must be at index 4 for sysret compatibility!
     * sysret loads CS = (STAR[63:48] + 16) | 3 = 0x20 | 3 = 0x23
     */
    gdt_set_gate(4, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 | 
                 GDT_ACCESS_SYSTEM | GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);

    /* Initialize TSS and install TSS descriptor (entries 5-6) */
    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = (uint16_t)sizeof(tss);
    tss.ist1 = (uint64_t)(uintptr_t)(ist1_stack + sizeof(ist1_stack));
    uint64_t rsp_now = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp_now));
    tss.rsp0 = rsp_now;

    gdt_set_tss_descriptor((uint64_t)(uintptr_t)&tss, (uint32_t)(sizeof(tss) - 1));
    
    vga_writestring("GDT: Descriptors configured\n");
    vga_writestring("GDT: Loading new GDT...\n");
    
    /* Load new GDT */
    gdt_flush_asm((uint64_t)&gdt_pointer);

    /* Load Task Register so ring transitions have a valid RSP0 */
    tss_load_tr();
    
    vga_writestring("GDT: New GDT loaded successfully\n");
    vga_writestring("GDT: Initialized with ");
    print_dec(GDT_ENTRIES);
    vga_writestring(" entries\n");
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
