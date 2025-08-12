#ifndef GDT_H
#define GDT_H

#include <stdint.h>

/* GDT Entry Structure */
struct gdt_entry {
    uint16_t limit_low;     /* Lower 16 bits of limit */
    uint16_t base_low;      /* Lower 16 bits of base */
    uint8_t  base_middle;   /* Next 8 bits of base */
    uint8_t  access;        /* Access flags */
    uint8_t  granularity;   /* Granularity */
    uint8_t  base_high;     /* High 8 bits of base */
} __attribute__((packed));

/* GDT Pointer Structure */
struct gdt_ptr {
    uint16_t limit;         /* Size of GDT table */
    uint64_t base;          /* Address of GDT table */
} __attribute__((packed));

/* TSS (Task State Segment) Structure for 64-bit */
struct tss_entry {
    uint32_t reserved1;
    uint64_t rsp0;          /* Ring 0 stack pointer */
    uint64_t rsp1;          /* Ring 1 stack pointer */
    uint64_t rsp2;          /* Ring 2 stack pointer */
    uint64_t reserved2;
    uint64_t ist1;          /* Interrupt Stack Table 1 */
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;
    uint64_t reserved3;
    uint16_t reserved4;
    uint16_t iomap_base;    /* I/O Map Base Address */
} __attribute__((packed));

/* GDT Segment Selectors */
#define GDT_KERNEL_CODE     0x08
#define GDT_KERNEL_DATA     0x10
#define GDT_USER_CODE       0x18
#define GDT_USER_DATA       0x20
#define GDT_TSS             0x28

/* Access byte flags */
#define GDT_ACCESS_PRESENT      0x80
#define GDT_ACCESS_RING0        0x00
#define GDT_ACCESS_RING1        0x20
#define GDT_ACCESS_RING2        0x40
#define GDT_ACCESS_RING3        0x60
#define GDT_ACCESS_SYSTEM       0x10
#define GDT_ACCESS_EXECUTABLE   0x08
#define GDT_ACCESS_DC           0x04
#define GDT_ACCESS_RW           0x02
#define GDT_ACCESS_ACCESSED     0x01

/* Granularity byte flags */
#define GDT_GRAN_4K             0x80
#define GDT_GRAN_32BIT          0x40
#define GDT_GRAN_64BIT          0x20
#define GDT_GRAN_AVL            0x10

/* Function prototypes */
void gdt_init(void);
void gdt_set_gate(int32_t num, uint64_t base, uint64_t limit, uint8_t access, uint8_t gran);
void tss_set_stack(uint64_t stack);

/* Assembly functions */
extern void gdt_flush(uint64_t gdt_ptr);
extern void tss_flush(void);

#endif /* GDT_H */