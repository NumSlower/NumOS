#ifndef NET_H
#define NET_H

#include "lib/base.h"

#define NET_IPV4_ADDR_LEN      4
#define NET_MAC_ADDR_LEN       6
#define NET_DRIVER_NAME_LEN    32
#define NET_INTERFACE_NAME_LEN 48

struct net_info {
    uint8_t  present;
    uint8_t  link_up;
    uint8_t  dhcp_configured;
    uint8_t  reserved0;
    uint8_t  mac[NET_MAC_ADDR_LEN];
    uint8_t  ipv4[NET_IPV4_ADDR_LEN];
    uint8_t  netmask[NET_IPV4_ADDR_LEN];
    uint8_t  gateway[NET_IPV4_ADDR_LEN];
    uint8_t  dhcp_server[NET_IPV4_ADDR_LEN];
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint8_t  reserved1;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    char     driver[NET_DRIVER_NAME_LEN];
    char     interface_name[NET_INTERFACE_NAME_LEN];
};

struct net_ping_result {
    uint8_t  success;
    uint8_t  ttl;
    uint16_t seq;
    uint32_t roundtrip_ms;
};

void net_init(void);
void net_poll(void);
int  net_is_available(void);
int  net_get_info(struct net_info *out);
int  net_request_dhcp(uint32_t timeout_ms);
int  net_ping_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                   uint32_t timeout_ms,
                   struct net_ping_result *out);
void net_print_status(void);

#endif /* NET_H */
