/* NumOS — pci.h — PCI configuration-space discovery */
#ifndef NUMOS_DRIVERS_PCI_H
#define NUMOS_DRIVERS_PCI_H

#include "libs/types.h"

#define PCI_CONFIG_ADDRESS 0x0CF8
#define PCI_CONFIG_DATA    0x0CFC

#define PCI_MAX_BUSES 256
#define PCI_MAX_DEVICES 32
#define PCI_MAX_FUNCTIONS 8
#define PCI_MAX_STORED 64

#define PCI_CLASS_UNCLASSIFIED  0x00
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_MEMORY        0x05
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_COMMUNICATION 0x07
#define PCI_CLASS_GENERIC       0x08
#define PCI_CLASS_INPUT         0x09
#define PCI_CLASS_DOCKING       0x0A
#define PCI_CLASS_PROCESSOR     0x0B
#define PCI_CLASS_SERIAL_BUS    0x0C

#define PCI_HEADER_TYPE_DEVICE  0x00
#define PCI_HEADER_TYPE_BRIDGE  0x01
#define PCI_MULTIFUNCTION_BIT   0x80

#define PCI_BAR_PIO     0
#define PCI_BAR_MMIO32  1
#define PCI_BAR_MMIO64  2

struct pci_device {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision_id;
    uint8_t header_type;
    uint8_t irq_pin;
    uint8_t irq_line;
    uint32_t bar_base[6];
    uint8_t bar_type[6];
};

bool pci_init(void);
uint32_t pci_get_device_count(void);
uint32_t pci_get_bus_count(void);
const struct pci_device* pci_get_device(uint32_t index);
const struct pci_device* pci_find_by_class(uint8_t class_code, uint8_t subclass, uint32_t index);
const char* pci_class_name(uint8_t class_code);
uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset);

#endif
