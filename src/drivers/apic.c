/* NumOS — apic.c — Local APIC */
#include "drivers/apic.h"
#include "drivers/acpi.h"
#include "cpu/cpuid.h"
#include "libs/types.h"
#include "libs/io.h"

static uint64_t apic_base = 0;

extern void pic_disable(void);

static inline uint32_t apic_read(uint32_t reg) {
    return *(volatile uint32_t*)(apic_base + reg);
}

static inline void apic_write(uint32_t reg, uint32_t val) {
    *(volatile uint32_t*)(apic_base + reg) = val;
}

bool apic_init(void) {
    if (!cpuid_has_apic()) return false;
    
    apic_base = acpi_get_lapic_base();
    if (!apic_base) return false;

    /* PIC remains active as fallback — not disabling it */
    /* pic_disable(); */

    apic_write(APIC_SVR, 0x1FF); // Enable APIC, spurious vector 0xFF
    apic_write(APIC_TPR, 0); // Accept all interrupts
    apic_write(APIC_LVT_ERROR, 0xFE); // Error vector 0xFE
    
    apic_write(APIC_ESR, 0);
    apic_write(APIC_ESR, 0);

    return true;
}

void apic_send_eoi(void) {
    apic_write(APIC_EOI, 0);
}

uint8_t apic_get_id(void) {
    return (uint8_t)(apic_read(APIC_ID) >> 24);
}
