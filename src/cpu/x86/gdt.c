/*
 * gdt.c - Global Descriptor Table for x86-64
 * Implements proper segmentation for 64-bit long mode with TSS
 * FIXED: User segments swapped for sysret compatibility
 */

#include "cpu/gdt.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"

/* Number of GDT entries
 * 0: NULL descriptor
 * 1: Kernel Code
 * 2: Kernel Data
 * 3: User Data   <- SWAPPED for sysret!
 * 4: User Code   <- SWAPPED for sysret!
 * 5-6: TSS (takes 2 entries = 16 bytes in 64-bit mode)
 */
#define GDT_ENTRIES 7

/* GDT structure - now includes space for TSS */
static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr gdt_pointer __attribute__((aligned(16)));

/* Task State Segment structure for 64-bit mode */
struct tss {
    uint32_t reserved0;
    uint64_t rsp0;      /* Stack pointer for ring 0 */
    uint64_t rsp1;      /* Stack pointer for ring 1 */
    uint64_t rsp2;      /* Stack pointer for ring 2 */
    uint64_t reserved1;
    uint64_t ist[7];    /* Interrupt Stack Table pointers */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb;      /* I/O permission bitmap offset */
} __attribute__((packed));

static struct tss kernel_tss __attribute__((aligned(16)));

/* External assembly function to load GDT */
extern void gdt_flush_asm(uint64_t gdt_ptr);

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
 * Set up TSS descriptor in GDT (takes entries 5 and 6)
 */
static void gdt_set_tss(uint64_t tss_base, uint32_t tss_limit) {
    /* TSS descriptor is 16 bytes in 64-bit mode, spanning entries 5 and 6 */
    
    /* Lower 8 bytes (entry 5) */
    gdt[5].limit_low = tss_limit & 0xFFFF;
    gdt[5].base_low = tss_base & 0xFFFF;
    gdt[5].base_middle = (tss_base >> 16) & 0xFF;
    gdt[5].access = 0x89;  /* Present, available 64-bit TSS */
    gdt[5].granularity = 0x00;
    gdt[5].base_high = (tss_base >> 24) & 0xFF;
    
    /* Upper 8 bytes (entry 6) */
    gdt[6].limit_low = (tss_base >> 32) & 0xFFFF;
    gdt[6].base_low = (tss_base >> 48) & 0xFFFF;
    gdt[6].base_middle = 0;
    gdt[6].access = 0;
    gdt[6].granularity = 0;
    gdt[6].base_high = 0;
}

/*
 * Initialize TSS
 */
static void tss_init(void) {
    uint64_t tss_base = (uint64_t)&kernel_tss;
    uint32_t tss_limit = sizeof(struct tss) - 1;
    
    vga_writestring("GDT: Initializing TSS...\n");
    
    /* Clear TSS */
    memset(&kernel_tss, 0, sizeof(struct tss));
    
    /* Set stack pointer for ring 0 (will be updated when switching tasks) */
    kernel_tss.rsp0 = 0;
    
    /* Set I/O permission bitmap to end of TSS (no I/O bitmap) */
    kernel_tss.iopb = sizeof(struct tss);
    
    /* Set up TSS descriptor in GDT */
    gdt_set_tss(tss_base, tss_limit);
    
    vga_writestring("GDT: TSS configured at 0x");
    print_hex(tss_base);
    vga_writestring("\n");
}

/*
 * Load TSS into task register
 */
static void tss_load(void) {
    /* TSS selector is 0x28 (entry 5, index 5 * 8 = 40 = 0x28) */
    vga_writestring("GDT: Loading TSS (selector 0x28)...\n");
    __asm__ volatile("ltr %0" : : "r"((uint16_t)0x28));
    vga_writestring("GDT: TSS loaded successfully\n");
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
    
    /* Entries 5-6: TSS (initialized by tss_init) */
    
    vga_writestring("GDT: Descriptors configured\n");
    vga_writestring("GDT: Loading new GDT...\n");
    
    /* Load new GDT */
    gdt_flush_asm((uint64_t)&gdt_pointer);
    
    vga_writestring("GDT: New GDT loaded successfully\n");
    
    /* Initialize and load TSS */
    tss_init();
    tss_load();
    
    vga_writestring("GDT: Initialized with ");
    print_dec(GDT_ENTRIES);
    vga_writestring(" entries (including TSS)\n");
}

/*
 * Update kernel stack in TSS (for task switching)
 */
void tss_set_kernel_stack(uint64_t stack) {
    kernel_tss.rsp0 = stack;
}

/*
 * Print GDT information for debugging
 */
void gdt_print_info(void) {
    vga_writestring("\nGDT Information:\n");
    vga_writestring("  Base Address: 0x");
    print_hex(gdt_pointer.base);
    vga_writestring("\n  Limit: ");
    print_dec(gdt_pointer.limit + 1);
    vga_writestring(" bytes\n");
    vga_writestring("  Entries: ");
    print_dec(GDT_ENTRIES);
    vga_writestring("\n\n");
    
    const char *names[] = {
        "NULL Descriptor",
        "Kernel Code (0x08)",
        "Kernel Data (0x10)",
        "User Data (0x18/0x1B)",     /* SWAPPED! */
        "User Code (0x20/0x23)",     /* SWAPPED! */
        "TSS Lower (0x28)",
        "TSS Upper"
    };
    
    for (int i = 0; i < GDT_ENTRIES; i++) {
        vga_writestring("  Entry ");
        print_dec(i);
        vga_writestring(": ");
        vga_writestring(names[i]);
        vga_writestring("\n");
    }
    
    vga_writestring("\nTSS Information:\n");
    vga_writestring("  Base: 0x");
    print_hex((uint64_t)&kernel_tss);
    vga_writestring("\n  Size: ");
    print_dec(sizeof(struct tss));
    vga_writestring(" bytes\n");
    vga_writestring("  RSP0: 0x");
    print_hex(kernel_tss.rsp0);
    vga_writestring("\n  Selector: 0x28\n");
}