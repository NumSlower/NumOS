#include "drivers/usb.h"

#include "drivers/device.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"

#define PCI_COMMAND_OFFSET 0x04
#define PCI_BAR4_OFFSET    0x20
#define PCI_CMD_IO_ENABLE  0x0001
#define PCI_CMD_BUSMASTER  0x0004

#define UHCI_PORTSC_BASE   0x10
#define UHCI_PORTSC_STRIDE 0x02
#define UHCI_PORTSC_CCS    0x0001
#define UHCI_PORTSC_PE     0x0004
#define UHCI_PORTSC_LSDA   0x0100
#define UHCI_PORTSC_PR     0x0200
#define UHCI_PORTSC_OC     0x0800

struct usb_controller_state {
    struct usb_controller_info info;
    struct usb_port_info ports[USB_MAX_PORTS];
};

static struct usb_controller_state controllers[USB_MAX_CONTROLLERS];
static int controller_count_val = 0;

static uint8_t host_type_from_prog_if(uint8_t prog_if) {
    switch (prog_if) {
        case 0x00: return USB_HOST_UHCI;
        case 0x10: return USB_HOST_OHCI;
        case 0x20: return USB_HOST_EHCI;
        case 0x30: return USB_HOST_XHCI;
        default:   return USB_HOST_NONE;
    }
}

static const char *host_type_name(uint8_t host_type) {
    switch (host_type) {
        case USB_HOST_UHCI: return "UHCI";
        case USB_HOST_OHCI: return "OHCI";
        case USB_HOST_EHCI: return "EHCI";
        case USB_HOST_XHCI: return "xHCI";
        default:            return "USB";
    }
}

static void copy_name(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }

    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

static void probe_uhci_ports(struct usb_controller_state *ctl) {
    if (!ctl || ctl->info.io_base == 0) return;

    ctl->info.port_count = 2;
    for (uint8_t port = 0; port < ctl->info.port_count; port++) {
        uint16_t reg = (uint16_t)(ctl->info.io_base +
                                  UHCI_PORTSC_BASE +
                                  (port * UHCI_PORTSC_STRIDE));
        uint16_t status = inw(reg);
        struct usb_port_info *out = &ctl->ports[port];

        memset(out, 0, sizeof(*out));
        out->present = 1;
        out->status_raw = status;
        out->connected = (status & UHCI_PORTSC_CCS) ? 1u : 0u;
        out->enabled = (status & UHCI_PORTSC_PE) ? 1u : 0u;
        out->low_speed = (status & UHCI_PORTSC_LSDA) ? 1u : 0u;
        out->reset = (status & UHCI_PORTSC_PR) ? 1u : 0u;
        out->powered = out->connected ? 1u : 0u;
        out->owner = (status & UHCI_PORTSC_OC) ? 1u : 0u;
    }
}

void usb_init(void) {
    memset(controllers, 0, sizeof(controllers));
    controller_count_val = 0;

    struct device_entry *devices[DEVICE_MAX_ENTRIES];
    int count = device_get_by_type(DEVICE_TYPE_USB, devices, DEVICE_MAX_ENTRIES);
    for (int i = 0; i < count && controller_count_val < USB_MAX_CONTROLLERS; i++) {
        struct device_entry *dev = devices[i];
        if (!dev || dev->bus != DEVICE_BUS_PCI) continue;

        struct usb_controller_state *ctl = &controllers[controller_count_val];
        memset(ctl, 0, sizeof(*ctl));

        ctl->info.present = 1;
        ctl->info.host_type = host_type_from_prog_if(dev->pci_prog_if);
        ctl->info.irq_line = dev->pci_irq;
        ctl->info.pci_bus = dev->pci_bus;
        ctl->info.pci_slot = dev->pci_slot;
        ctl->info.pci_func = dev->pci_func;
        ctl->info.vendor_id = dev->vendor_id;
        ctl->info.device_id = dev->device_id;
        copy_name(ctl->info.name, dev->name, sizeof(ctl->info.name));

        if (ctl->info.host_type == USB_HOST_UHCI) {
            uint16_t pci_cmd = pci_config_read16(dev->pci_bus, dev->pci_slot,
                                                 dev->pci_func, PCI_COMMAND_OFFSET);
            pci_cmd |= (PCI_CMD_IO_ENABLE | PCI_CMD_BUSMASTER);
            pci_config_write16(dev->pci_bus, dev->pci_slot,
                               dev->pci_func, PCI_COMMAND_OFFSET, pci_cmd);

            uint32_t bar4 = pci_config_read32(dev->pci_bus, dev->pci_slot,
                                              dev->pci_func, PCI_BAR4_OFFSET);
            ctl->info.io_base = bar4 & ~0x1Fu;
            probe_uhci_ports(ctl);
        }

        controller_count_val++;
    }

    if (controller_count_val > 0) {
        vga_writestring("USB: Found ");
        print_dec((uint64_t)controller_count_val);
        vga_writestring(" controller(s)\n");
        for (int i = 0; i < controller_count_val; i++) {
            vga_writestring("USB: ");
            vga_writestring(host_type_name(controllers[i].info.host_type));
            vga_writestring(" ");
            vga_writestring(controllers[i].info.name);
            if (controllers[i].info.io_base) {
                vga_writestring(" io=");
                print_hex32(controllers[i].info.io_base);
            }
            vga_putchar('\n');
        }
    } else {
        vga_writestring("USB: No controllers detected\n");
    }
}

int usb_controller_count(void) {
    return controller_count_val;
}

int usb_get_controller_info(int index, struct usb_controller_info *out) {
    if (!out) return -1;
    if (index < 0 || index >= controller_count_val) return -1;
    memcpy(out, &controllers[index].info, sizeof(*out));
    return 0;
}

int usb_get_port_info(int controller_index, int port_index,
                      struct usb_port_info *out) {
    if (!out) return -1;
    if (controller_index < 0 || controller_index >= controller_count_val) return -1;

    struct usb_controller_state *ctl = &controllers[controller_index];
    if (port_index < 0 || port_index >= ctl->info.port_count) return -1;

    memcpy(out, &ctl->ports[port_index], sizeof(*out));
    return 0;
}
