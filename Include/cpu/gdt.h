#ifndef GDT_H
#define GDT_H

#include "lib/base.h"

/* GDT Entry Structure for 64-bit mode */
struct gdt_entry {
    uint16_t limit_low;    // Lower 16 bits of limit
    uint16_t base_low;     // Lower 16 bits of base
    uint8_t base_middle;   // Next 8 bits of base
    uint8_t access;        // Access flags
    uint8_t granularity;   // Granularity and upper 4 bits of limit
    uint8_t base_high;     // Upper 8 bits of base
} __attribute__((packed));

/* TSS Entry for 64-bit mode (16 bytes) */
struct tss_entry {
    uint16_t length;
    uint16_t base_low16;
    uint8_t base_mid8;
    uint8_t flags1;
    uint8_t flags2;
    uint8_t base_high8;
    uint32_t base_upper32;
    uint32_t reserved;
} __attribute__((packed));

/* GDT Pointer Structure */
struct gdt_ptr {
    uint16_t limit;        // Upper 16 bits of all selector limits
    uint64_t base;         // Address of first gdt_entry struct
} __attribute__((packed));

/* GDT Access Byte Flags */
#define GDT_ACCESS_PRESENT     0x80  // Present bit
#define GDT_ACCESS_DPL0        0x00  // Ring 0 (kernel)
#define GDT_ACCESS_DPL1        0x20  // Ring 1
#define GDT_ACCESS_DPL2        0x40  // Ring 2
#define GDT_ACCESS_DPL3        0x60  // Ring 3 (user)
#define GDT_ACCESS_SYSTEM      0x10  // System segment
#define GDT_ACCESS_CODE        0x08  // Code segment
#define GDT_ACCESS_DATA        0x00  // Data segment
#define GDT_ACCESS_EXECUTABLE  0x08  // Executable
#define GDT_ACCESS_CONFORMING  0x04  // Conforming (code)
#define GDT_ACCESS_READABLE    0x02  // Readable (code)
#define GDT_ACCESS_WRITABLE    0x02  // Writable (data)
#define GDT_ACCESS_ACCESSED    0x01  // Accessed bit

/* GDT Granularity Byte Flags */
#define GDT_GRAN_4K            0x80  // 4KB granularity
#define GDT_GRAN_1B            0x00  // 1B granularity
#define GDT_GRAN_32BIT         0x40  // 32-bit segment
#define GDT_GRAN_16BIT         0x00  // 16-bit segment
#define GDT_GRAN_64BIT         0x20  // 64-bit segment (long mode)
#define GDT_GRAN_AVL           0x10  // Available for system use

/* Segment Selector Offsets */
#define GDT_NULL_SELECTOR      0x00
#define GDT_KERNEL_CODE        0x08
#define GDT_KERNEL_DATA        0x10
#define GDT_USER_CODE          0x18
#define GDT_USER_DATA          0x20

/* Function prototypes */
void gdt_init(void);
void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);

#endif /* GDT_H */