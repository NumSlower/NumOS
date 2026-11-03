/* NumOS — acpi.c — ACPI table parser */
#include "drivers/acpi.h"
#include "kernel/types.h"

static struct acpi_rsdp* rsdp;
static struct acpi_sdt_header* rsdt_xsdt;
static struct acpi_madt* madt;

static uint64_t lapic_base = 0;
static uint64_t ioapic_base = 0;
static uint8_t ioapic_id = 0;
static uint32_t ioapic_gsi_base = 0;
static uint8_t cpu_count = 0;

static struct acpi_iso isos[16];
static uint8_t iso_count = 0;

static bool acpi_validate_checksum(void* ptr, size_t size) {
    uint8_t sum = 0;
    uint8_t* bytes = (uint8_t*)ptr;
    for (size_t i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum == 0;
}

static bool acpi_sig_match(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) return false;
    }
    return true;
}

void* acpi_find_table(const char sig[4]) {
    if (!rsdt_xsdt) return NULL;

    bool is_xsdt = (rsdp->revision >= 2);
    size_t entries_count = (rsdt_xsdt->length - sizeof(struct acpi_sdt_header)) / (is_xsdt ? 8 : 4);

    uint8_t* entries = (uint8_t*)rsdt_xsdt + sizeof(struct acpi_sdt_header);

    for (size_t i = 0; i < entries_count; i++) {
        struct acpi_sdt_header* header;
        if (is_xsdt) {
            header = (struct acpi_sdt_header*)(uintptr_t)*(uint64_t*)(entries + i * 8);
        } else {
            header = (struct acpi_sdt_header*)(uintptr_t)*(uint32_t*)(entries + i * 4);
        }

        if (header && acpi_sig_match(header->signature, sig, 4)) {
            if (acpi_validate_checksum(header, header->length)) {
                return header;
            }
        }
    }
    return NULL;
}

bool acpi_init(void* rsdp_ptr) {
    if (!rsdp_ptr) return false;
    rsdp = (struct acpi_rsdp*)rsdp_ptr;

    if (!acpi_validate_checksum(rsdp, 20)) return false;
    if (rsdp->revision >= 2 && !acpi_validate_checksum(rsdp, rsdp->length)) return false;

    if (rsdp->revision >= 2 && rsdp->xsdt_address) {
        rsdt_xsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->xsdt_address;
    } else {
        rsdt_xsdt = (struct acpi_sdt_header*)(uintptr_t)rsdp->rsdt_address;
    }

    if (!acpi_validate_checksum(rsdt_xsdt, rsdt_xsdt->length)) return false;

    madt = (struct acpi_madt*)acpi_find_table("APIC");
    if (!madt) return false;

    lapic_base = madt->local_apic_address;

    uint8_t* ptr = (uint8_t*)madt + sizeof(struct acpi_madt);
    uint8_t* end = (uint8_t*)madt + madt->header.length;

    while (ptr < end) {
        struct acpi_madt_entry_header* hdr = (struct acpi_madt_entry_header*)ptr;
        if (hdr->length == 0) break;

        if (hdr->type == 0) { // LAPIC
            cpu_count++;
        } else if (hdr->type == 1) { // IOAPIC
            struct acpi_madt_ioapic* ioapic = (struct acpi_madt_ioapic*)hdr;
            ioapic_base = ioapic->ioapic_addr;
            ioapic_id = ioapic->ioapic_id;
            ioapic_gsi_base = ioapic->gsi_base;
        } else if (hdr->type == 2) { // ISO
            struct acpi_madt_iso* iso = (struct acpi_madt_iso*)hdr;
            if (iso_count < 16) {
                isos[iso_count].bus = iso->bus;
                isos[iso_count].source = iso->source;
                isos[iso_count].gsi = iso->gsi;
                isos[iso_count].flags = iso->flags;
                iso_count++;
            }
        } else if (hdr->type == 5) { // LAPIC Override
            struct acpi_madt_lapic_override* over = (struct acpi_madt_lapic_override*)hdr;
            lapic_base = over->address;
        }

        ptr += hdr->length;
    }

    return true;
}

uint64_t acpi_get_lapic_base(void) { return lapic_base; }
uint64_t acpi_get_ioapic_base(void) { return ioapic_base; }
uint8_t acpi_get_ioapic_id(void) { return ioapic_id; }
uint32_t acpi_get_ioapic_gsi_base(void) { return ioapic_gsi_base; }
uint8_t acpi_get_cpu_count(void) { return cpu_count; }
struct acpi_iso* acpi_get_isos(uint8_t* count) {
    if (count) *count = iso_count;
    return isos;
}
