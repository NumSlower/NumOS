/*
 * device.c - NumOS Hardware and VM Device Detection
 *
 * Detection strategy
 * ==================
 * 1. Hypervisor fingerprint via CPUID (leaf 0 and 0x40000000).
 *    Identifies QEMU, KVM, VMware, VirtualBox, Hyper-V, Xen, Parallels.
 *
 * 2. PS/2 controller probe for keyboard and mouse.
 *    Reads the 8042 status register without disturbing driver state.
 *
 * 3. Full PCI bus scan (bus 0-7, slot 0-31, function 0-7).
 *    Each valid device is classified by PCI class/subclass into one of the
 *    device_type_t categories.  Well-known vendor/device ID pairs receive
 *    descriptive names; everything else gets a generic "PCI <class>" label.
 *
 * All detections are read-only probes: nothing is initialised, enabled, or
 * configured by this driver.  Actual driver init remains in the dedicated
 * driver files (keyboard.c, ata.c, vga.c, etc.).
 *
 * VM compatibility notes
 * ======================
 * The kernel runs correctly on the following platforms without any changes:
 *
 *   QEMU + KVM    Already supported.  IDE disk and VGA text mode work out
 *                 of the box.  Virtio devices are detected as PCI but not
 *                 driven at this stage.
 *
 *   VirtualBox    Use IDE controller (PIIX3/PIIX4) and VGA adapter.
 *                 In VM settings: System -> Enable I/O APIC off (simpler),
 *                 Display -> VBoxVGA (not VMSVGA or VBoxSVGA).
 *                 Attach NumOS.iso as the optical drive only. The ISO
 *                 already carries disk.img as a multiboot2 ramdisk module.
 *
 *   VMware        Use the IDE adapter (not PVSCSI) and VGA display.
 *                 In .vmx: scsi0.present = "FALSE", ide0:0.present = "TRUE".
 *                 SVGA acceleration is detected but not used.
 *
 *   Hyper-V       Use Generation 1 VM (BIOS, not UEFI) with IDE controller.
 *                 Disable Secure Boot.  VGA legacy video works in Gen 1.
 *
 *   Bochs / BIOS  Native VGA text mode and IDE PIO both work.
 *
 * Common requirement for all: boot via GRUB multiboot2 (already done).
 */

#include "drivers/device.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"

/* =========================================================================
 * Module state
 * ======================================================================= */

static struct device_entry  device_table[DEVICE_MAX_ENTRIES];
static int                  device_count_val = 0;
static struct hypervisor_info hv_info;

static struct device_entry *register_device(const char *name,
                                            device_type_t type,
                                            device_bus_t bus);

static void set_hypervisor_info(hypervisor_id_t id,
                                const char *vendor,
                                const char *name,
                                int detected) {
    memset(&hv_info, 0, sizeof(hv_info));
    hv_info.id = id;
    hv_info.detected = detected;

    if (vendor) {
        size_t vendor_len = strlen(vendor);
        if (vendor_len > sizeof(hv_info.vendor_string) - 1) {
            vendor_len = sizeof(hv_info.vendor_string) - 1;
        }
        memcpy(hv_info.vendor_string, vendor, vendor_len);
        hv_info.vendor_string[vendor_len] = '\0';
    }

    if (name) {
        size_t name_len = strlen(name);
        if (name_len > sizeof(hv_info.name) - 1) {
            name_len = sizeof(hv_info.name) - 1;
        }
        memcpy(hv_info.name, name, name_len);
        hv_info.name[name_len] = '\0';
    }
}

static void register_hypervisor_device(void) {
    for (int i = 0; i < device_count_val; i++) {
        if (device_table[i].type == DEVICE_TYPE_HYPERVISOR) return;
    }

    struct device_entry *e = register_device(hv_info.name,
                                             DEVICE_TYPE_HYPERVISOR,
                                             DEVICE_BUS_VIRTUAL);
    (void)e;
}

static void detect_hypervisor_from_pci_fallback(void) {
    if (hv_info.detected) return;
    if (!device_is_virtualbox_pci()) return;

    set_hypervisor_info(HYPERVISOR_VIRTUALBOX,
                        "VBoxVBoxVBox",
                        "VirtualBox",
                        1);
    register_hypervisor_device();
}

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

