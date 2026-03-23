#ifndef APIC_H
#define APIC_H

#include "lib/base.h"

int apic_is_available(void);
int apic_is_initialized(void);
int apic_init(void);
uint32_t apic_get_id(void);
void apic_send_eoi(void);
void apic_send_ipi(uint32_t apic_id, uint32_t icr_low);

#endif /* APIC_H */
