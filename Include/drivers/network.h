#ifndef NET_H
#define NET_H

#include "lib/base.h"

#define NET_IPV4_ADDR_LEN      4
#define NET_MAC_ADDR_LEN       6
#define NET_DRIVER_NAME_LEN    32
#define NET_INTERFACE_NAME_LEN 48
#define NET_HOST_NAME_LEN      64
#define NET_HTTP_PATH_LEN      192
#define NET_HTTP_HEADER_VALUE_LEN 64

#define NET_CLIENT_FLAG_INSECURE       0x00000001u
#define NET_HTTP_FLAG_INCLUDE_HEADERS  0x00000002u

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

struct net_tcp_info {
    uint8_t  state;
    uint8_t  reset;
    uint8_t  remote_closed;
    uint8_t  reserved0;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t recv_ready;
    uint32_t send_ready;
    uint8_t  remote_ip[NET_IPV4_ADDR_LEN];
};

struct net_tls_result {
    uint8_t  success;
    uint8_t  secure;
    uint16_t protocol_version;
    uint16_t cipher_suite;
    uint16_t remote_port;
    uint8_t  remote_ip[NET_IPV4_ADDR_LEN];
    char     server_name[NET_HOST_NAME_LEN];
};

struct net_http_request {
    uint8_t  remote_ip[NET_IPV4_ADDR_LEN];
    uint16_t remote_port;
    uint16_t secure;
    uint32_t flags;
    uint32_t timeout_ms;
    char     host[NET_HOST_NAME_LEN];
    char     path[NET_HTTP_PATH_LEN];
};

struct net_http_result {
    uint16_t status_code;
    uint16_t protocol_version;
    uint16_t cipher_suite;
    uint16_t remote_port;
    uint8_t  secure;
    uint8_t  truncated;
    uint8_t  headers_included;
    uint8_t  reserved0;
    uint32_t bytes_received;
    uint32_t body_offset;
    uint8_t  remote_ip[NET_IPV4_ADDR_LEN];
    char     content_type[NET_HTTP_HEADER_VALUE_LEN];
    char     location[NET_HTTP_PATH_LEN];
};

void net_init(void);
void net_poll(void);
int  net_is_available(void);
int  net_get_info(struct net_info *out);
int  net_request_dhcp(uint32_t timeout_ms);
int  net_ping_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                   uint32_t timeout_ms,
                   struct net_ping_result *out);
int  net_tcp_connect_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                          uint16_t port,
                          uint32_t timeout_ms);
ssize_t net_tcp_send(int handle, const void *buf, size_t len, uint32_t timeout_ms);
ssize_t net_tcp_recv(int handle, void *buf, size_t len, uint32_t timeout_ms);
int  net_tcp_close(int handle, uint32_t timeout_ms);
int  net_tcp_get_info(int handle, struct net_tcp_info *out);
int  net_tls_probe_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                        uint16_t port,
                        const char *server_name,
                        uint32_t flags,
                        uint32_t timeout_ms,
                        struct net_tls_result *out);
ssize_t net_http_get_ipv4(const struct net_http_request *request,
                          void *buf,
                          size_t len,
                          struct net_http_result *out);
void net_print_status(void);

#endif /* NET_H */