/*
 * register_device - append a new entry to the device table.
 * Returns a pointer to the entry so callers can fill PCI fields, or NULL
 * when the table is full.
 */
static struct device_entry *register_device(const char   *name,
                                            device_type_t type,
                                            device_bus_t  bus) {
    if (device_count_val >= DEVICE_MAX_ENTRIES) return NULL;

    struct device_entry *e = &device_table[device_count_val++];
    memset(e, 0, sizeof(*e));

    /* Safe name copy */
    int i = 0;
    while (name[i] && i < 47) { e->name[i] = name[i]; i++; }
    e->name[i] = '\0';

    e->type    = type;
    e->bus     = bus;
    e->present = 1;
    return e;
}

/* =========================================================================
 * PCI configuration space access
 * ======================================================================= */

/*
 * pci_make_address - build the 32-bit PCI config address register value.
 */
static inline uint32_t pci_make_address(uint8_t bus, uint8_t slot,
                                        uint8_t func, uint8_t offset) {
    return (uint32_t)(1u << 31)
         | ((uint32_t)bus   << 16)
         | ((uint32_t)(slot & 0x1F) << 11)
         | ((uint32_t)(func & 0x07) << 8)
         | ((uint32_t)(offset & 0xFC));
}

uint32_t pci_config_read32(uint8_t bus, uint8_t slot,
                           uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return inl(PCI_CONFIG_DATA);
}

uint8_t pci_config_read8(uint8_t bus, uint8_t slot,
                         uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return (uint8_t)((inl(PCI_CONFIG_DATA) >> ((offset & 3) * 8)) & 0xFF);
}

uint16_t pci_config_read16(uint8_t bus, uint8_t slot,
                           uint8_t func, uint8_t offset) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    return (uint16_t)((inl(PCI_CONFIG_DATA) >> ((offset & 2) * 8)) & 0xFFFF);
}

void pci_config_write32(uint8_t bus, uint8_t slot,
                        uint8_t func, uint8_t offset, uint32_t value) {
    outl(PCI_CONFIG_ADDRESS, pci_make_address(bus, slot, func, offset));
    outl(PCI_CONFIG_DATA, value);
}

void pci_config_write16(uint8_t bus, uint8_t slot,
                        uint8_t func, uint8_t offset, uint16_t value) {
    uint32_t current = pci_config_read32(bus, slot, func, offset);
    uint32_t shift = (uint32_t)((offset & 2) * 8);
    current &= ~(0xFFFFu << shift);
    current |= ((uint32_t)value << shift);
    pci_config_write32(bus, slot, func, offset, current);
}

void pci_config_write8(uint8_t bus, uint8_t slot,
                       uint8_t func, uint8_t offset, uint8_t value) {
    uint32_t current = pci_config_read32(bus, slot, func, offset);
    uint32_t shift = (uint32_t)((offset & 3) * 8);
    current &= ~(0xFFu << shift);
    current |= ((uint32_t)value << shift);
    pci_config_write32(bus, slot, func, offset, current);
}

/* =========================================================================
 * Hypervisor detection
 * =========================================================================
 *
 * CPUID leaf 1, ECX bit 31 (Hypervisor Present Bit) tells us a VMM is active.
 * Leaf 0x40000000 returns the hypervisor vendor string in EBX:ECX:EDX
 * (12 bytes, not null-terminated by the hardware).
 *
 * Known vendor strings:
 *   "KVMKVMKVM\0\0\0" -> KVM (also used by QEMU+KVM)
 *   "TCGTCGTCGTCG"    -> QEMU TCG (software emulation, no KVM)
 *   "VMwareVMware"    -> VMware
 *   "VBoxVBoxVBox"    -> VirtualBox
 *   "Microsoft Hv"   -> Hyper-V
 *   "XenVMMXenVMM"   -> Xen
 *   "prl hyperv  "   -> Parallels
 */
