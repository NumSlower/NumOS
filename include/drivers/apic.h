/* NumOS — apic.h — Local APIC */
#ifndef NUMOS_APIC_H
#define NUMOS_APIC_H

#include "libs/types.h"

#define APIC_ID 0x20
#define APIC_VER 0x30
#define APIC_TPR 0x80
#define APIC_EOI 0xB0
#define APIC_SVR 0xF0
#define APIC_ESR 0x280
#define APIC_ICR_LO 0x300
#define APIC_ICR_HI 0x310
#define APIC_LVT_TIMER 0x320
#define APIC_LVT_LINT0 0x350
#define APIC_LVT_LINT1 0x360
#define APIC_LVT_ERROR 0x370
#define APIC_TIMER_ICR 0x380
#define APIC_TIMER_CCR 0x390
#define APIC_TIMER_DCR 0x3E0

bool apic_init(void);
void apic_send_eoi(void);
uint8_t apic_get_id(void);

#endif
