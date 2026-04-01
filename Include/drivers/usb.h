#ifndef USB_H
#define USB_H

#include "lib/base.h"

#define USB_MAX_CONTROLLERS 8
#define USB_MAX_PORTS       8

#define USB_HOST_NONE  0u
#define USB_HOST_UHCI  1u
#define USB_HOST_OHCI  2u
#define USB_HOST_EHCI  3u
#define USB_HOST_XHCI  4u

struct usb_controller_info {
    uint8_t  present;
    uint8_t  host_type;
    uint8_t  port_count;
    uint8_t  irq_line;
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint8_t  reserved;
    uint16_t vendor_id;
    uint16_t device_id;
    uint32_t io_base;
    char     name[48];
};

struct usb_port_info {
    uint8_t  present;
    uint8_t  connected;
    uint8_t  enabled;
    uint8_t  low_speed;
    uint8_t  owner;
    uint8_t  powered;
    uint8_t  reset;
    uint8_t  reserved;
    uint16_t status_raw;
};

void usb_init(void);
int usb_controller_count(void);
int usb_get_controller_info(int index, struct usb_controller_info *out);
int usb_get_port_info(int controller_index, int port_index,
                      struct usb_port_info *out);

#endif /* USB_H */