static void detect_hypervisor(void) {
    set_hypervisor_info(HYPERVISOR_NONE, NULL, "Bare metal", 0);

    uint32_t eax, ebx, ecx, edx;

    /* Step 1: check Hypervisor Present Bit in CPUID leaf 1 */
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );

    if (!(ecx & (1u << 31))) {
        return;
    }

    /* Step 2: read vendor string from leaf 0x40000000 */
    __asm__ volatile(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x40000000)
    );

    /* Pack the 12 vendor bytes in order: EBX, ECX, EDX */
    uint32_t words[3] = { ebx, ecx, edx };
    memcpy(hv_info.vendor_string, words, 12);
    hv_info.vendor_string[12] = '\0';

    /* Step 3: identify the platform */
    const char *vs = hv_info.vendor_string;

    if (memcmp(vs, "KVMKVMKVM", 9) == 0) {
        set_hypervisor_info(HYPERVISOR_KVM, hv_info.vendor_string,
                            "KVM (QEMU/KVM)", 1);
    } else if (memcmp(vs, "TCGTCGTCGTCG", 12) == 0) {
        set_hypervisor_info(HYPERVISOR_QEMU, hv_info.vendor_string,
                            "QEMU (TCG)", 1);
    } else if (memcmp(vs, "VMwareVMware", 12) == 0) {
        set_hypervisor_info(HYPERVISOR_VMWARE, hv_info.vendor_string,
                            "VMware", 1);
    } else if (memcmp(vs, "VBoxVBoxVBox", 12) == 0) {
        set_hypervisor_info(HYPERVISOR_VIRTUALBOX, hv_info.vendor_string,
                            "VirtualBox", 1);
    } else if (memcmp(vs, "Microsoft Hv", 12) == 0) {
        set_hypervisor_info(HYPERVISOR_HYPERV, hv_info.vendor_string,
                            "Microsoft Hyper-V", 1);
    } else if (memcmp(vs, "XenVMMXenVMM", 12) == 0) {
        set_hypervisor_info(HYPERVISOR_XEN, hv_info.vendor_string,
                            "Xen", 1);
    } else if (memcmp(vs, "prl hyperv", 10) == 0) {
        set_hypervisor_info(HYPERVISOR_PARALLELS, hv_info.vendor_string,
                            "Parallels", 1);
    } else {
        set_hypervisor_info(HYPERVISOR_UNKNOWN, hv_info.vendor_string,
                            "Unknown Hypervisor", 1);
    }

    register_hypervisor_device();
}

/* =========================================================================
 * PS/2 controller probe
 * =========================================================================
 *
 * Reading the 8042 status register (0x64) is non-destructive.
 * Bit 2 (System Flag) should be set after POST; if it is clear the 8042
 * is absent or not yet initialised.
 *
 * Mouse presence: try to enable the auxiliary (mouse) port via 0xA8
 * and check whether the 8042 acknowledges it (no timeout on OBF).
 * We restore the mask state with 0xA7 (disable aux) right after.
 */
static void detect_ps2_devices(void) {
    uint8_t status = inb(0x64);

    /* Bit 2 must be set (controller passed POST self-test) */
    if (!(status & 0x04)) return;

    /* Keyboard: port 0x60 data is present after any key press, but we
     * detect the controller itself rather than a buffered keystroke.
     * The keyboard driver (keyboard.c) already initialises IRQ 1, so we
     * just register it as present here.                                   */
    register_device("PS/2 Keyboard", DEVICE_TYPE_KEYBOARD, DEVICE_BUS_PS2);

    /* Mouse: send 0xA8 (Enable Auxiliary Device) to the command register.
     * If the 8042 supports a second port it will not set bit 5 (AuxClock)
     * in the subsequent status read.  Restore with 0xA7 immediately.      */

    /* Flush any pending output buffer byte first */
    if (inb(0x64) & 0x01) { (void)inb(0x60); }

    outb(0x64, 0xA8);                   /* enable aux port     */
    for (volatile int i = 0; i < 1000; i++);
    uint8_t s2 = inb(0x64);
    outb(0x64, 0xA7);                   /* disable aux port    */

    /* Bit 5 = AuxClock disabled; clear means the port responded */
    if (!(s2 & 0x20)) {
        register_device("PS/2 Mouse", DEVICE_TYPE_MOUSE, DEVICE_BUS_PS2);
    }
}

/* =========================================================================
 * PCI device name lookup
 * =========================================================================
 *
 * Returns a human-readable name for well-known vendor/device combinations.
 * Falls back to a generic class description for anything unrecognised.
 */
