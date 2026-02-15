/*
 * tss.c — NumOS minimal TSS for Ring 3 interrupt handling
 *
 * ROOT CAUSE OF THE REBOOT:
 *   When the CPU takes any interrupt (timer, keyboard, etc.) while executing
 *   at Ring 3, it:
 *     1. Reads RSP0 from the TSS (Task State Segment).
 *     2. Switches the stack to that address.
 *     3. Pushes SS, RSP, RFLAGS, CS, RIP onto the new kernel stack.
 *     4. Jumps to the interrupt handler.
 *
 *   If no TSS descriptor exists in the GDT, or if the TSS isn't loaded (ltr),
 *   or if RSP0 == 0, the CPU triple-faults immediately → machine resets.
 *
 *   This file provides a minimal TSS that satisfies all three requirements.
 *
 * GDT LAYOUT (after tss_init):
 *   GDT[0] = null
 *   GDT[1] = kernel code  (0x08)
 *   GDT[2] = kernel data  (0x10)
 *   GDT[3] = user data    (0x1B)
 *   GDT[4] = user code    (0x23)
 *   GDT[5] = TSS low      (0x28) — selector used with ltr
 *   GDT[6] = TSS high     (0x30) — upper 32 bits of TSS base
 *
 * The TSS descriptor in 64-bit mode occupies TWO 8-byte GDT slots.
 * Your GDT must therefore be declared with at least 7 entries.
 */

#include "cpu/tss.h"
#include "drivers/vga.h"

/* ── TSS structure (64-bit, only the fields we care about) ───────────────── */

/*
 * x86-64 TSS.  Only RSP0 matters for our use case.  The full struct is
 * 104 bytes; we define it completely to get the layout right.
 */
typedef struct {
    uint32_t reserved0;
    uint64_t rsp0;          /* kernel stack pointer for Ring 0 entry */
    uint64_t rsp1;          /* unused */
    uint64_t rsp2;          /* unused */
    uint64_t reserved1;
    uint64_t ist[7];        /* interrupt stack table — unused, zero */
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;   /* I/O permission bitmap offset (point past TSS) */
} __attribute__((packed)) tss64_t;

/* ── GDT descriptor helpers ──────────────────────────────────────────────── */

/*
 * A 64-bit TSS descriptor occupies 16 bytes (two GDT slots):
 *
 *  Bits 63:56  base[31:24]
 *  Bits 55:52  flags (G=0, must-be-0, L=0, AVL=0)
 *  Bits 51:48  limit[19:16]
 *  Bits 47:40  access (P=1, DPL=0, 0, type=9 = available 64-bit TSS)
 *  Bits 39:32  base[23:16]
 *  Bits 31:16  base[15:0]
 *  Bits 15:0   limit[15:0]
 *
 * Followed immediately by:
 *  Bits 63:32  (reserved, must be 0)
 *  Bits 31:0   base[63:32]
 */
typedef struct {
    uint64_t low;
    uint64_t high;
} tss_descriptor_t;

static tss_descriptor_t make_tss_descriptor(uint64_t base, uint32_t limit)
{
    tss_descriptor_t d;

    /* Low 64 bits */
    d.low  = (uint64_t)(limit & 0xFFFF);           /* limit[15:0]  */
    d.low |= (base  & 0x00FFFFFFULL) << 16;         /* base[23:0]   */
    d.low |= (uint64_t)0x89ULL       << 40;         /* access: P=1, DPL=0, type=9 */
    d.low |= ((uint64_t)(limit >> 16) & 0xF) << 48; /* limit[19:16] */
    d.low |= ((base >> 24) & 0xFFULL) << 56;        /* base[31:24]  */

    /* High 64 bits: upper 32 bits of base, rest reserved=0 */
    d.high = (base >> 32) & 0xFFFFFFFFULL;

    return d;
}

/* ── Kernel interrupt stack (16 KB) ─────────────────────────────────────── */

#define IST_SIZE (16 * 1024)
static uint8_t g_int_stack[IST_SIZE] __attribute__((aligned(16)));

/* ── The TSS itself ──────────────────────────────────────────────────────── */

static tss64_t g_tss __attribute__((aligned(16)));

/* ── GDT access ──────────────────────────────────────────────────────────── */

/*
 * We need to write two 8-byte entries into the GDT starting at index 5.
 * The GDTR gives us the base address; we cast it to a uint64_t array.
 *
 * Your GDT must be declared with at least 7 entries, e.g.:
 *   uint64_t gdt[7] = { ... };
 *
 * If your GDT only has 5 entries (0-4), increase it to 7 before calling
 * tss_init().  The two new slots (indices 5 and 6) can start as zero.
 */
static void install_tss_in_gdt(void)
{
    /* Read the current GDTR */
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;

    __asm__ volatile ("sgdt %0" : "=m"(gdtr));

    uint64_t *gdt = (uint64_t *)(uintptr_t)gdtr.base;

    /* TSS descriptor goes at GDT[5] (two slots: 5 and 6) */
    uint64_t tss_base  = (uint64_t)(uintptr_t)&g_tss;
    uint32_t tss_limit = (uint32_t)(sizeof(g_tss) - 1);

    tss_descriptor_t desc = make_tss_descriptor(tss_base, tss_limit);

    gdt[5] = desc.low;
    gdt[6] = desc.high;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

void tss_init(void)
{
    /* Zero the TSS */
    uint8_t *p = (uint8_t *)&g_tss;
    for (size_t i = 0; i < sizeof(g_tss); i++) p[i] = 0;

    /* RSP0 = top of interrupt stack (CPU switches here on Ring 3 → Ring 0) */
    g_tss.rsp0       = (uint64_t)(uintptr_t)(g_int_stack + IST_SIZE);
    g_tss.iopb_offset = (uint16_t)sizeof(g_tss);  /* no IOPB */

    /* Install the 16-byte TSS descriptor at GDT[5]/GDT[6] */
    install_tss_in_gdt();

    /* Load the task register: selector = GDT[5] = 0x28 */
    __asm__ volatile ("ltr %0" : : "r"((uint16_t)0x28));

    vga_writestring("TSS: installed at GDT[5], selector=0x28, RSP0 set\n");
}

void tss_set_rsp0(uint64_t rsp0)
{
    g_tss.rsp0 = rsp0;
}