/* NumOS — pci.c — PCI configuration-space discovery
 *
 * Enumerates bus/device/function through I/O ports 0xCF8/0xCFC (config
 * mechanism 1), recursing into PCI-to-PCI bridges via their secondary bus
 * number. Devices are stored with identity, class, IRQ routing fields and
 * decoded BAR bases. Read-only discovery: BAR sizes are not measured here.
 */
#include "drivers/pci.h"
#include "libs/io.h"
#include "libs/types.h"

static struct pci_device devices[PCI_MAX_STORED];
static uint32_t device_count = 0;
static uint32_t buses_scanned = 0;
static bool scan_overflow = false;

static uint32_t make_config_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    return 0x80000000u
         | ((uint32_t)bus << 16)
         | ((uint32_t)device << 11)
         | ((uint32_t)function << 8)
         | ((uint32_t)(offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, make_config_address(bus, device, function, offset));
    return inl(PCI_CONFIG_DATA);
}

static uint16_t pci_read_vendor(uint8_t bus, uint8_t device, uint8_t function) {
    uint32_t v = pci_config_read32(bus, device, function, 0x00);
    return (uint16_t)(v & 0xFFFF);
}

static void pci_record_device(uint8_t bus, uint8_t device, uint8_t function) {
    if (device_count >= PCI_MAX_STORED) {
        scan_overflow = true;
        return;
    }

    uint32_t r0 = pci_config_read32(bus, device, function, 0x00);
    uint32_t rc = pci_config_read32(bus, device, function, 0x08);
    uint32_t rh = pci_config_read32(bus, device, function, 0x0C);
    uint32_t ri = pci_config_read32(bus, device, function, 0x3C);

    struct pci_device* d = &devices[device_count];
    d->bus = bus;
    d->device = device;
    d->function = function;
    d->vendor_id = (uint16_t)(r0 & 0xFFFF);
    d->device_id = (uint16_t)(r0 >> 16);
    d->revision_id = (uint8_t)(rc & 0xFF);
    d->prog_if = (uint8_t)((rc >> 8) & 0xFF);
    d->subclass = (uint8_t)((rc >> 16) & 0xFF);
    d->class_code = (uint8_t)((rc >> 24) & 0xFF);
    d->header_type = (uint8_t)((rh >> 16) & 0x7F);
    d->irq_pin = (uint8_t)(ri & 0xFF);
    d->irq_line = (uint8_t)((ri >> 8) & 0xFF);

    for (uint8_t i = 0; i < 6; i++) {
        uint32_t bar = pci_config_read32(bus, device, function, 0x10 + i * 4);
        if (bar & 0x1u) {
            d->bar_type[i] = PCI_BAR_PIO;
            d->bar_base[i] = bar & 0xFFFFFFFCu;
        } else {
            uint8_t mem_type = (uint8_t)((bar >> 1) & 0x3u);
            d->bar_type[i] = (mem_type == 2) ? PCI_BAR_MMIO64 : PCI_BAR_MMIO32;
            d->bar_base[i] = bar & 0xFFFFFFF0u;
        }
        if (d->bar_type[i] == PCI_BAR_MMIO64) i++;
    }

    device_count++;
}

static void pci_scan_bus(uint8_t bus);

static void pci_scan_slot(uint8_t bus, uint8_t device) {
    uint16_t vendor = pci_read_vendor(bus, device, 0);
    if (vendor == 0xFFFF) return;

    uint32_t rh = pci_config_read32(bus, device, 0, 0x0C);
    bool multifunction = ((rh >> 16) & PCI_MULTIFUNCTION_BIT) != 0;
    uint8_t max_fn = multifunction ? PCI_MAX_FUNCTIONS : 1;

    for (uint8_t fn = 0; fn < max_fn; fn++) {
        if (pci_read_vendor(bus, device, fn) == 0xFFFF) continue;

        uint32_t rc = pci_config_read32(bus, device, fn, 0x08);
        uint8_t class_code = (uint8_t)((rc >> 24) & 0xFF);
        uint8_t subclass = (uint8_t)((rc >> 16) & 0xFF);
        uint32_t rh_fn = pci_config_read32(bus, device, fn, 0x0C);
        uint8_t header_type = (uint8_t)((rh_fn >> 16) & 0x7F);

        pci_record_device(bus, device, fn);

        if (header_type == PCI_HEADER_TYPE_BRIDGE && class_code == PCI_CLASS_BRIDGE && subclass == 0x04) {
            uint32_t rb = pci_config_read32(bus, device, fn, 0x18);
            uint8_t secondary = (uint8_t)((rb >> 8) & 0xFF);
            if (secondary != 0 && secondary != bus) {
                pci_scan_bus(secondary);
            }
        }
    }
}

static void pci_scan_bus(uint8_t bus) {
    if (buses_scanned >= PCI_MAX_BUSES) {
        scan_overflow = true;
        return;
    }
    buses_scanned++;

    for (uint8_t dev = 0; dev < PCI_MAX_DEVICES; dev++) {
        pci_scan_slot(bus, dev);
    }
}

const char* pci_class_name(uint8_t class_code) {
    switch (class_code) {
        case PCI_CLASS_UNCLASSIFIED:  return "Unclassified";
        case PCI_CLASS_STORAGE:       return "Storage";
        case PCI_CLASS_NETWORK:       return "Network";
        case PCI_CLASS_DISPLAY:       return "Display";
        case PCI_CLASS_MULTIMEDIA:    return "Multimedia";
        case PCI_CLASS_MEMORY:        return "Memory";
        case PCI_CLASS_BRIDGE:        return "Bridge";
        case PCI_CLASS_COMMUNICATION: return "Communication";
        case PCI_CLASS_GENERIC:       return "Generic";
        case PCI_CLASS_INPUT:         return "Input";
        case PCI_CLASS_DOCKING:       return "Docking";
        case PCI_CLASS_PROCESSOR:     return "Processor";
        case PCI_CLASS_SERIAL_BUS:    return "Serial Bus";
        default:                      return "Other";
    }
}

bool pci_init(void) {
    device_count = 0;
    buses_scanned = 0;
    scan_overflow = false;

    if (pci_read_vendor(0, 0, 0) == 0xFFFF) {
        return false;
    }

    pci_scan_bus(0);
    return device_count > 0;
}

uint32_t pci_get_device_count(void) {
    return device_count;
}

uint32_t pci_get_bus_count(void) {
    return buses_scanned;
}

const struct pci_device* pci_get_device(uint32_t index) {
    if (index >= device_count) return NULL;
    return &devices[index];
}

const struct pci_device* pci_find_by_class(uint8_t class_code, uint8_t subclass, uint32_t index) {
    uint32_t seen = 0;
    for (uint32_t i = 0; i < device_count; i++) {
        if (devices[i].class_code == class_code && devices[i].subclass == subclass) {
            if (seen == index) return &devices[i];
            seen++;
        }
    }
    return NULL;
}
