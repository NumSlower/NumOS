/*
 * gdt.c - Global Descriptor Table for x86-64
 *
 * Sets up the seven GDT entries required for NumOS:
 *
 *   Entry 0 (0x00): NULL descriptor (required by x86 architecture)
 *   Entry 1 (0x08): Kernel Code  - Ring 0, 64-bit
 *   Entry 2 (0x10): Kernel Data  - Ring 0
 *   Entry 3 (0x18): User Data    - Ring 3  (SYSRETQ loads SS from here)
 *   Entry 4 (0x20): User Code    - Ring 3, 64-bit  (SYSRETQ loads CS here)
 *   Entry 5-6 (0x28): TSS descriptor (16 bytes in 64-bit mode)
 *
 * User data MUST be at index 3 and user code at index 4 so that SYSRETQ
 * derives the correct selectors from the STAR MSR:
 *   SS = STAR[63:48] + 8  = 0x10 + 8  = 0x18 | 3  (entry 3, user data)
 *   CS = STAR[63:48] + 16 = 0x10 + 16 = 0x20 | 3  (entry 4, user code)
 */

#include "cpu/gdt.h"
#include "cpu/tss.h"
#include "kernel/kernel.h"
#include "drivers/graphices/vga.h"

/* =========================================================================
 * Module data
 * ======================================================================= */

#define GDT_ENTRIES 7   /* NULL + Kernel Code + Kernel Data + User Data
                           + User Code + TSS low + TSS high */

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct gdt_ptr   gdt_pointer      __attribute__((aligned(16)));
/* Provided in gdt_flush.asm; loads the GDTR and reloads segment registers. */
extern void gdt_flush_asm(uint64_t gdt_ptr);

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

/*
 * gdt_set_tss_descriptor - write the 64-bit TSS descriptor into entries 5-6.
 *
 * A 64-bit TSS descriptor is 16 bytes (two consecutive GDT slots) because the
 * upper 32 bits of the base address do not fit in a single 8-byte entry.
 */
static void gdt_set_tss_descriptor(uint64_t base, uint32_t limit) {
    struct tss_entry desc;
    memset(&desc, 0, sizeof(desc));

    desc.length       = (uint16_t)(limit & 0xFFFF);
    desc.base_low16   = (uint16_t)(base        & 0xFFFF);
    desc.base_mid8    = (uint8_t)((base >> 16) & 0xFF);
    desc.flags1       = 0x89;  /* Present, DPL=0, type=0x9 (64-bit TSS available) */
    desc.flags2       = (uint8_t)((limit >> 16) & 0x0F);
    desc.base_high8   = (uint8_t)((base >> 24) & 0xFF);
    desc.base_upper32 = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    desc.reserved     = 0;

    memcpy(&gdt[5], &desc, sizeof(desc));
}

/*
 * tss_load_tr - load the Task Register with the TSS selector.
 * Must be called after gdt_flush_asm() so the new GDT is active.
 */
static void tss_load_tr(void) {
    uint16_t sel = GDT_TSS_SELECTOR;
    __asm__ volatile("ltr %w0" : : "r"(sel) : "memory");
}

/* =========================================================================
 * Public API
 * ======================================================================= */

/*
 * gdt_set_gate - write one 8-byte GDT entry.
 * base and limit are only meaningful for system segments (not code/data in
 * 64-bit mode, where the base is ignored and the limit is flat 4 GB).
 */
void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                  uint8_t access, uint8_t gran) {
    if (num >= GDT_ENTRIES) return;

    gdt[num].base_low    = (uint16_t)(base & 0xFFFF);
    gdt[num].base_middle = (uint8_t)((base >> 16) & 0xFF);
    gdt[num].base_high   = (uint8_t)((base >> 24) & 0xFF);

    gdt[num].limit_low   = (uint16_t)(limit & 0xFFFF);
    gdt[num].granularity = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));

    gdt[num].access      = access;
}

/*
 * gdt_init - build the GDT, install the TSS descriptor, flush the GDTR,
 * and load the Task Register.
 */
void gdt_init(void) {
    vga_writestring("GDT: Starting initialization...\n");

    gdt_pointer.limit = (uint16_t)(sizeof(struct gdt_entry) * GDT_ENTRIES - 1);
    gdt_pointer.base  = (uint64_t)&gdt;

    memset(&gdt, 0, sizeof(struct gdt_entry) * GDT_ENTRIES);

    vga_writestring("GDT: Configuring descriptors...\n");

    /* Entry 0: NULL descriptor */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* Entry 1: Kernel Code (selector 0x08)
     * access  = Present | DPL0 | System | Code | Readable
     * gran    = 4KB granularity | 64-bit long mode */
    gdt_set_gate(1, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 |
                 GDT_ACCESS_SYSTEM  | GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);

    /* Entry 2: Kernel Data (selector 0x10)
     * access  = Present | DPL0 | System | Data | Writable
     * gran    = 4KB granularity | 32-bit (data segments ignore the L bit) */
    gdt_set_gate(2, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL0 |
                 GDT_ACCESS_SYSTEM  | GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* Entry 3: User Data (selector 0x18 | 3 = 0x1B)
     * CRITICAL: must be at index 3 for SYSRETQ SS selection.
     * access  = Present | DPL3 | System | Data | Writable */
    gdt_set_gate(3, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 |
                 GDT_ACCESS_SYSTEM  | GDT_ACCESS_DATA | GDT_ACCESS_WRITABLE,
                 GDT_GRAN_4K | GDT_GRAN_32BIT);

    /* Entry 4: User Code (selector 0x20 | 3 = 0x23)
     * CRITICAL: must be at index 4 for SYSRETQ CS selection.
     * access  = Present | DPL3 | System | Code | Readable */
    gdt_set_gate(4, 0, 0xFFFFF,
                 GDT_ACCESS_PRESENT | GDT_ACCESS_DPL3 |
                 GDT_ACCESS_SYSTEM  | GDT_ACCESS_CODE | GDT_ACCESS_READABLE,
                 GDT_GRAN_4K | GDT_GRAN_64BIT);

    tss_init();
    uint64_t tss_base = 0;
    uint32_t tss_limit = 0;
    tss_get_descriptor(&tss_base, &tss_limit);

    /* Entries 5-6: TSS descriptor (16 bytes, two GDT slots) */
    gdt_set_tss_descriptor(tss_base, tss_limit);

    vga_writestring("GDT: Loading new GDT...\n");
    gdt_flush_asm((uint64_t)&gdt_pointer);
    tss_load_tr();

    vga_writestring("GDT: Initialized with ");
    print_dec(GDT_ENTRIES);
    vga_writestring(" entries\n");
}
