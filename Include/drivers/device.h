#ifndef DEVICE_H
#define DEVICE_H

#include "lib/base.h"

/* =========================================================================
 * NumOS Device Detection and Registry
 *
 * Detects hardware and virtual devices present in the system:
 *   - PS/2 keyboard and mouse
 *   - PCI bus devices (GPU, USB, audio, network, storage, NPU)
 *   - Hypervisor / VM platform (QEMU, KVM, VMware, VirtualBox, Hyper-V, Xen)
 *
 * All detected devices are stored in a flat table accessible via the
 * device_get() / device_count() API.  The kernel calls device_init()
 * once during boot after the IDT and PIC are ready.
 * ========================================================================= */

/* ---- Maximum devices tracked ------------------------------------------- */
#define DEVICE_MAX_ENTRIES  64

/* ---- Device categories -------------------------------------------------- */
typedef enum {
    DEVICE_TYPE_UNKNOWN     = 0,
    DEVICE_TYPE_KEYBOARD    = 1,
    DEVICE_TYPE_MOUSE       = 2,
    DEVICE_TYPE_GPU         = 3,
    DEVICE_TYPE_USB         = 4,
    DEVICE_TYPE_STORAGE     = 5,
    DEVICE_TYPE_NETWORK     = 6,
    DEVICE_TYPE_AUDIO       = 7,
    DEVICE_TYPE_NPU         = 8,    /* Neural / ML accelerator             */
    DEVICE_TYPE_BRIDGE      = 9,    /* PCI host/ISA/PCI-PCI bridge         */
    DEVICE_TYPE_SERIAL      = 10,   /* COM ports, etc.                     */
    DEVICE_TYPE_TIMER       = 11,
    DEVICE_TYPE_SYSTEM      = 12,   /* Chipset, ACPI, etc.                 */
    DEVICE_TYPE_HYPERVISOR  = 13,   /* Virtual machine platform            */
} device_type_t;

/* ---- Attachment bus ----------------------------------------------------- */
typedef enum {
    DEVICE_BUS_UNKNOWN  = 0,
    DEVICE_BUS_PS2      = 1,
    DEVICE_BUS_PCI      = 2,
    DEVICE_BUS_ISA      = 3,
    DEVICE_BUS_USB      = 4,
    DEVICE_BUS_VIRTUAL  = 5,   /* Hypervisor / emulated device            */
} device_bus_t;

/* ---- Hypervisor platform IDs ------------------------------------------- */
typedef enum {
    HYPERVISOR_NONE         = 0,
    HYPERVISOR_UNKNOWN      = 1,
    HYPERVISOR_QEMU         = 2,
    HYPERVISOR_KVM          = 3,
    HYPERVISOR_VMWARE       = 4,
    HYPERVISOR_VIRTUALBOX   = 5,
    HYPERVISOR_HYPERV       = 6,
    HYPERVISOR_XEN          = 7,
    HYPERVISOR_PARALLELS    = 8,
} hypervisor_id_t;

/* ---- PCI class / subclass byte pairs ------------------------------------ */
#define PCI_CLASS_STORAGE       0x01
#define PCI_CLASS_NETWORK       0x02
#define PCI_CLASS_DISPLAY       0x03
#define PCI_CLASS_MULTIMEDIA    0x04
#define PCI_CLASS_BRIDGE        0x06
#define PCI_CLASS_COMM          0x07
#define PCI_CLASS_INPUT         0x09
#define PCI_CLASS_PROCESSOR     0x0B
#define PCI_CLASS_SERIAL_BUS    0x0C
#define PCI_CLASS_ACCEL         0x12   /* Processing accelerators (NPU/AI) */
#define PCI_SUBCLASS_USB_UHCI   0x00
#define PCI_SUBCLASS_USB_OHCI   0x10
#define PCI_SUBCLASS_USB_EHCI   0x20
#define PCI_SUBCLASS_USB_XHCI   0x30

/* ---- PCI config space port I/O ----------------------------------------- */
#define PCI_CONFIG_ADDRESS  0xCF8
#define PCI_CONFIG_DATA     0xCFC
#define PCI_VENDOR_INVALID  0xFFFF

/* ---- Device descriptor -------------------------------------------------- */
struct device_entry {
    char          name[48];         /* Human-readable description           */
    device_type_t type;
    device_bus_t  bus;
    int           present;

    /* PCI fields (valid when bus == DEVICE_BUS_PCI) */
    uint16_t  vendor_id;
    uint16_t  device_id;
    uint8_t   pci_bus;
    uint8_t   pci_slot;
    uint8_t   pci_func;
    uint8_t   pci_class;
    uint8_t   pci_subclass;
    uint8_t   pci_prog_if;
    uint8_t   pci_revision;
    uint8_t   pci_irq;
    uint32_t  pci_bar[2];           /* First two BARs (cached at detection) */
};

/* ---- Hypervisor info ---------------------------------------------------- */
struct hypervisor_info {
    hypervisor_id_t id;
    char            vendor_string[13];  /* 12 printable bytes + NUL         */
    char            name[32];
    int             detected;
};

/* =========================================================================
 * Public API
 * ======================================================================= */

/* Initialise the device subsystem and populate the registry.
 * Must be called after idt_init() and pic_init() so PCI I/O is safe.     */
void device_init(void);

/* Lightweight hypervisor detection for early boot decisions. */
void device_detect_hypervisor_early(void);

/* Quick PCI scan for VirtualBox vendor ID. */
int device_is_virtualbox_pci(void);

/* Return all devices of a given type.
 * Fills out[0..max-1]; returns number actually written.                   */
int device_get_by_type(device_type_t type,
                       struct device_entry **out, int max);

/* Return hypervisor information (always valid after device_init()).        */
const struct hypervisor_info *device_get_hypervisor(void);

/* Print a formatted device summary to the VGA console.                    */
void device_print_all(void);

/* ---- PCI helpers (available to other drivers) -------------------------- */
uint32_t pci_config_read32(uint8_t bus, uint8_t slot,
                           uint8_t func, uint8_t offset);
uint8_t  pci_config_read8 (uint8_t bus, uint8_t slot,
                           uint8_t func, uint8_t offset);

#endif /* DEVICE_H */