static const char *pci_vendor_name(uint16_t vendor, uint16_t device,
                                   uint8_t class, uint8_t subclass,
                                   uint8_t prog_if) {
    /* ---- Storage controllers ---- */
    if (class == 0x01) {
        if (subclass == 0x01) return "IDE Controller";
        if (subclass == 0x06) return "SATA Controller (AHCI)";
        if (subclass == 0x08) return "NVMe Controller";
        return "Storage Controller";
    }

    /* ---- Network adapters ---- */
    if (class == 0x02) {
        if (vendor == 0x8086 && device == 0x100E) return "Intel e1000 NIC";
        if (vendor == 0x1022 && device == 0x2000) return "AMD PCnet NIC";
        if (vendor == 0x1AF4 && device == 0x1000) return "Virtio NIC";
        if (vendor == 0x10EC && device == 0x8139) return "Realtek RTL8139";
        if (vendor == 0x10EC && device == 0x8168) return "Realtek RTL8168";
        return "Network Adapter";
    }

    /* ---- Display / GPU ---- */
    if (class == 0x03) {
        if (vendor == 0x1234 && device == 0x1111) return "QEMU/Bochs VGA";
        if (vendor == 0x80EE && device == 0xBEEF) return "VirtualBox VGA";
        if (vendor == 0x15AD && device == 0x0405) return "VMware SVGA II";
        if (vendor == 0x15AD && device == 0x0406) return "VMware SVGA 3D";
        if (vendor == 0x10DE) return "NVIDIA GPU";
        if (vendor == 0x1002) return "AMD/ATI GPU";
        if (vendor == 0x8086) return "Intel Integrated GPU";
        if (vendor == 0x1AF4 && device == 0x1050) return "Virtio GPU";
        return "Display Adapter";
    }

    /* ---- Multimedia / audio ---- */
    if (class == 0x04) {
        if (vendor == 0x8086 && device == 0x2668) return "Intel HDA Audio";
        if (vendor == 0x8086 && device == 0x293E) return "Intel HDA Audio";
        if (vendor == 0x1022 && device == 0x780D) return "AMD HDA Audio";
        if (vendor == 0x1274 && device == 0x1371) return "Creative ES1371";
        if (vendor == 0x1AF4 && device == 0x1059) return "Virtio Sound";
        return "Audio Controller";
    }

    /* ---- Bridges ---- */
    if (class == 0x06) {
        if (subclass == 0x00) return "Host Bridge";
        if (subclass == 0x01) return "ISA Bridge";
        if (subclass == 0x04) return "PCI-PCI Bridge";
        return "System Bridge";
    }

    /* ---- USB controllers ---- */
    if (class == 0x0C && subclass == 0x03) {
        if (prog_if == 0x00) return "USB UHCI Controller";
        if (prog_if == 0x10) return "USB OHCI Controller";
        if (prog_if == 0x20) return "USB EHCI Controller";
        if (prog_if == 0x30) return "USB xHCI Controller";
        return "USB Controller";
    }

    /* ---- Input ---- */
    if (class == 0x09) {
        if (subclass == 0x00) return "PCI Keyboard";
        if (subclass == 0x02) return "PCI Mouse";
        return "Input Device";
    }

    /* ---- Processing accelerators (NPU / AI cards) ---- */
    if (class == 0x12) {
        if (vendor == 0x1D78) return "Neural Processing Unit";
        return "Processing Accelerator / NPU";
    }

    /* ---- Miscellaneous processor ---- */
    if (class == 0x0B) {
        return "Processor / Coprocessor";
    }

    /* ---- Comm controllers ---- */
    if (class == 0x07) {
        return "Communication Controller";
    }

    /* ---- ACPI / system mgmt ---- */
    if (class == 0x08) {
        return "System Peripheral";
    }

    /* Vendor-specific virtio catch-all */
    if (vendor == 0x1AF4) return "Virtio Device";

    return "PCI Device";
}

/*
 * pci_class_to_type - map a PCI class/subclass to a device_type_t.
 */
