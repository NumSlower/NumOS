/* NumOS — ioapic.h — I/O APIC */
#ifndef NUMOS_IOAPIC_H
#define NUMOS_IOAPIC_H

#include "libs/types.h"

bool ioapic_init(void);
void ioapic_set_entry(uint8_t index, uint64_t entry);
uint64_t ioapic_get_entry(uint8_t index);
void ioapic_mask_irq(uint8_t irq);
void ioapic_unmask_irq(uint8_t irq);
void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id);

#endif
