/* NumOS — acpi.h — ACPI table parser */
#ifndef NUMOS_ACPI_H
#define NUMOS_ACPI_H

#include "libs/types.h"

struct acpi_rsdp {
    char signature[8];
    uint8_t checksum;
    char oem_id[6];
    uint8_t revision;
    uint32_t rsdt_address;
    uint32_t length;
    uint64_t xsdt_address;
    uint8_t ext_checksum;
    uint8_t reserved[3];
} __attribute__((packed));

struct acpi_sdt_header {
    char signature[4];
    uint32_t length;
    uint8_t revision;
    uint8_t checksum;
    char oem_id[6];
    char oem_table_id[8];
    uint32_t oem_revision;
    uint32_t creator_id;
    uint32_t creator_revision;
} __attribute__((packed));

struct acpi_madt {
    struct acpi_sdt_header header;
    uint32_t local_apic_address;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_entry_header {
    uint8_t type;
    uint8_t length;
} __attribute__((packed));

struct acpi_madt_lapic {
    struct acpi_madt_entry_header header;
    uint8_t acpi_id;
    uint8_t apic_id;
    uint32_t flags;
} __attribute__((packed));

struct acpi_madt_ioapic {
    struct acpi_madt_entry_header header;
    uint8_t ioapic_id;
    uint8_t reserved;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed));

struct acpi_madt_iso {
    struct acpi_madt_entry_header header;
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
} __attribute__((packed));

struct acpi_madt_lapic_override {
    struct acpi_madt_entry_header header;
    uint16_t reserved;
    uint64_t address;
} __attribute__((packed));

struct acpi_iso {
    uint8_t bus;
    uint8_t source;
    uint32_t gsi;
    uint16_t flags;
};

bool acpi_init(void* rsdp_ptr);
uint64_t acpi_get_lapic_base(void);
uint64_t acpi_get_ioapic_base(void);
uint8_t acpi_get_ioapic_id(void);
uint32_t acpi_get_ioapic_gsi_base(void);
uint8_t acpi_get_cpu_count(void);
struct acpi_iso* acpi_get_isos(uint8_t* count);
void* acpi_find_table(const char sig[4]);

#endif