static device_type_t pci_class_to_type(uint8_t class, uint8_t subclass,
                                       uint8_t prog_if) {
    switch (class) {
        case PCI_CLASS_STORAGE:     return DEVICE_TYPE_STORAGE;
        case PCI_CLASS_NETWORK:     return DEVICE_TYPE_NETWORK;
        case PCI_CLASS_DISPLAY:     return DEVICE_TYPE_GPU;
        case PCI_CLASS_MULTIMEDIA:  return DEVICE_TYPE_AUDIO;
        case PCI_CLASS_BRIDGE:      return DEVICE_TYPE_BRIDGE;
        case PCI_CLASS_COMM:        return DEVICE_TYPE_SERIAL;
        case PCI_CLASS_INPUT:
            if (subclass == 0x00) return DEVICE_TYPE_KEYBOARD;
            if (subclass == 0x02) return DEVICE_TYPE_MOUSE;
            return DEVICE_TYPE_UNKNOWN;
        case PCI_CLASS_SERIAL_BUS:
            if (subclass == 0x03) return DEVICE_TYPE_USB;
            return DEVICE_TYPE_UNKNOWN;
        case PCI_CLASS_ACCEL:       return DEVICE_TYPE_NPU;
        case 0x08:                  return DEVICE_TYPE_SYSTEM;
        default:                    return DEVICE_TYPE_UNKNOWN;
    }
    (void)prog_if;
}

/* =========================================================================
 * PCI bus scan
 * ========================================================================= */

/*
 * scan_pci_function - read and register one PCI function.
 */
static void scan_pci_function(uint8_t bus, uint8_t slot, uint8_t func) {
    uint32_t id = pci_config_read32(bus, slot, func, 0x00);
    if (id == 0xFFFFFFFF || id == 0x00000000) return;

    uint16_t vendor   = (uint16_t)(id & 0xFFFF);
    uint16_t device   = (uint16_t)(id >> 16);
    uint32_t class_r  = pci_config_read32(bus, slot, func, 0x08);
    uint8_t  revision = (uint8_t)(class_r & 0xFF);
    uint8_t  prog_if  = (uint8_t)((class_r >>  8) & 0xFF);
    uint8_t  subclass = (uint8_t)((class_r >> 16) & 0xFF);
    uint8_t  class    = (uint8_t)((class_r >> 24) & 0xFF);

    /* Skip empty class 0xFF entries */
    if (class == 0xFF) return;

    const char    *name  = pci_vendor_name(vendor, device,
                                           class, subclass, prog_if);
    device_type_t  type  = pci_class_to_type(class, subclass, prog_if);

    struct device_entry *e = register_device(name, type, DEVICE_BUS_PCI);
    if (!e) return;

    e->vendor_id    = vendor;
    e->device_id    = device;
    e->pci_bus      = bus;
    e->pci_slot     = slot;
    e->pci_func     = func;
    e->pci_class    = class;
    e->pci_subclass = subclass;
    e->pci_prog_if  = prog_if;
    e->pci_revision = revision;

    /* Cache first two BARs */
    e->pci_bar[0] = pci_config_read32(bus, slot, func, 0x10);
    e->pci_bar[1] = pci_config_read32(bus, slot, func, 0x14);

    /* Read interrupt line from config space offset 0x3C */
    e->pci_irq = pci_config_read8(bus, slot, func, 0x3C);
}

/*
 * scan_pci_slot - probe all functions on one slot.
 * If the device is multi-function (header type bit 7) scan all 8 functions.
 */
static void scan_pci_slot(uint8_t bus, uint8_t slot) {
    uint32_t id = pci_config_read32(bus, slot, 0, 0x00);
    if ((id & 0xFFFF) == PCI_VENDOR_INVALID) return;

    scan_pci_function(bus, slot, 0);

    uint8_t header = pci_config_read8(bus, slot, 0, 0x0E);
    if (header & 0x80) {
        for (uint8_t func = 1; func < 8; func++) {
            uint32_t fid = pci_config_read32(bus, slot, func, 0x00);
            if ((fid & 0xFFFF) != PCI_VENDOR_INVALID) {
                scan_pci_function(bus, slot, func);
            }
        }
    }
}

/*
 * scan_pci_bus - scan all 32 slots on one bus.
 */
static void scan_pci_bus(uint8_t bus) {
    for (uint8_t slot = 0; slot < 32; slot++) {
        scan_pci_slot(bus, slot);
    }
}

/*
 * detect_pci_devices - scan buses 0-7.
 * Most systems (and all common VMs) put all devices on bus 0.
 * Buses 1-7 cover typical PCIe root complexes.
 */
static void detect_pci_devices(void) {
    for (uint8_t bus = 0; bus < 8; bus++) {
        scan_pci_bus(bus);
    }
}

