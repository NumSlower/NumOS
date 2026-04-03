#include "drivers/device.h"

static struct hypervisor_info arm64_hypervisor = {
    .id = HYPERVISOR_QEMU,
    .vendor_string = "ARM64",
    .name = "QEMU virt",
    .detected = 1,
};

void device_init(void) {
}

void device_detect_hypervisor_early(void) {
}

int device_is_virtualbox_pci(void) {
    return 0;
}

int device_get_by_type(device_type_t type, struct device_entry **out, int max) {
    (void)type;
    (void)out;
    (void)max;
    return 0;
}

const struct hypervisor_info *device_get_hypervisor(void) {
    return &arm64_hypervisor;
}

void device_print_all(void) {
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot,
                           uint8_t func, uint8_t offset) {
    (void)bus;
    (void)slot;
    (void)func;
    (void)offset;
    return 0xFFFFFFFFu;
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot,
                         uint8_t func, uint8_t offset) {
    return (uint8_t)pci_config_read32(bus, slot, func, offset);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot,
                           uint8_t func, uint8_t offset) {
    return (uint16_t)pci_config_read32(bus, slot, func, offset);
}

void pci_config_write32(uint8_t bus, uint8_t slot,
                        uint8_t func, uint8_t offset, uint32_t value) {
    (void)bus;
    (void)slot;
    (void)func;
    (void)offset;
    (void)value;
}

void pci_config_write16(uint8_t bus, uint8_t slot,
                        uint8_t func, uint8_t offset, uint16_t value) {
    (void)bus;
    (void)slot;
    (void)func;
    (void)offset;
    (void)value;
}

void pci_config_write8(uint8_t bus, uint8_t slot,
                       uint8_t func, uint8_t offset, uint8_t value) {
    (void)bus;
    (void)slot;
    (void)func;
    (void)offset;
    (void)value;
}
