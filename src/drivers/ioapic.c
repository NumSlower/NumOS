/* NumOS — ioapic.c — I/O APIC */
#include "drivers/ioapic.h"
#include "drivers/acpi.h"
#include "kernel/types.h"

static uint64_t ioapic_base = 0;

static inline uint32_t ioapic_read(uint8_t reg) {
    *(volatile uint32_t*)(ioapic_base + 0x00) = reg;
    return *(volatile uint32_t*)(ioapic_base + 0x10);
}

static inline void ioapic_write(uint8_t reg, uint32_t val) {
    *(volatile uint32_t*)(ioapic_base + 0x00) = reg;
    *(volatile uint32_t*)(ioapic_base + 0x10) = val;
}

bool ioapic_init(void) {
    ioapic_base = acpi_get_ioapic_base();
    if (!ioapic_base) return false;

    uint32_t ver = ioapic_read(0x01);
    uint32_t max_entries = ((ver >> 16) & 0xFF) + 1;

    for (uint32_t i = 0; i < max_entries; i++) {
        uint64_t entry = ioapic_get_entry(i);
        entry |= (1ULL << 16); // mask
        ioapic_set_entry(i, entry);
    }

    return true;
}

void ioapic_set_entry(uint8_t index, uint64_t entry) {
    uint8_t reg = 0x10 + (index * 2);
    ioapic_write(reg, (uint32_t)entry);
    ioapic_write(reg + 1, (uint32_t)(entry >> 32));
}

uint64_t ioapic_get_entry(uint8_t index) {
    uint8_t reg = 0x10 + (index * 2);
    uint32_t low = ioapic_read(reg);
    uint32_t high = ioapic_read(reg + 1);
    return ((uint64_t)high << 32) | low;
}

void ioapic_mask_irq(uint8_t irq) {
    uint64_t entry = ioapic_get_entry(irq);
    entry |= (1ULL << 16);
    ioapic_set_entry(irq, entry);
}

void ioapic_unmask_irq(uint8_t irq) {
    uint64_t entry = ioapic_get_entry(irq);
    entry &= ~(1ULL << 16);
    ioapic_set_entry(irq, entry);
}

void ioapic_route_irq(uint8_t irq, uint8_t vector, uint8_t dest_apic_id) {
    uint64_t entry = vector;
    // Fixed delivery mode (000), Physical Destination mode (0), Active High (0), Edge triggered (0), unmasked
    entry |= ((uint64_t)dest_apic_id << 56);
    ioapic_set_entry(irq, entry);
}