/* =========================================================================
 * Public API
 * ========================================================================= */

void device_init(void) {
    memset(device_table, 0, sizeof(device_table));
    device_count_val = 0;
    memset(&hv_info,     0, sizeof(hv_info));

    vga_writestring("Device: Detecting hypervisor...\n");
    detect_hypervisor();

    vga_writestring("Device: Probing PS/2 controller...\n");
    detect_ps2_devices();

    vga_writestring("Device: Scanning PCI bus...\n");
    detect_pci_devices();
    detect_hypervisor_from_pci_fallback();

    vga_writestring("Device: Found ");
    print_dec((uint64_t)device_count_val);
    vga_writestring(" device(s)\n");
}

int device_get_by_type(device_type_t type,
                       struct device_entry **out, int max) {
    int found = 0;
    for (int i = 0; i < device_count_val && found < max; i++) {
        if (device_table[i].type == type) {
            out[found++] = &device_table[i];
        }
    }
    return found;
}

const struct hypervisor_info *device_get_hypervisor(void) {
    return &hv_info;
}

/*
 * device_print_all - write a formatted device list to VGA.
 * Groups devices by type and shows PCI coordinates for PCI devices.
 */
void device_print_all(void) {
    static const char *type_names[] = {
        "Unknown      ",
        "Keyboard     ",
        "Mouse        ",
        "GPU          ",
        "USB          ",
        "Storage      ",
        "Network      ",
        "Audio        ",
        "NPU          ",
        "Bridge       ",
        "Serial       ",
        "Timer        ",
        "System       ",
        "Hypervisor   ",
    };

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("\n=== Detected Devices ===\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    /* Hypervisor line first */
    vga_writestring("  Platform  : ");
    if (hv_info.detected) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
        vga_writestring(hv_info.name);
        vga_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
        vga_writestring(" [");
        vga_writestring(hv_info.vendor_string);
        vga_writestring("]");
    } else {
        vga_writestring("Bare metal");
    }
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));

    vga_writestring("\n  TYPE           NAME\n");
    vga_writestring("  ------------- ---------------------------------------\n");

    for (int i = 0; i < device_count_val; i++) {
        struct device_entry *e = &device_table[i];

        /* Skip the hypervisor entry; already printed above */
        if (e->type == DEVICE_TYPE_HYPERVISOR) continue;

        uint8_t t = (uint8_t)e->type;
        if (t > 13) t = 0;

        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        vga_writestring("  ");
        vga_writestring(type_names[t]);
        vga_writestring(" ");

        /* Color-code by type */
        switch (e->type) {
            case DEVICE_TYPE_GPU:     vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK)); break;
            case DEVICE_TYPE_STORAGE: vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BROWN,   VGA_COLOR_BLACK)); break;
            case DEVICE_TYPE_NETWORK: vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN,    VGA_COLOR_BLACK)); break;
            case DEVICE_TYPE_USB:     vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_BLUE,    VGA_COLOR_BLACK)); break;
            case DEVICE_TYPE_AUDIO:   vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN,   VGA_COLOR_BLACK)); break;
            case DEVICE_TYPE_NPU:     vga_setcolor(vga_entry_color(VGA_COLOR_WHITE,         VGA_COLOR_BLACK)); break;
            default:                  vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY,    VGA_COLOR_BLACK)); break;
        }

        vga_writestring(e->name);

        if (e->bus == DEVICE_BUS_PCI) {
            vga_setcolor(vga_entry_color(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK));
            vga_writestring(" [");
            print_hex32((uint32_t)e->vendor_id);
            vga_writestring(":");
            print_hex32((uint32_t)e->device_id);
            vga_writestring("]");
        }

        vga_writestring("\n");
    }

    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("  ------------- ---------------------------------------\n");
    vga_writestring("  Total: ");
    print_dec((uint64_t)device_count_val);
    vga_writestring(" device(s)\n\n");
}

void device_detect_hypervisor_early(void) {
    detect_hypervisor();
    detect_hypervisor_from_pci_fallback();
}

int device_is_virtualbox_pci(void) {
    for (uint8_t bus = 0; bus < 8; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            uint32_t id = pci_config_read32(bus, slot, 0, 0x00);
            if ((id & 0xFFFF) == 0x80EE) return 1;
        }
    }
    return 0;
}
