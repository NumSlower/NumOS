/* NumOS — include/kernel/idt.h — IDT header */
#ifndef NUMOS_KERNEL_IDT_H
#define NUMOS_KERNEL_IDT_H

#include "kernel/types.h"

struct idt_entry {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

void idt_set_gate(uint8_t vector, uint64_t handler, uint16_t selector, uint8_t type_attr, uint8_t ist);
void idt_init(void);

extern void idt_load(void* ptr);

#endif
