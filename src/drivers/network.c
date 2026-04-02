#include "drivers/network.h"

#include "cpu/paging.h"
#include "drivers/device.h"
#include "drivers/graphices/vga.h"
#include "drivers/timer.h"
#include "kernel/kernel.h"
#include "kernel/scheduler.h"

#define PCI_COMMAND_OFFSET       0x04
#define PCI_COMMAND_IO           0x0001
#define PCI_COMMAND_MEMORY       0x0002
#define PCI_COMMAND_BUSMASTER    0x0004

#define E1000_VENDOR_ID          0x8086
#define E1000_DEVICE_ID_82540EM  0x100E

#define PCNET_VENDOR_ID          0x1022
#define PCNET_DEVICE_ID          0x2000

#define E1000_MMIO_VIRT_BASE     0xFFFFFFFFC1000000UL
#define E1000_MMIO_MAP_SIZE      0x00020000UL

#define E1000_REG_CTRL           0x0000
#define E1000_REG_STATUS         0x0008
#define E1000_REG_EERD           0x0014
#define E1000_REG_ICR            0x00C0
#define E1000_REG_IMC            0x00D8
#define E1000_REG_RCTL           0x0100
#define E1000_REG_TCTL           0x0400
#define E1000_REG_TIPG           0x0410
#define E1000_REG_RDBAL          0x2800
#define E1000_REG_RDBAH          0x2804
#define E1000_REG_RDLEN          0x2808
#define E1000_REG_RDH            0x2810
#define E1000_REG_RDT            0x2818
#define E1000_REG_TDBAL          0x3800
#define E1000_REG_TDBAH          0x3804
#define E1000_REG_TDLEN          0x3808
#define E1000_REG_TDH            0x3810
#define E1000_REG_TDT            0x3818
#define E1000_REG_MTA            0x5200
#define E1000_REG_RAL            0x5400
#define E1000_REG_RAH            0x5404

#define E1000_CTRL_SLU           0x00000040UL
#define E1000_CTRL_RST           0x04000000UL
#define E1000_STATUS_LU          0x00000002UL

#define E1000_RCTL_EN            0x00000002UL
#define E1000_RCTL_BAM           0x00008000UL
#define E1000_RCTL_SECRC         0x04000000UL

#define E1000_TCTL_EN            0x00000002UL
#define E1000_TCTL_PSP           0x00000008UL
#define E1000_TCTL_CT_SHIFT      4
#define E1000_TCTL_COLD_SHIFT    12

#define E1000_TX_CMD_EOP         0x01
#define E1000_TX_CMD_IFCS        0x02
#define E1000_TX_CMD_RS          0x08
#define E1000_TX_STATUS_DD       0x01
#define E1000_RX_STATUS_DD       0x01

#define PCNET_IO_RDP             0x10
#define PCNET_IO_RAP             0x12
#define PCNET_IO_RESET           0x14
#define PCNET_IO_BDP             0x16

#define PCNET_CSR0_INIT          0x0001
#define PCNET_CSR0_STRT          0x0002
#define PCNET_CSR0_STOP          0x0004
#define PCNET_CSR0_TDMD          0x0008
#define PCNET_CSR0_TXON          0x0010
#define PCNET_CSR0_RXON          0x0020
#define PCNET_CSR0_IDON          0x0100
#define PCNET_CSR0_TINT          0x0200
#define PCNET_CSR0_RINT          0x0400
#define PCNET_CSR0_MERR          0x0800
#define PCNET_CSR0_MISS          0x1000
#define PCNET_CSR0_CERR          0x2000
#define PCNET_CSR0_BABL          0x4000
#define PCNET_CSR0_ERR           0x8000
#define PCNET_CSR0_ACK_BITS      0x7F00

#define PCNET_CSR3_DEFAULT       0x0000
#define PCNET_CSR4_AUTO_PAD_TX   0x0800

#define PCNET_BCR2_ASEL          0x0002
#define PCNET_BCR20_SWSTYLE2     0x0002

#define PCNET_DESC_OWN           0x80
#define PCNET_DESC_ERR           0x40
#define PCNET_DESC_STP           0x02
#define PCNET_DESC_ENP           0x01

#define NET_RX_DESC_COUNT        32
#define NET_TX_DESC_COUNT        8
#define NET_PACKET_BUFFER_SIZE   2048
#define NET_ARP_CACHE_SIZE       8
#define NET_ETH_FRAME_MIN        60

#define ETH_TYPE_IPV4            0x0800
#define ETH_TYPE_ARP             0x0806

#define ARP_OP_REQUEST           1
#define ARP_OP_REPLY             2

#define IPV4_PROTO_ICMP          1
#define IPV4_PROTO_TCP           6
#define IPV4_PROTO_UDP           17

#define DHCP_CLIENT_PORT         68
#define DHCP_SERVER_PORT         67
#define DHCP_MAGIC_COOKIE        0x63825363UL
#define DHCP_FLAG_BROADCAST      0x8000

#define DHCP_MSG_DISCOVER        1
#define DHCP_MSG_OFFER           2
#define DHCP_MSG_REQUEST         3
#define DHCP_MSG_ACK             5
#define DHCP_MSG_NAK             6

#define DHCP_OPT_SUBNET_MASK     1
#define DHCP_OPT_ROUTER          3
#define DHCP_OPT_REQUESTED_IP    50
#define DHCP_OPT_LEASE_TIME      51
#define DHCP_OPT_MSG_TYPE        53
#define DHCP_OPT_SERVER_ID       54
#define DHCP_OPT_PARAM_LIST      55
#define DHCP_OPT_END             255

#define ICMP_TYPE_ECHO_REPLY     0
#define ICMP_TYPE_ECHO_REQUEST   8

#define TCP_FLAG_FIN             0x01
#define TCP_FLAG_SYN             0x02
#define TCP_FLAG_RST             0x04
#define TCP_FLAG_PSH             0x08
#define TCP_FLAG_ACK             0x10

#define NET_TCP_MAX_CONNECTIONS  8
#define NET_TCP_RECV_BUFFER_SIZE 8192
#define NET_TCP_WINDOW_SIZE      NET_TCP_RECV_BUFFER_SIZE
#define NET_TCP_TX_MSS           1200
#define NET_TCP_DEFAULT_TIMEOUT  5000
#define NET_TCP_EPHEMERAL_BASE   40000

#define NET_OK                   0
#define NET_ERR_GENERIC         -1
#define NET_ERR_UNAVAILABLE     -2
#define NET_ERR_TIMEOUT         -3
#define NET_ERR_NOT_CONFIGURED  -4
#define NET_ERR_INVALID         -5
#define NET_ERR_CLOSED          -6

struct e1000_rx_desc {
    uint64_t addr;
    uint16_t length;
    uint16_t checksum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed));

struct e1000_tx_desc {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed));

enum net_backend_type {
    NET_BACKEND_NONE = 0,
    NET_BACKEND_E1000 = 1,
    NET_BACKEND_PCNET = 2,
};

struct pcnet_init_block {
    uint16_t mode;
    uint8_t  rlen;
    uint8_t  tlen;
    uint8_t  phys_addr[NET_MAC_ADDR_LEN];
    uint16_t reserved;
    uint8_t  logical_addr[8];
    uint32_t rx_ring;
    uint32_t tx_ring;
} __attribute__((packed));

struct net_arp_entry {
    uint8_t  valid;
    uint8_t  ip[NET_IPV4_ADDR_LEN];
    uint8_t  mac[NET_MAC_ADDR_LEN];
    uint64_t seen_ms;
};

struct net_dhcp_state {
    uint32_t xid;
    uint32_t lease_seconds;
    uint8_t  offer_valid;
    uint8_t  ack_received;
    uint8_t  nak_received;
    uint8_t  msg_type;
    uint8_t  offered_ip[NET_IPV4_ADDR_LEN];
    uint8_t  server_id[NET_IPV4_ADDR_LEN];
    uint8_t  subnet_mask[NET_IPV4_ADDR_LEN];
    uint8_t  router[NET_IPV4_ADDR_LEN];
};

struct net_ping_state {
    uint8_t  active;
    uint8_t  done;
    uint8_t  success;
    uint8_t  ttl;
    uint16_t ident;
    uint16_t seq;
    uint64_t start_ms;
    uint8_t  target_ip[NET_IPV4_ADDR_LEN];
};

enum net_tcp_conn_state {
    NET_TCP_CLOSED = 0,
    NET_TCP_SYN_SENT = 1,
    NET_TCP_ESTABLISHED = 2,
    NET_TCP_CLOSE_WAIT = 3,
    NET_TCP_FIN_WAIT_1 = 4,
    NET_TCP_FIN_WAIT_2 = 5,
    NET_TCP_CLOSING = 6,
    NET_TCP_LAST_ACK = 7,
    NET_TCP_RESET = 8,
};

struct net_tcp_conn {
    uint8_t  used;
    uint8_t  state;
    uint8_t  remote_closed;
    uint8_t  reset;
    uint16_t local_port;
    uint16_t remote_port;
    uint32_t iss;
    uint32_t snd_una;
    uint32_t snd_nxt;
    uint32_t rcv_nxt;
    uint8_t  remote_ip[NET_IPV4_ADDR_LEN];
    uint32_t rx_head;
    uint32_t rx_tail;
    uint64_t last_activity_ms;
    int      owner_pid;
    uint8_t  rx_buffer[NET_TCP_RECV_BUFFER_SIZE];
};

struct net_state {
    uint8_t  backend;
    uint8_t  present;
    uint8_t  ready;
    uint8_t  link_up;
    uint8_t  dhcp_configured;
    uint8_t  mac[NET_MAC_ADDR_LEN];
    uint8_t  ipv4[NET_IPV4_ADDR_LEN];
    uint8_t  netmask[NET_IPV4_ADDR_LEN];
    uint8_t  gateway[NET_IPV4_ADDR_LEN];
    uint8_t  dhcp_server[NET_IPV4_ADDR_LEN];
    uint8_t  pci_bus;
    uint8_t  pci_slot;
    uint8_t  pci_func;
    uint8_t  reserved0;
    char     interface_name[NET_INTERFACE_NAME_LEN];
    char     driver[NET_DRIVER_NAME_LEN];
    uint16_t io_base;
    uint16_t reserved1;
    uint64_t mmio_phys;
    volatile uint8_t *mmio;
    struct e1000_rx_desc *rx_descs;
    struct e1000_tx_desc *tx_descs;
    uint64_t rx_descs_phys;
    uint64_t tx_descs_phys;
    uint8_t *pcnet_rx_ring;
    uint8_t *pcnet_tx_ring;
    struct pcnet_init_block *pcnet_init;
    uint64_t pcnet_rx_ring_phys;
    uint64_t pcnet_tx_ring_phys;
    uint64_t pcnet_init_phys;
    void    *rx_buffers[NET_RX_DESC_COUNT];
    void    *tx_buffers[NET_TX_DESC_COUNT];
    uint64_t rx_buffers_phys[NET_RX_DESC_COUNT];
    uint64_t tx_buffers_phys[NET_TX_DESC_COUNT];
    uint32_t rx_index;
    uint32_t tx_index;
    uint16_t next_ip_id;
    uint16_t next_ping_seq;
    uint16_t next_tcp_port;
    uint64_t rx_packets;
    uint64_t tx_packets;
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    struct net_arp_entry arp_cache[NET_ARP_CACHE_SIZE];
    struct net_dhcp_state dhcp;
    struct net_ping_state ping;
    struct net_tcp_conn tcp[NET_TCP_MAX_CONNECTIONS];
};

struct net_eth_header {
    uint8_t  dst[NET_MAC_ADDR_LEN];
    uint8_t  src[NET_MAC_ADDR_LEN];
    uint16_t ethertype;
} __attribute__((packed));

struct net_arp_packet {
    uint16_t htype;
    uint16_t ptype;
    uint8_t  hlen;
    uint8_t  plen;
    uint16_t oper;
    uint8_t  sha[NET_MAC_ADDR_LEN];
    uint8_t  spa[NET_IPV4_ADDR_LEN];
    uint8_t  tha[NET_MAC_ADDR_LEN];
    uint8_t  tpa[NET_IPV4_ADDR_LEN];
} __attribute__((packed));

struct net_ipv4_header {
    uint8_t  version_ihl;
    uint8_t  dscp_ecn;
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t checksum;
    uint8_t  src[NET_IPV4_ADDR_LEN];
    uint8_t  dst[NET_IPV4_ADDR_LEN];
} __attribute__((packed));

struct net_udp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint16_t length;
    uint16_t checksum;
} __attribute__((packed));

struct net_tcp_header {
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq_num;
    uint32_t ack_num;
    uint8_t  data_offset;
    uint8_t  flags;
    uint16_t window;
    uint16_t checksum;
    uint16_t urgent_ptr;
} __attribute__((packed));

struct net_icmp_echo {
    uint8_t  type;
    uint8_t  code;
    uint16_t checksum;
    uint16_t ident;
    uint16_t seq;
} __attribute__((packed));

struct net_dhcp_packet {
    uint8_t  op;
    uint8_t  htype;
    uint8_t  hlen;
    uint8_t  hops;
    uint32_t xid;
    uint16_t secs;
    uint16_t flags;
    uint32_t ciaddr;
    uint32_t yiaddr;
    uint32_t siaddr;
    uint32_t giaddr;
    uint8_t  chaddr[16];
    uint8_t  sname[64];
    uint8_t  file[128];
    uint32_t magic;
    uint8_t  options[312];
} __attribute__((packed));

static struct net_state g_net;

static void net_print_virtualbox_pcnet_hint(void) {
    const struct hypervisor_info *hv = device_get_hypervisor();
    struct device_entry *devices[DEVICE_MAX_ENTRIES];
    int count = device_get_by_type(DEVICE_TYPE_NETWORK, devices, DEVICE_MAX_ENTRIES);

    for (int i = 0; i < count; i++) {
        struct device_entry *dev = devices[i];
        if (!dev || dev->bus != DEVICE_BUS_PCI) continue;
        if (dev->vendor_id != 0x1022 || dev->device_id != 0x2000) continue;

        vga_writestring("NET: AMD PCnet NIC detected but setup failed\n");
        if (hv && hv->id == HYPERVISOR_VIRTUALBOX) {
            vga_writestring("NET: VirtualBox adapter type may still be switched to Intel PRO/1000 MT Desktop\n");
        }
        return;
    }
}

static int net_phys32(uint64_t phys, uint32_t *out) {
    if (!out) return 0;
    if (phys > 0xFFFFFFFFu) return 0;
    *out = (uint32_t)phys;
    return 1;
}

static uint16_t pcnet_read_csr(uint16_t reg) {
    outw(g_net.io_base + PCNET_IO_RAP, reg);
    return inw(g_net.io_base + PCNET_IO_RDP);
}

static void pcnet_write_csr(uint16_t reg, uint16_t value) {
    outw(g_net.io_base + PCNET_IO_RAP, reg);
    outw(g_net.io_base + PCNET_IO_RDP, value);
}

static uint16_t pcnet_read_bcr(uint16_t reg) {
    outw(g_net.io_base + PCNET_IO_RAP, reg);
    return inw(g_net.io_base + PCNET_IO_BDP);
}

static void pcnet_write_bcr(uint16_t reg, uint16_t value) {
    outw(g_net.io_base + PCNET_IO_RAP, reg);
    outw(g_net.io_base + PCNET_IO_BDP, value);
}

static void pcnet_reset(void) {
    (void)inw(g_net.io_base + PCNET_IO_RESET);
    for (volatile int i = 0; i < 100000; i++);
}

static void pcnet_read_mac(uint8_t mac[NET_MAC_ADDR_LEN]) {
    for (int i = 0; i < NET_MAC_ADDR_LEN; i++) {
        mac[i] = inb((uint16_t)(g_net.io_base + i));
    }
}

static size_t pcnet_frame_length(size_t len) {
    return (len < NET_ETH_FRAME_MIN) ? NET_ETH_FRAME_MIN : len;
}

static size_t pcnet_ring_offset(uint32_t index) {
    return (size_t)index * 16u;
}

static void pcnet_init_desc(uint8_t *ring, uint32_t index,
                            uint32_t buffer_phys, int card_owns) {
    size_t off = pcnet_ring_offset(index);
    uint16_t bcnt = (uint16_t)(-(int)NET_PACKET_BUFFER_SIZE);
    bcnt &= 0x0FFFu;
    bcnt |= 0xF000u;

    memset(ring + off, 0, 16);
    memcpy(ring + off, &buffer_phys, sizeof(buffer_phys));
    memcpy(ring + off + 4, &bcnt, sizeof(bcnt));
    ring[off + 7] = card_owns ? PCNET_DESC_OWN : 0;
}

static int pcnet_driver_owns(const uint8_t *ring, uint32_t index) {
    return (ring[pcnet_ring_offset(index) + 7] & PCNET_DESC_OWN) == 0;
}

static uint16_t read_be16(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

static uint32_t read_be32(const void *ptr) {
    const uint8_t *p = (const uint8_t *)ptr;
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static void write_be16(void *ptr, uint16_t value) {
    uint8_t *p = (uint8_t *)ptr;
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)(value & 0xFFu);
}

static void write_be32(void *ptr, uint32_t value) {
    uint8_t *p = (uint8_t *)ptr;
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)((value >> 16) & 0xFFu);
    p[2] = (uint8_t)((value >> 8) & 0xFFu);
    p[3] = (uint8_t)(value & 0xFFu);
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

static int ip_is_zero(const uint8_t ip[NET_IPV4_ADDR_LEN]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static int ip_is_broadcast(const uint8_t ip[NET_IPV4_ADDR_LEN]) {
    return ip[0] == 255 && ip[1] == 255 && ip[2] == 255 && ip[3] == 255;
}

static int ip_equal(const uint8_t a[NET_IPV4_ADDR_LEN],
                    const uint8_t b[NET_IPV4_ADDR_LEN]) {
    return memcmp(a, b, NET_IPV4_ADDR_LEN) == 0;
}

static int ip_same_subnet(const uint8_t a[NET_IPV4_ADDR_LEN],
                          const uint8_t b[NET_IPV4_ADDR_LEN],
                          const uint8_t mask[NET_IPV4_ADDR_LEN]) {
    for (int i = 0; i < NET_IPV4_ADDR_LEN; i++) {
        if ((uint8_t)(a[i] & mask[i]) != (uint8_t)(b[i] & mask[i])) return 0;
    }
    return 1;
}

static void ipv4_to_str(const uint8_t ip[NET_IPV4_ADDR_LEN], char *out, size_t cap) {
    if (!out || cap < 8) return;

    char buf[16];
    size_t pos = 0;
    for (int i = 0; i < NET_IPV4_ADDR_LEN; i++) {
        uint8_t value = ip[i];
        if (value >= 100) buf[pos++] = (char)('0' + (value / 100));
        if (value >= 10)  buf[pos++] = (char)('0' + ((value / 10) % 10));
        buf[pos++] = (char)('0' + (value % 10));
        if (i != NET_IPV4_ADDR_LEN - 1) buf[pos++] = '.';
    }
    if (pos >= cap) pos = cap - 1;
    memcpy(out, buf, pos);
    out[pos] = '\0';
}

static void mac_to_str(const uint8_t mac[NET_MAC_ADDR_LEN], char *out, size_t cap) {
    static const char hex[] = "0123456789ABCDEF";
    if (!out || cap < 18) return;

    size_t pos = 0;
    for (int i = 0; i < NET_MAC_ADDR_LEN; i++) {
        out[pos++] = hex[(mac[i] >> 4) & 0x0F];
        out[pos++] = hex[mac[i] & 0x0F];
        if (i != NET_MAC_ADDR_LEN - 1) out[pos++] = ':';
    }
    out[pos] = '\0';
}

static void net_delay(void) {
    for (volatile int i = 0; i < 200000; i++);
}

static volatile uint32_t *net_reg(uint32_t offset) {
    return (volatile uint32_t *)(uintptr_t)(g_net.mmio + offset);
}

static uint32_t e1000_read32(uint32_t offset) {
    return *net_reg(offset);
}

static void e1000_write32(uint32_t offset, uint32_t value) {
    *net_reg(offset) = value;
}

static uint16_t net_checksum16(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;

    while (len > 1) {
        sum += (uint32_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
        bytes += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)((uint16_t)bytes[0] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

static int tcp_seq_before(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) < 0;
}

static int tcp_seq_after(uint32_t a, uint32_t b) {
    return (int32_t)(a - b) > 0;
}

static uint32_t tcp_conn_rx_len(const struct net_tcp_conn *conn) {
    if (conn->rx_tail >= conn->rx_head) {
        return conn->rx_tail - conn->rx_head;
    }
    return NET_TCP_RECV_BUFFER_SIZE - conn->rx_head + conn->rx_tail;
}

static uint32_t tcp_conn_rx_space(const struct net_tcp_conn *conn) {
    return (NET_TCP_RECV_BUFFER_SIZE - 1u) - tcp_conn_rx_len(conn);
}

static void tcp_conn_release(struct net_tcp_conn *conn) {
    if (!conn) return;
    memset(conn, 0, sizeof(*conn));
}

static int tcp_conn_queue(struct net_tcp_conn *conn, const uint8_t *data, size_t len) {
    if (!conn || (!data && len != 0)) return 0;
    if (len > tcp_conn_rx_space(conn)) return 0;

    for (size_t i = 0; i < len; i++) {
        conn->rx_buffer[conn->rx_tail] = data[i];
        conn->rx_tail = (conn->rx_tail + 1u) % NET_TCP_RECV_BUFFER_SIZE;
    }
    return 1;
}

static size_t tcp_conn_dequeue(struct net_tcp_conn *conn, uint8_t *out, size_t len) {
    size_t copied = 0;

    if (!conn || !out) return 0;
    while (copied < len && conn->rx_head != conn->rx_tail) {
        out[copied++] = conn->rx_buffer[conn->rx_head];
        conn->rx_head = (conn->rx_head + 1u) % NET_TCP_RECV_BUFFER_SIZE;
    }
    return copied;
}

static int tcp_conn_ack_valid(const struct net_tcp_conn *conn, uint32_t ack_num) {
    if (!conn) return 0;
    if (tcp_seq_before(ack_num, conn->snd_una)) return 0;
    if (tcp_seq_after(ack_num, conn->snd_nxt)) return 0;
    return 1;
}

static struct net_tcp_conn *tcp_conn_from_handle(int handle) {
    if (handle <= 0 || handle > NET_TCP_MAX_CONNECTIONS) return NULL;
    struct net_tcp_conn *conn = &g_net.tcp[handle - 1];
    if (!conn->used) return NULL;
    return conn;
}

static struct net_tcp_conn *tcp_conn_find(const uint8_t src_ip[NET_IPV4_ADDR_LEN],
                                          uint16_t src_port,
                                          uint16_t dst_port) {
    for (int i = 0; i < NET_TCP_MAX_CONNECTIONS; i++) {
        struct net_tcp_conn *conn = &g_net.tcp[i];
        if (!conn->used) continue;
        if (conn->local_port != dst_port) continue;
        if (conn->remote_port != src_port) continue;
        if (!ip_equal(conn->remote_ip, src_ip)) continue;
        return conn;
    }
    return NULL;
}

static struct net_tcp_conn *tcp_conn_alloc(void) {
    for (int i = 0; i < NET_TCP_MAX_CONNECTIONS; i++) {
        struct net_tcp_conn *conn = &g_net.tcp[i];
        if (conn->used) continue;
        memset(conn, 0, sizeof(*conn));
        conn->used = 1;
        conn->state = NET_TCP_CLOSED;
        return conn;
    }
    return NULL;
}

static uint16_t tcp_pick_local_port(void) {
    uint16_t start = g_net.next_tcp_port;
    if (start < NET_TCP_EPHEMERAL_BASE) start = NET_TCP_EPHEMERAL_BASE;

    for (uint32_t attempt = 0; attempt < 0x8000u; attempt++) {
        uint16_t port = (uint16_t)(start + attempt);
        if (port < NET_TCP_EPHEMERAL_BASE) port = (uint16_t)(NET_TCP_EPHEMERAL_BASE + attempt);

        int used = 0;
        for (int i = 0; i < NET_TCP_MAX_CONNECTIONS; i++) {
            if (g_net.tcp[i].used && g_net.tcp[i].local_port == port) {
                used = 1;
                break;
            }
        }
        if (!used) {
            g_net.next_tcp_port = (uint16_t)(port + 1u);
            if (g_net.next_tcp_port < NET_TCP_EPHEMERAL_BASE) {
                g_net.next_tcp_port = NET_TCP_EPHEMERAL_BASE;
            }
            return port;
        }
    }

    return 0;
}

static uint32_t net_checksum16_partial(uint32_t sum, const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;

    while (len > 1) {
        sum += (uint32_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
        bytes += 2;
        len -= 2;
    }
    if (len) {
        sum += (uint32_t)((uint16_t)bytes[0] << 8);
    }
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return sum;
}

static uint16_t net_tcp_checksum(const uint8_t src_ip[NET_IPV4_ADDR_LEN],
                                 const uint8_t dst_ip[NET_IPV4_ADDR_LEN],
                                 const void *segment,
                                 size_t segment_len) {
    uint32_t sum = 0;
    uint8_t pseudo[12];

    memcpy(pseudo + 0, src_ip, NET_IPV4_ADDR_LEN);
    memcpy(pseudo + 4, dst_ip, NET_IPV4_ADDR_LEN);
    pseudo[8] = 0;
    pseudo[9] = IPV4_PROTO_TCP;
    write_be16(pseudo + 10, (uint16_t)segment_len);

    sum = net_checksum16_partial(0, pseudo, sizeof(pseudo));
    sum = net_checksum16_partial(sum, segment, segment_len);
    while (sum >> 16) {
        sum = (sum & 0xFFFFu) + (sum >> 16);
    }
    return (uint16_t)(~sum & 0xFFFFu);
}

static void arp_cache_update(const uint8_t ip[NET_IPV4_ADDR_LEN],
                             const uint8_t mac[NET_MAC_ADDR_LEN]) {
    int slot = -1;
    uint64_t oldest = 0;
    int oldest_slot = 0;

    for (int i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        struct net_arp_entry *entry = &g_net.arp_cache[i];
        if (entry->valid && ip_equal(entry->ip, ip)) {
            memcpy(entry->mac, mac, NET_MAC_ADDR_LEN);
            entry->seen_ms = timer_get_uptime_ms();
            return;
        }
        if (!entry->valid && slot < 0) slot = i;
        if (i == 0 || entry->seen_ms < oldest) {
            oldest = entry->seen_ms;
            oldest_slot = i;
        }
    }

    if (slot < 0) slot = oldest_slot;
    g_net.arp_cache[slot].valid = 1;
    memcpy(g_net.arp_cache[slot].ip, ip, NET_IPV4_ADDR_LEN);
    memcpy(g_net.arp_cache[slot].mac, mac, NET_MAC_ADDR_LEN);
    g_net.arp_cache[slot].seen_ms = timer_get_uptime_ms();
}

static int arp_cache_lookup(const uint8_t ip[NET_IPV4_ADDR_LEN],
                            uint8_t mac[NET_MAC_ADDR_LEN]) {
    for (int i = 0; i < NET_ARP_CACHE_SIZE; i++) {
        struct net_arp_entry *entry = &g_net.arp_cache[i];
        if (!entry->valid) continue;
        if (!ip_equal(entry->ip, ip)) continue;
        memcpy(mac, entry->mac, NET_MAC_ADDR_LEN);
        return 1;
    }
    return 0;
}

void net_poll(void);

static int e1000_send_frame(const void *frame, size_t len) {
    if (!g_net.ready || !frame || len == 0 || len > NET_PACKET_BUFFER_SIZE) {
        return NET_ERR_INVALID;
    }

    uint32_t idx = g_net.tx_index;
    struct e1000_tx_desc *desc = &g_net.tx_descs[idx];
    uint64_t wait_deadline = timer_get_uptime_ms() + 200;

    while (!(desc->status & E1000_TX_STATUS_DD)) {
        if (timer_get_uptime_ms() >= wait_deadline) return NET_ERR_TIMEOUT;
        net_poll();
    }

    memcpy(g_net.tx_buffers[idx], frame, len);
    desc->length = (uint16_t)len;
    desc->cmd = E1000_TX_CMD_EOP | E1000_TX_CMD_IFCS | E1000_TX_CMD_RS;
    desc->status = 0;
    desc->cso = 0;
    desc->css = 0;
    desc->special = 0;

    g_net.tx_index = (idx + 1u) % NET_TX_DESC_COUNT;
    e1000_write32(E1000_REG_TDT, g_net.tx_index);

    g_net.tx_packets++;
    g_net.tx_bytes += len;
    return NET_OK;
}

static int pcnet_send_frame(const void *frame, size_t len) {
    uint32_t idx = g_net.tx_index;
    uint8_t *ring = g_net.pcnet_tx_ring;
    size_t frame_len = pcnet_frame_length(len);
    uint64_t wait_deadline = timer_get_uptime_ms() + 200;
    size_t off;
    uint16_t bcnt;

    if (!g_net.ready || !frame || len == 0 || frame_len > NET_PACKET_BUFFER_SIZE) {
        return NET_ERR_INVALID;
    }

    while (!pcnet_driver_owns(ring, idx)) {
        if (timer_get_uptime_ms() >= wait_deadline) return NET_ERR_TIMEOUT;
        net_poll();
    }

    memcpy(g_net.tx_buffers[idx], frame, len);
    if (frame_len > len) {
        memset((uint8_t *)g_net.tx_buffers[idx] + len, 0, frame_len - len);
    }

    off = pcnet_ring_offset(idx);
    bcnt = (uint16_t)(-(int)frame_len);
    bcnt &= 0x0FFFu;
    bcnt |= 0xF000u;

    memset(ring + off + 8, 0, 8);
    memcpy(ring + off + 4, &bcnt, sizeof(bcnt));
    ring[off + 7] = PCNET_DESC_OWN | PCNET_DESC_STP | PCNET_DESC_ENP;

    g_net.tx_index = (idx + 1u) % NET_TX_DESC_COUNT;
    pcnet_write_csr(0, PCNET_CSR0_TDMD);

    g_net.tx_packets++;
    g_net.tx_bytes += frame_len;
    return NET_OK;
}

static int net_send_frame(const void *frame, size_t len) {
    if (g_net.backend == NET_BACKEND_E1000) {
        return e1000_send_frame(frame, len);
    }
    if (g_net.backend == NET_BACKEND_PCNET) {
        return pcnet_send_frame(frame, len);
    }
    return NET_ERR_UNAVAILABLE;
}

static int net_send_arp(uint16_t opcode,
                        const uint8_t target_mac[NET_MAC_ADDR_LEN],
                        const uint8_t target_ip[NET_IPV4_ADDR_LEN]) {
    uint8_t frame[sizeof(struct net_eth_header) + sizeof(struct net_arp_packet)];
    struct net_eth_header *eth = (struct net_eth_header *)frame;
    struct net_arp_packet *arp =
        (struct net_arp_packet *)(frame + sizeof(struct net_eth_header));

    if (opcode == ARP_OP_REQUEST) {
        memset(eth->dst, 0xFF, NET_MAC_ADDR_LEN);
        memset(arp->tha, 0x00, NET_MAC_ADDR_LEN);
    } else {
        if (!target_mac) return NET_ERR_INVALID;
        memcpy(eth->dst, target_mac, NET_MAC_ADDR_LEN);
        memcpy(arp->tha, target_mac, NET_MAC_ADDR_LEN);
    }

    memcpy(eth->src, g_net.mac, NET_MAC_ADDR_LEN);
    write_be16(&eth->ethertype, ETH_TYPE_ARP);

    write_be16(&arp->htype, 1);
    write_be16(&arp->ptype, ETH_TYPE_IPV4);
    arp->hlen = NET_MAC_ADDR_LEN;
    arp->plen = NET_IPV4_ADDR_LEN;
    write_be16(&arp->oper, opcode);
    memcpy(arp->sha, g_net.mac, NET_MAC_ADDR_LEN);
    memcpy(arp->spa, g_net.ipv4, NET_IPV4_ADDR_LEN);
    memcpy(arp->tpa, target_ip, NET_IPV4_ADDR_LEN);

    return net_send_frame(frame, sizeof(frame));
}

static int net_send_ipv4(const uint8_t dst_ip[NET_IPV4_ADDR_LEN],
                         uint8_t protocol,
                         const void *payload,
                         size_t payload_len) {
    uint8_t next_hop_ip[NET_IPV4_ADDR_LEN];
    uint8_t dst_mac[NET_MAC_ADDR_LEN];
    uint8_t frame[1514];
    struct net_eth_header *eth = (struct net_eth_header *)frame;
    struct net_ipv4_header *ip =
        (struct net_ipv4_header *)(frame + sizeof(struct net_eth_header));
    uint8_t *out_payload = (uint8_t *)(frame + sizeof(struct net_eth_header) +
                                       sizeof(struct net_ipv4_header));
    size_t frame_len = sizeof(struct net_eth_header) + sizeof(struct net_ipv4_header) +
                       payload_len;

    if (!g_net.ready) return NET_ERR_UNAVAILABLE;
    if (!payload || payload_len == 0) return NET_ERR_INVALID;
    if (!g_net.dhcp_configured && !ip_is_broadcast(dst_ip)) return NET_ERR_NOT_CONFIGURED;
    if (frame_len > sizeof(frame)) return NET_ERR_INVALID;

    if (ip_is_broadcast(dst_ip)) {
        memset(dst_mac, 0xFF, NET_MAC_ADDR_LEN);
    } else {
        if (ip_same_subnet(dst_ip, g_net.ipv4, g_net.netmask)) {
            memcpy(next_hop_ip, dst_ip, NET_IPV4_ADDR_LEN);
        } else if (!ip_is_zero(g_net.gateway)) {
            memcpy(next_hop_ip, g_net.gateway, NET_IPV4_ADDR_LEN);
        } else {
            return NET_ERR_INVALID;
        }

        if (!arp_cache_lookup(next_hop_ip, dst_mac)) {
            uint64_t deadline = timer_get_uptime_ms() + 1000;
            if (net_send_arp(ARP_OP_REQUEST, NULL, next_hop_ip) != NET_OK) {
                return NET_ERR_GENERIC;
            }
            while (timer_get_uptime_ms() < deadline) {
                net_poll();
                if (arp_cache_lookup(next_hop_ip, dst_mac)) break;
            }
            if (!arp_cache_lookup(next_hop_ip, dst_mac)) return NET_ERR_TIMEOUT;
        }
    }

    memcpy(eth->dst, dst_mac, NET_MAC_ADDR_LEN);
    memcpy(eth->src, g_net.mac, NET_MAC_ADDR_LEN);
    write_be16(&eth->ethertype, ETH_TYPE_IPV4);

    memset(ip, 0, sizeof(*ip));
    ip->version_ihl = 0x45;
    ip->ttl = 64;
    ip->protocol = protocol;
    write_be16(&ip->total_length, (uint16_t)(sizeof(*ip) + payload_len));
    write_be16(&ip->identification, g_net.next_ip_id++);
    write_be16(&ip->flags_fragment, 0x4000);
    memcpy(ip->src, g_net.ipv4, NET_IPV4_ADDR_LEN);
    memcpy(ip->dst, dst_ip, NET_IPV4_ADDR_LEN);
    memcpy(out_payload, payload, payload_len);
    write_be16(&ip->checksum, net_checksum16(ip, sizeof(*ip)));

    return net_send_frame(frame, frame_len);
}

static int net_send_udp(const uint8_t dst_ip[NET_IPV4_ADDR_LEN],
                        uint16_t src_port,
                        uint16_t dst_port,
                        const void *payload,
                        size_t payload_len) {
    uint8_t packet[1472];
    struct net_udp_header *udp = (struct net_udp_header *)packet;

    if (sizeof(*udp) + payload_len > sizeof(packet)) return NET_ERR_INVALID;
    write_be16(&udp->src_port, src_port);
    write_be16(&udp->dst_port, dst_port);
    write_be16(&udp->length, (uint16_t)(sizeof(*udp) + payload_len));
    write_be16(&udp->checksum, 0);
    memcpy(packet + sizeof(*udp), payload, payload_len);

    return net_send_ipv4(dst_ip, IPV4_PROTO_UDP, packet,
                         sizeof(*udp) + payload_len);
}

static int net_send_tcp_segment(struct net_tcp_conn *conn,
                                uint8_t flags,
                                const void *payload,
                                size_t payload_len) {
    uint8_t packet[sizeof(struct net_tcp_header) + NET_TCP_TX_MSS];
    struct net_tcp_header *tcp = (struct net_tcp_header *)packet;
    uint16_t window;
    size_t segment_len;

    if (!conn) return NET_ERR_INVALID;
    if (payload_len > NET_TCP_TX_MSS) return NET_ERR_INVALID;

    memset(packet, 0, sizeof(packet));
    write_be16(&tcp->src_port, conn->local_port);
    write_be16(&tcp->dst_port, conn->remote_port);
    write_be32(&tcp->seq_num, conn->snd_nxt);
    write_be32(&tcp->ack_num, conn->rcv_nxt);
    tcp->data_offset = (uint8_t)(sizeof(*tcp) / 4u) << 4;
    tcp->flags = flags;
    window = (uint16_t)tcp_conn_rx_space(conn);
    if (window == 0) window = 1;
    write_be16(&tcp->window, window);
    write_be16(&tcp->urgent_ptr, 0);

    if (payload_len > 0 && payload) {
        memcpy(packet + sizeof(*tcp), payload, payload_len);
    }

    segment_len = sizeof(*tcp) + payload_len;
    write_be16(&tcp->checksum, 0);
    write_be16(&tcp->checksum,
               net_tcp_checksum(g_net.ipv4, conn->remote_ip, packet, segment_len));

    if (net_send_ipv4(conn->remote_ip, IPV4_PROTO_TCP, packet, segment_len) != NET_OK) {
        return NET_ERR_GENERIC;
    }

    if (flags & TCP_FLAG_SYN) conn->snd_nxt++;
    if (flags & TCP_FLAG_FIN) conn->snd_nxt++;
    conn->snd_nxt += (uint32_t)payload_len;
    conn->last_activity_ms = timer_get_uptime_ms();
    return NET_OK;
}

static int net_send_icmp_echo_reply(const uint8_t dst_ip[NET_IPV4_ADDR_LEN],
                                    const uint8_t *request,
                                    size_t request_len) {
    uint8_t packet[1472];
    struct net_icmp_echo *icmp = (struct net_icmp_echo *)packet;

    if (request_len > sizeof(packet)) return NET_ERR_INVALID;
    memcpy(packet, request, request_len);
    icmp->type = ICMP_TYPE_ECHO_REPLY;
    icmp->code = 0;
    write_be16(&icmp->checksum, 0);
    write_be16(&icmp->checksum, net_checksum16(packet, request_len));
    return net_send_ipv4(dst_ip, IPV4_PROTO_ICMP, packet, request_len);
}

static int net_send_icmp_echo_request(const uint8_t dst_ip[NET_IPV4_ADDR_LEN],
                                      uint16_t ident,
                                      uint16_t seq) {
    uint8_t packet[64];
    struct net_icmp_echo *icmp = (struct net_icmp_echo *)packet;
    uint64_t now = timer_get_uptime_ms();

    memset(packet, 0, sizeof(packet));
    icmp->type = ICMP_TYPE_ECHO_REQUEST;
    icmp->code = 0;
    write_be16(&icmp->ident, ident);
    write_be16(&icmp->seq, seq);
    write_be32(packet + sizeof(*icmp), (uint32_t)now);
    write_be32(packet + sizeof(*icmp) + 4, (uint32_t)(now >> 32));
    write_be16(&icmp->checksum, net_checksum16(packet, sizeof(packet)));

    return net_send_ipv4(dst_ip, IPV4_PROTO_ICMP, packet, sizeof(packet));
}

static int net_map_mmio(uint64_t phys_base, size_t size, volatile uint8_t **out) {
    uint64_t aligned_phys = paging_align_down(phys_base, PAGE_SIZE);
    uint64_t aligned_virt = E1000_MMIO_VIRT_BASE;
    size_t map_size = paging_align_up(size + (phys_base - aligned_phys), PAGE_SIZE);

    for (size_t off = 0; off < map_size; off += PAGE_SIZE) {
        uint64_t virt = aligned_virt + off;
        uint64_t phys = aligned_phys + off;
        if (!paging_is_mapped(virt)) {
            if (paging_map_page(virt, phys,
                                PAGE_WRITABLE | PAGE_CACHE_DISABLE | PAGE_GLOBAL) != 0) {
                return NET_ERR_GENERIC;
            }
        }
    }

    *out = (volatile uint8_t *)(uintptr_t)(aligned_virt + (phys_base - aligned_phys));
    return NET_OK;
}

static int e1000_read_eeprom(uint8_t address, uint16_t *out) {
    uint32_t value = 0;
    if (!out) return 0;

    e1000_write32(E1000_REG_EERD, 1u | ((uint32_t)address << 8));
    for (int i = 0; i < 100000; i++) {
        value = e1000_read32(E1000_REG_EERD);
        if (value & (1u << 4)) {
            *out = (uint16_t)(value >> 16);
            return 1;
        }
    }
    return 0;
}

static void e1000_read_mac(uint8_t mac[NET_MAC_ADDR_LEN]) {
    uint16_t word0 = 0;
    uint16_t word1 = 0;
    uint16_t word2 = 0;

    if (e1000_read_eeprom(0, &word0)) {
        (void)e1000_read_eeprom(1, &word1);
        (void)e1000_read_eeprom(2, &word2);
        mac[0] = (uint8_t)(word0 & 0xFFu);
        mac[1] = (uint8_t)(word0 >> 8);
        mac[2] = (uint8_t)(word1 & 0xFFu);
        mac[3] = (uint8_t)(word1 >> 8);
        mac[4] = (uint8_t)(word2 & 0xFFu);
        mac[5] = (uint8_t)(word2 >> 8);
        return;
    }

    uint32_t ral = e1000_read32(E1000_REG_RAL);
    uint32_t rah = e1000_read32(E1000_REG_RAH);
    mac[0] = (uint8_t)(ral & 0xFFu);
    mac[1] = (uint8_t)((ral >> 8) & 0xFFu);
    mac[2] = (uint8_t)((ral >> 16) & 0xFFu);
    mac[3] = (uint8_t)((ral >> 24) & 0xFFu);
    mac[4] = (uint8_t)(rah & 0xFFu);
    mac[5] = (uint8_t)((rah >> 8) & 0xFFu);
}

static int e1000_alloc_dma(void) {
    g_net.rx_descs = (struct e1000_rx_desc *)vmm_alloc_pages(1,
                    PAGE_PRESENT | PAGE_WRITABLE);
    g_net.tx_descs = (struct e1000_tx_desc *)vmm_alloc_pages(1,
                    PAGE_PRESENT | PAGE_WRITABLE);
    if (!g_net.rx_descs || !g_net.tx_descs) return NET_ERR_GENERIC;

    memset(g_net.rx_descs, 0, PAGE_SIZE);
    memset(g_net.tx_descs, 0, PAGE_SIZE);
    g_net.rx_descs_phys = paging_get_physical_address((uint64_t)(uintptr_t)g_net.rx_descs);
    g_net.tx_descs_phys = paging_get_physical_address((uint64_t)(uintptr_t)g_net.tx_descs);

    for (int i = 0; i < NET_RX_DESC_COUNT; i++) {
        g_net.rx_buffers[i] = vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
        if (!g_net.rx_buffers[i]) return NET_ERR_GENERIC;
        g_net.rx_buffers_phys[i] =
            paging_get_physical_address((uint64_t)(uintptr_t)g_net.rx_buffers[i]);
        g_net.rx_descs[i].addr = g_net.rx_buffers_phys[i];
        g_net.rx_descs[i].status = 0;
    }

    for (int i = 0; i < NET_TX_DESC_COUNT; i++) {
        g_net.tx_buffers[i] = vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
        if (!g_net.tx_buffers[i]) return NET_ERR_GENERIC;
        g_net.tx_buffers_phys[i] =
            paging_get_physical_address((uint64_t)(uintptr_t)g_net.tx_buffers[i]);
        g_net.tx_descs[i].addr = g_net.tx_buffers_phys[i];
        g_net.tx_descs[i].status = E1000_TX_STATUS_DD;
    }

    return NET_OK;
}

static int e1000_init_rings(void) {
    if (e1000_alloc_dma() != NET_OK) return NET_ERR_GENERIC;

    e1000_write32(E1000_REG_IMC, 0xFFFFFFFFu);
    (void)e1000_read32(E1000_REG_ICR);

    e1000_write32(E1000_REG_RDBAL, (uint32_t)(g_net.rx_descs_phys & 0xFFFFFFFFu));
    e1000_write32(E1000_REG_RDBAH, (uint32_t)(g_net.rx_descs_phys >> 32));
    e1000_write32(E1000_REG_RDLEN, NET_RX_DESC_COUNT * sizeof(struct e1000_rx_desc));
    e1000_write32(E1000_REG_RDH, 0);
    e1000_write32(E1000_REG_RDT, NET_RX_DESC_COUNT - 1);

    e1000_write32(E1000_REG_TDBAL, (uint32_t)(g_net.tx_descs_phys & 0xFFFFFFFFu));
    e1000_write32(E1000_REG_TDBAH, (uint32_t)(g_net.tx_descs_phys >> 32));
    e1000_write32(E1000_REG_TDLEN, NET_TX_DESC_COUNT * sizeof(struct e1000_tx_desc));
    e1000_write32(E1000_REG_TDH, 0);
    e1000_write32(E1000_REG_TDT, 0);

    uint32_t ral = ((uint32_t)g_net.mac[0]) |
                   ((uint32_t)g_net.mac[1] << 8) |
                   ((uint32_t)g_net.mac[2] << 16) |
                   ((uint32_t)g_net.mac[3] << 24);
    uint32_t rah = ((uint32_t)g_net.mac[4]) |
                   ((uint32_t)g_net.mac[5] << 8) |
                   0x80000000u;
    e1000_write32(E1000_REG_RAL, ral);
    e1000_write32(E1000_REG_RAH, rah);

    for (uint32_t i = 0; i < 128; i++) {
        e1000_write32(E1000_REG_MTA + i * 4, 0);
    }

    e1000_write32(E1000_REG_TIPG, 0x0060200Au);
    e1000_write32(E1000_REG_TCTL,
                  E1000_TCTL_EN |
                  E1000_TCTL_PSP |
                  (0x10u << E1000_TCTL_CT_SHIFT) |
                  (0x40u << E1000_TCTL_COLD_SHIFT));
    e1000_write32(E1000_REG_RCTL,
                  E1000_RCTL_EN |
                  E1000_RCTL_BAM |
                  E1000_RCTL_SECRC);

    g_net.rx_index = 0;
    g_net.tx_index = 0;
    return NET_OK;
}

static int e1000_probe_device(void) {
    struct device_entry *devices[DEVICE_MAX_ENTRIES];
    int count = device_get_by_type(DEVICE_TYPE_NETWORK, devices, DEVICE_MAX_ENTRIES);

    for (int i = 0; i < count; i++) {
        struct device_entry *dev = devices[i];
        if (!dev || dev->bus != DEVICE_BUS_PCI) continue;
        if (dev->vendor_id != E1000_VENDOR_ID) continue;
        if (dev->device_id != E1000_DEVICE_ID_82540EM) continue;

        memset(&g_net, 0, sizeof(g_net));
        g_net.backend = NET_BACKEND_E1000;
        g_net.present = 1;
        g_net.pci_bus = dev->pci_bus;
        g_net.pci_slot = dev->pci_slot;
        g_net.pci_func = dev->pci_func;
        g_net.mmio_phys = (uint64_t)(dev->pci_bar[0] & ~0x0Fu);
        copy_name(g_net.interface_name, dev->name, sizeof(g_net.interface_name));
        copy_name(g_net.driver, "e1000", sizeof(g_net.driver));

        if (!g_net.mmio_phys) return NET_ERR_GENERIC;

        uint16_t pci_cmd = pci_config_read16(dev->pci_bus, dev->pci_slot,
                                             dev->pci_func, PCI_COMMAND_OFFSET);
        pci_cmd |= (PCI_COMMAND_MEMORY | PCI_COMMAND_BUSMASTER);
        pci_config_write16(dev->pci_bus, dev->pci_slot,
                           dev->pci_func, PCI_COMMAND_OFFSET, pci_cmd);

        if (net_map_mmio(g_net.mmio_phys, E1000_MMIO_MAP_SIZE, &g_net.mmio) != NET_OK) {
            return NET_ERR_GENERIC;
        }

        e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_RST);
        net_delay();
        e1000_write32(E1000_REG_CTRL, e1000_read32(E1000_REG_CTRL) | E1000_CTRL_SLU);
        e1000_read_mac(g_net.mac);

        if (e1000_init_rings() != NET_OK) return NET_ERR_GENERIC;

        g_net.link_up = (e1000_read32(E1000_REG_STATUS) & E1000_STATUS_LU) ? 1u : 0u;
        g_net.ready = 1;
        return NET_OK;
    }

    return NET_ERR_UNAVAILABLE;
}

static int pcnet_alloc_dma(void) {
    uint32_t phys32 = 0;

    g_net.pcnet_init = (struct pcnet_init_block *)vmm_alloc_pages(1,
                     PAGE_PRESENT | PAGE_WRITABLE);
    g_net.pcnet_rx_ring = (uint8_t *)vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
    g_net.pcnet_tx_ring = (uint8_t *)vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
    if (!g_net.pcnet_init || !g_net.pcnet_rx_ring || !g_net.pcnet_tx_ring) {
        return NET_ERR_GENERIC;
    }

    memset(g_net.pcnet_init, 0, PAGE_SIZE);
    memset(g_net.pcnet_rx_ring, 0, PAGE_SIZE);
    memset(g_net.pcnet_tx_ring, 0, PAGE_SIZE);

    g_net.pcnet_init_phys =
        paging_get_physical_address((uint64_t)(uintptr_t)g_net.pcnet_init);
    g_net.pcnet_rx_ring_phys =
        paging_get_physical_address((uint64_t)(uintptr_t)g_net.pcnet_rx_ring);
    g_net.pcnet_tx_ring_phys =
        paging_get_physical_address((uint64_t)(uintptr_t)g_net.pcnet_tx_ring);

    if (!net_phys32(g_net.pcnet_init_phys, &phys32)) return NET_ERR_GENERIC;
    if (!net_phys32(g_net.pcnet_rx_ring_phys, &phys32)) return NET_ERR_GENERIC;
    if (!net_phys32(g_net.pcnet_tx_ring_phys, &phys32)) return NET_ERR_GENERIC;

    for (int i = 0; i < NET_RX_DESC_COUNT; i++) {
        g_net.rx_buffers[i] = vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
        if (!g_net.rx_buffers[i]) return NET_ERR_GENERIC;
        g_net.rx_buffers_phys[i] =
            paging_get_physical_address((uint64_t)(uintptr_t)g_net.rx_buffers[i]);
        if (!net_phys32(g_net.rx_buffers_phys[i], &phys32)) return NET_ERR_GENERIC;
        pcnet_init_desc(g_net.pcnet_rx_ring, (uint32_t)i, phys32, 1);
    }

    for (int i = 0; i < NET_TX_DESC_COUNT; i++) {
        g_net.tx_buffers[i] = vmm_alloc_pages(1, PAGE_PRESENT | PAGE_WRITABLE);
        if (!g_net.tx_buffers[i]) return NET_ERR_GENERIC;
        g_net.tx_buffers_phys[i] =
            paging_get_physical_address((uint64_t)(uintptr_t)g_net.tx_buffers[i]);
        if (!net_phys32(g_net.tx_buffers_phys[i], &phys32)) return NET_ERR_GENERIC;
        pcnet_init_desc(g_net.pcnet_tx_ring, (uint32_t)i, phys32, 0);
    }

    return NET_OK;
}

static int pcnet_init_rings(void) {
    uint32_t rx_ring_phys32 = 0;
    uint32_t tx_ring_phys32 = 0;
    uint32_t init_phys32 = 0;
    uint16_t csr4 = 0;
    uint16_t bcr2 = 0;

    if (pcnet_alloc_dma() != NET_OK) return NET_ERR_GENERIC;
    if (!net_phys32(g_net.pcnet_rx_ring_phys, &rx_ring_phys32)) return NET_ERR_GENERIC;
    if (!net_phys32(g_net.pcnet_tx_ring_phys, &tx_ring_phys32)) return NET_ERR_GENERIC;
    if (!net_phys32(g_net.pcnet_init_phys, &init_phys32)) return NET_ERR_GENERIC;

    memset(g_net.pcnet_init, 0, sizeof(*g_net.pcnet_init));
    g_net.pcnet_init->mode = 0;
    g_net.pcnet_init->rlen = 5u << 4;
    g_net.pcnet_init->tlen = 3u << 4;
    memcpy(g_net.pcnet_init->phys_addr, g_net.mac, NET_MAC_ADDR_LEN);
    g_net.pcnet_init->rx_ring = rx_ring_phys32;
    g_net.pcnet_init->tx_ring = tx_ring_phys32;

    pcnet_write_bcr(20, PCNET_BCR20_SWSTYLE2);
    bcr2 = pcnet_read_bcr(2);
    pcnet_write_bcr(2, (uint16_t)(bcr2 | PCNET_BCR2_ASEL));

    csr4 = pcnet_read_csr(4);
    pcnet_write_csr(4, (uint16_t)(csr4 | PCNET_CSR4_AUTO_PAD_TX));
    pcnet_write_csr(3, PCNET_CSR3_DEFAULT);
    pcnet_write_csr(1, (uint16_t)(init_phys32 & 0xFFFFu));
    pcnet_write_csr(2, (uint16_t)((init_phys32 >> 16) & 0xFFFFu));
    pcnet_write_csr(0, PCNET_CSR0_INIT);

    {
        uint64_t deadline = timer_get_uptime_ms() + 200;
        while (timer_get_uptime_ms() < deadline) {
            uint16_t csr0 = pcnet_read_csr(0);
            if (csr0 & PCNET_CSR0_IDON) {
                pcnet_write_csr(0, PCNET_CSR0_IDON);
                pcnet_write_csr(0, PCNET_CSR0_STRT);
                g_net.rx_index = 0;
                g_net.tx_index = 0;
                g_net.link_up = 1;
                return NET_OK;
            }
        }
    }

    return NET_ERR_TIMEOUT;
}

static int pcnet_probe_device(void) {
    struct device_entry *devices[DEVICE_MAX_ENTRIES];
    int count = device_get_by_type(DEVICE_TYPE_NETWORK, devices, DEVICE_MAX_ENTRIES);

    for (int i = 0; i < count; i++) {
        struct device_entry *dev = devices[i];
        uint16_t pci_cmd;

        if (!dev || dev->bus != DEVICE_BUS_PCI) continue;
        if (dev->vendor_id != PCNET_VENDOR_ID) continue;
        if (dev->device_id != PCNET_DEVICE_ID) continue;

        memset(&g_net, 0, sizeof(g_net));
        g_net.backend = NET_BACKEND_PCNET;
        g_net.present = 1;
        g_net.pci_bus = dev->pci_bus;
        g_net.pci_slot = dev->pci_slot;
        g_net.pci_func = dev->pci_func;
        g_net.io_base = (uint16_t)(dev->pci_bar[0] & ~0x3u);
        copy_name(g_net.interface_name, dev->name, sizeof(g_net.interface_name));
        copy_name(g_net.driver, "pcnet", sizeof(g_net.driver));

        if (!g_net.io_base) return NET_ERR_GENERIC;

        pci_cmd = pci_config_read16(dev->pci_bus, dev->pci_slot,
                                    dev->pci_func, PCI_COMMAND_OFFSET);
        pci_cmd |= (PCI_COMMAND_IO | PCI_COMMAND_BUSMASTER);
        pci_config_write16(dev->pci_bus, dev->pci_slot,
                           dev->pci_func, PCI_COMMAND_OFFSET, pci_cmd);

        pcnet_reset();
        pcnet_read_mac(g_net.mac);
        if (pcnet_init_rings() != NET_OK) return NET_ERR_GENERIC;

        g_net.ready = 1;
        return NET_OK;
    }

    return NET_ERR_UNAVAILABLE;
}

static void dhcp_reset_state(void) {
    memset(&g_net.dhcp, 0, sizeof(g_net.dhcp));
}

static size_t dhcp_build_discover(struct net_dhcp_packet *packet) {
    uint8_t *opt;

    memset(packet, 0, sizeof(*packet));
    packet->op = 1;
    packet->htype = 1;
    packet->hlen = NET_MAC_ADDR_LEN;
    write_be32(&packet->xid, g_net.dhcp.xid);
    write_be16(&packet->flags, DHCP_FLAG_BROADCAST);
    memcpy(packet->chaddr, g_net.mac, NET_MAC_ADDR_LEN);
    write_be32(&packet->magic, DHCP_MAGIC_COOKIE);

    opt = packet->options;
    *opt++ = DHCP_OPT_MSG_TYPE; *opt++ = 1; *opt++ = DHCP_MSG_DISCOVER;
    *opt++ = DHCP_OPT_PARAM_LIST; *opt++ = 3;
    *opt++ = DHCP_OPT_SUBNET_MASK;
    *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_SERVER_ID;
    *opt++ = DHCP_OPT_END;

    return (size_t)((uintptr_t)opt - (uintptr_t)packet);
}

static size_t dhcp_build_request(struct net_dhcp_packet *packet) {
    uint8_t *opt;

    memset(packet, 0, sizeof(*packet));
    packet->op = 1;
    packet->htype = 1;
    packet->hlen = NET_MAC_ADDR_LEN;
    write_be32(&packet->xid, g_net.dhcp.xid);
    write_be16(&packet->flags, DHCP_FLAG_BROADCAST);
    memcpy(packet->chaddr, g_net.mac, NET_MAC_ADDR_LEN);
    write_be32(&packet->magic, DHCP_MAGIC_COOKIE);

    opt = packet->options;
    *opt++ = DHCP_OPT_MSG_TYPE; *opt++ = 1; *opt++ = DHCP_MSG_REQUEST;
    *opt++ = DHCP_OPT_REQUESTED_IP; *opt++ = 4;
    memcpy(opt, g_net.dhcp.offered_ip, NET_IPV4_ADDR_LEN); opt += NET_IPV4_ADDR_LEN;
    *opt++ = DHCP_OPT_SERVER_ID; *opt++ = 4;
    memcpy(opt, g_net.dhcp.server_id, NET_IPV4_ADDR_LEN); opt += NET_IPV4_ADDR_LEN;
    *opt++ = DHCP_OPT_PARAM_LIST; *opt++ = 3;
    *opt++ = DHCP_OPT_SUBNET_MASK;
    *opt++ = DHCP_OPT_ROUTER;
    *opt++ = DHCP_OPT_SERVER_ID;
    *opt++ = DHCP_OPT_END;

    return (size_t)((uintptr_t)opt - (uintptr_t)packet);
}

static void dhcp_configure_from_state(void) {
    memcpy(g_net.ipv4, g_net.dhcp.offered_ip, NET_IPV4_ADDR_LEN);
    memcpy(g_net.netmask, g_net.dhcp.subnet_mask, NET_IPV4_ADDR_LEN);
    memcpy(g_net.gateway, g_net.dhcp.router, NET_IPV4_ADDR_LEN);
    memcpy(g_net.dhcp_server, g_net.dhcp.server_id, NET_IPV4_ADDR_LEN);
    g_net.dhcp_configured = 1;
}

static void dhcp_parse_options(const uint8_t *opts, size_t len) {
    size_t i = 0;

    while (i < len) {
        uint8_t code = opts[i++];
        if (code == 0) continue;
        if (code == DHCP_OPT_END) break;
        if (i >= len) break;

        uint8_t opt_len = opts[i++];
        if (i + opt_len > len) break;

        switch (code) {
            case DHCP_OPT_MSG_TYPE:
                if (opt_len >= 1) g_net.dhcp.msg_type = opts[i];
                break;
            case DHCP_OPT_SERVER_ID:
                if (opt_len >= NET_IPV4_ADDR_LEN) {
                    memcpy(g_net.dhcp.server_id, &opts[i], NET_IPV4_ADDR_LEN);
                }
                break;
            case DHCP_OPT_SUBNET_MASK:
                if (opt_len >= NET_IPV4_ADDR_LEN) {
                    memcpy(g_net.dhcp.subnet_mask, &opts[i], NET_IPV4_ADDR_LEN);
                }
                break;
            case DHCP_OPT_ROUTER:
                if (opt_len >= NET_IPV4_ADDR_LEN) {
                    memcpy(g_net.dhcp.router, &opts[i], NET_IPV4_ADDR_LEN);
                }
                break;
            case DHCP_OPT_LEASE_TIME:
                if (opt_len >= 4) {
                    g_net.dhcp.lease_seconds = read_be32(&opts[i]);
                }
                break;
            default:
                break;
        }

        i += opt_len;
    }
}

static void net_handle_dhcp(const uint8_t *payload, size_t payload_len) {
    const struct net_dhcp_packet *packet = (const struct net_dhcp_packet *)payload;

    if (payload_len < 240 || !packet) return;
    if (packet->op != 2 || packet->htype != 1 || packet->hlen != NET_MAC_ADDR_LEN) return;
    if (read_be32(&packet->xid) != g_net.dhcp.xid) return;
    if (memcmp(packet->chaddr, g_net.mac, NET_MAC_ADDR_LEN) != 0) return;
    if (read_be32(&packet->magic) != DHCP_MAGIC_COOKIE) return;

    dhcp_parse_options(packet->options, payload_len - 240);
    memcpy(g_net.dhcp.offered_ip, &packet->yiaddr, NET_IPV4_ADDR_LEN);

    if (g_net.dhcp.msg_type == DHCP_MSG_OFFER) {
        g_net.dhcp.offer_valid = 1;
    } else if (g_net.dhcp.msg_type == DHCP_MSG_ACK) {
        g_net.dhcp.ack_received = 1;
        dhcp_configure_from_state();
    } else if (g_net.dhcp.msg_type == DHCP_MSG_NAK) {
        g_net.dhcp.nak_received = 1;
    }
}

static void net_handle_icmp(const uint8_t src_ip[NET_IPV4_ADDR_LEN],
                            const struct net_ipv4_header *ip,
                            const uint8_t *payload,
                            size_t payload_len) {
    const struct net_icmp_echo *icmp = (const struct net_icmp_echo *)payload;

    if (payload_len < sizeof(*icmp)) return;

    if (icmp->type == ICMP_TYPE_ECHO_REQUEST &&
        ip_equal(ip->dst, g_net.ipv4)) {
        (void)net_send_icmp_echo_reply(src_ip, payload, payload_len);
        return;
    }

    if (icmp->type == ICMP_TYPE_ECHO_REPLY && g_net.ping.active) {
        uint16_t ident = read_be16(&icmp->ident);
        uint16_t seq = read_be16(&icmp->seq);
        if (ident == g_net.ping.ident &&
            seq == g_net.ping.seq &&
            ip_equal(src_ip, g_net.ping.target_ip)) {
            g_net.ping.active = 0;
            g_net.ping.done = 1;
            g_net.ping.success = 1;
            g_net.ping.ttl = ip->ttl;
        }
    }
}

static void net_handle_udp(const uint8_t *payload, size_t payload_len) {
    const struct net_udp_header *udp = (const struct net_udp_header *)payload;
    size_t udp_len;

    if (payload_len < sizeof(*udp)) return;
    udp_len = read_be16(&udp->length);
    if (udp_len < sizeof(*udp) || udp_len > payload_len) return;

    if (read_be16(&udp->dst_port) == DHCP_CLIENT_PORT &&
        read_be16(&udp->src_port) == DHCP_SERVER_PORT) {
        net_handle_dhcp(payload + sizeof(*udp), udp_len - sizeof(*udp));
    }
}

static void tcp_conn_update_ack(struct net_tcp_conn *conn, uint32_t ack_num) {
    if (!conn || !tcp_conn_ack_valid(conn, ack_num)) return;
    if (tcp_seq_after(ack_num, conn->snd_una)) {
        conn->snd_una = ack_num;
    }

    if (conn->state == NET_TCP_FIN_WAIT_1 && conn->snd_una == conn->snd_nxt) {
        conn->state = conn->remote_closed ? NET_TCP_CLOSED : NET_TCP_FIN_WAIT_2;
    } else if (conn->state == NET_TCP_CLOSING && conn->snd_una == conn->snd_nxt) {
        conn->state = NET_TCP_CLOSED;
    } else if (conn->state == NET_TCP_LAST_ACK && conn->snd_una == conn->snd_nxt) {
        conn->state = NET_TCP_CLOSED;
    }
}

static void tcp_conn_send_ack(struct net_tcp_conn *conn) {
    if (!conn) return;
    (void)net_send_tcp_segment(conn, TCP_FLAG_ACK, NULL, 0);
}

static void tcp_conn_handle_fin(struct net_tcp_conn *conn) {
    if (!conn) return;

    conn->remote_closed = 1;
    conn->rcv_nxt++;
    tcp_conn_send_ack(conn);

    if (conn->state == NET_TCP_ESTABLISHED) {
        conn->state = NET_TCP_CLOSE_WAIT;
    } else if (conn->state == NET_TCP_FIN_WAIT_1) {
        conn->state = (conn->snd_una == conn->snd_nxt) ? NET_TCP_CLOSED : NET_TCP_CLOSING;
    } else if (conn->state == NET_TCP_FIN_WAIT_2) {
        conn->state = NET_TCP_CLOSED;
    }
}

static void net_handle_tcp(const uint8_t src_ip[NET_IPV4_ADDR_LEN],
                           const uint8_t *payload,
                           size_t payload_len) {
    const struct net_tcp_header *tcp = (const struct net_tcp_header *)payload;
    struct net_tcp_conn *conn;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t flags;
    uint32_t seq_num;
    uint32_t ack_num;
    size_t header_len;
    size_t data_len;

    if (payload_len < sizeof(*tcp)) return;

    header_len = (size_t)((tcp->data_offset >> 4) & 0x0Fu) * 4u;
    if (header_len < sizeof(*tcp) || header_len > payload_len) return;

    src_port = read_be16(&tcp->src_port);
    dst_port = read_be16(&tcp->dst_port);
    seq_num = read_be32(&tcp->seq_num);
    ack_num = read_be32(&tcp->ack_num);
    flags = tcp->flags;
    data_len = payload_len - header_len;

    conn = tcp_conn_find(src_ip, src_port, dst_port);
    if (!conn) return;

    conn->last_activity_ms = timer_get_uptime_ms();

    if (flags & TCP_FLAG_RST) {
        conn->reset = 1;
        conn->state = NET_TCP_RESET;
        conn->remote_closed = 1;
        return;
    }

    if (conn->state == NET_TCP_SYN_SENT) {
        if ((flags & (TCP_FLAG_SYN | TCP_FLAG_ACK)) != (TCP_FLAG_SYN | TCP_FLAG_ACK)) {
            return;
        }
        if (!tcp_conn_ack_valid(conn, ack_num) || ack_num != conn->snd_nxt) {
            return;
        }

        conn->snd_una = ack_num;
        conn->rcv_nxt = seq_num + 1u;
        conn->state = NET_TCP_ESTABLISHED;
        tcp_conn_send_ack(conn);
        return;
    }

    if (flags & TCP_FLAG_ACK) {
        tcp_conn_update_ack(conn, ack_num);
    }

    if (data_len > 0) {
        if (seq_num == conn->rcv_nxt) {
            if (tcp_conn_queue(conn, payload + header_len, data_len)) {
                conn->rcv_nxt += (uint32_t)data_len;
                tcp_conn_send_ack(conn);
            } else {
                tcp_conn_send_ack(conn);
            }
        } else {
            tcp_conn_send_ack(conn);
        }
    }

    if (flags & TCP_FLAG_FIN) {
        uint32_t fin_seq = seq_num + (uint32_t)data_len;
        if (fin_seq == conn->rcv_nxt) {
            tcp_conn_handle_fin(conn);
        } else {
            tcp_conn_send_ack(conn);
        }
    }
}

static void net_handle_ipv4(const uint8_t src_mac[NET_MAC_ADDR_LEN],
                            const uint8_t *frame,
                            size_t frame_len) {
    const struct net_ipv4_header *ip = (const struct net_ipv4_header *)frame;
    size_t ihl = (size_t)(ip->version_ihl & 0x0Fu) * 4u;
    size_t total_len;
    uint8_t src_ip[NET_IPV4_ADDR_LEN];

    if (frame_len < sizeof(*ip) || ihl < sizeof(*ip) || frame_len < ihl) return;
    if ((ip->version_ihl >> 4) != 4) return;

    total_len = read_be16(&ip->total_length);
    if (total_len < ihl || total_len > frame_len) return;

    memcpy(src_ip, ip->src, NET_IPV4_ADDR_LEN);
    arp_cache_update(src_ip, src_mac);

    if (ip->protocol == IPV4_PROTO_ICMP) {
        net_handle_icmp(src_ip, ip, frame + ihl, total_len - ihl);
    } else if (ip->protocol == IPV4_PROTO_TCP) {
        net_handle_tcp(src_ip, frame + ihl, total_len - ihl);
    } else if (ip->protocol == IPV4_PROTO_UDP) {
        net_handle_udp(frame + ihl, total_len - ihl);
    }
}

static void net_handle_arp(const uint8_t *frame, size_t frame_len) {
    const struct net_arp_packet *arp = (const struct net_arp_packet *)frame;
    uint16_t opcode;

    if (frame_len < sizeof(*arp)) return;
    if (read_be16(&arp->htype) != 1) return;
    if (read_be16(&arp->ptype) != ETH_TYPE_IPV4) return;
    if (arp->hlen != NET_MAC_ADDR_LEN || arp->plen != NET_IPV4_ADDR_LEN) return;

    arp_cache_update(arp->spa, arp->sha);
    opcode = read_be16(&arp->oper);

    if (opcode == ARP_OP_REQUEST &&
        g_net.dhcp_configured &&
        ip_equal(arp->tpa, g_net.ipv4)) {
        (void)net_send_arp(ARP_OP_REPLY, arp->sha, arp->spa);
    }
}

static void net_process_frame(uint8_t *buffer, size_t len) {
    if (len < sizeof(struct net_eth_header)) return;

    struct net_eth_header *eth = (struct net_eth_header *)buffer;
    size_t eth_payload_len = len - sizeof(*eth);
    if (read_be16(&eth->ethertype) == ETH_TYPE_ARP) {
        net_handle_arp(buffer + sizeof(*eth), eth_payload_len);
    } else if (read_be16(&eth->ethertype) == ETH_TYPE_IPV4) {
        net_handle_ipv4(eth->src, buffer + sizeof(*eth), eth_payload_len);
    }
}

void net_poll(void) {
    if (!g_net.ready) return;

    if (g_net.backend == NET_BACKEND_E1000) {
        g_net.link_up = (e1000_read32(E1000_REG_STATUS) & E1000_STATUS_LU) ? 1u : 0u;

        while (g_net.rx_descs[g_net.rx_index].status & E1000_RX_STATUS_DD) {
            struct e1000_rx_desc *desc = &g_net.rx_descs[g_net.rx_index];
            uint8_t *buffer = (uint8_t *)g_net.rx_buffers[g_net.rx_index];
            size_t len = desc->length;

            net_process_frame(buffer, len);

            g_net.rx_packets++;
            g_net.rx_bytes += len;
            desc->status = 0;
            e1000_write32(E1000_REG_RDT, g_net.rx_index);
            g_net.rx_index = (g_net.rx_index + 1u) % NET_RX_DESC_COUNT;
        }
        return;
    }

    if (g_net.backend == NET_BACKEND_PCNET) {
        uint16_t csr0 = pcnet_read_csr(0);
        g_net.link_up = (csr0 & (PCNET_CSR0_RXON | PCNET_CSR0_TXON)) ==
                        (PCNET_CSR0_RXON | PCNET_CSR0_TXON);

        while (pcnet_driver_owns(g_net.pcnet_rx_ring, g_net.rx_index)) {
            size_t off = pcnet_ring_offset(g_net.rx_index);
            uint8_t status = g_net.pcnet_rx_ring[off + 7];
            uint16_t plen = 0;
            uint8_t *buffer = (uint8_t *)g_net.rx_buffers[g_net.rx_index];

            memcpy(&plen, g_net.pcnet_rx_ring + off + 8, sizeof(plen));
            if ((status & PCNET_DESC_ERR) == 0 &&
                (status & (PCNET_DESC_STP | PCNET_DESC_ENP)) ==
                (PCNET_DESC_STP | PCNET_DESC_ENP)) {
                size_t len = plen;
                if (len > 4) len -= 4;
                net_process_frame(buffer, len);
                g_net.rx_packets++;
                g_net.rx_bytes += len;
            }

            memset(g_net.pcnet_rx_ring + off + 8, 0, 8);
            g_net.pcnet_rx_ring[off + 7] = PCNET_DESC_OWN;
            g_net.rx_index = (g_net.rx_index + 1u) % NET_RX_DESC_COUNT;
        }

        if (csr0 & PCNET_CSR0_ACK_BITS) {
            pcnet_write_csr(0, (uint16_t)(csr0 & PCNET_CSR0_ACK_BITS));
        }
    }
}

int net_is_available(void) {
    return g_net.ready ? 1 : 0;
}

int net_get_info(struct net_info *out) {
    if (!out) return NET_ERR_INVALID;
    memset(out, 0, sizeof(*out));
    if (!g_net.present) return NET_ERR_UNAVAILABLE;

    out->present = g_net.present;
    out->link_up = g_net.link_up;
    out->dhcp_configured = g_net.dhcp_configured;
    memcpy(out->mac, g_net.mac, NET_MAC_ADDR_LEN);
    memcpy(out->ipv4, g_net.ipv4, NET_IPV4_ADDR_LEN);
    memcpy(out->netmask, g_net.netmask, NET_IPV4_ADDR_LEN);
    memcpy(out->gateway, g_net.gateway, NET_IPV4_ADDR_LEN);
    memcpy(out->dhcp_server, g_net.dhcp_server, NET_IPV4_ADDR_LEN);
    out->pci_bus = g_net.pci_bus;
    out->pci_slot = g_net.pci_slot;
    out->pci_func = g_net.pci_func;
    out->rx_packets = g_net.rx_packets;
    out->tx_packets = g_net.tx_packets;
    out->rx_bytes = g_net.rx_bytes;
    out->tx_bytes = g_net.tx_bytes;
    copy_name(out->driver, g_net.driver, sizeof(out->driver));
    copy_name(out->interface_name, g_net.interface_name, sizeof(out->interface_name));
    return NET_OK;
}

int net_request_dhcp(uint32_t timeout_ms) {
    struct net_dhcp_packet packet;
    uint8_t broadcast_ip[NET_IPV4_ADDR_LEN] = {255, 255, 255, 255};
    size_t packet_len;
    uint64_t half_deadline;
    uint64_t final_deadline;
    uint32_t wait_ms = timeout_ms ? timeout_ms : 4000u;

    if (!g_net.ready) return NET_ERR_UNAVAILABLE;

    memset(g_net.ipv4, 0, sizeof(g_net.ipv4));
    memset(g_net.netmask, 0, sizeof(g_net.netmask));
    memset(g_net.gateway, 0, sizeof(g_net.gateway));
    memset(g_net.dhcp_server, 0, sizeof(g_net.dhcp_server));
    g_net.dhcp_configured = 0;

    dhcp_reset_state();
    g_net.dhcp.xid = ((uint32_t)timer_get_uptime_ms() << 16) ^
                     ((uint32_t)g_net.mac[4] << 8) ^
                     g_net.mac[5];
    if (g_net.dhcp.xid == 0) g_net.dhcp.xid = 0x4E554D31u;

    packet_len = dhcp_build_discover(&packet);
    if (net_send_udp(broadcast_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                     &packet, packet_len) != NET_OK) {
        return NET_ERR_GENERIC;
    }

    half_deadline = timer_get_uptime_ms() + (wait_ms / 2u);
    while (timer_get_uptime_ms() < half_deadline) {
        net_poll();
        if (g_net.dhcp.offer_valid) break;
    }
    if (!g_net.dhcp.offer_valid) return NET_ERR_TIMEOUT;

    packet_len = dhcp_build_request(&packet);
    if (net_send_udp(broadcast_ip, DHCP_CLIENT_PORT, DHCP_SERVER_PORT,
                     &packet, packet_len) != NET_OK) {
        return NET_ERR_GENERIC;
    }

    final_deadline = timer_get_uptime_ms() + wait_ms;
    while (timer_get_uptime_ms() < final_deadline) {
        net_poll();
        if (g_net.dhcp.ack_received) return NET_OK;
        if (g_net.dhcp.nak_received) return NET_ERR_GENERIC;
    }

    return NET_ERR_TIMEOUT;
}

int net_ping_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                  uint32_t timeout_ms,
                  struct net_ping_result *out) {
    uint16_t ident;
    uint16_t seq;
    uint64_t deadline;
    uint32_t wait_ms = timeout_ms ? timeout_ms : 1000u;

    if (!addr || !out) return NET_ERR_INVALID;
    if (!g_net.ready) return NET_ERR_UNAVAILABLE;
    if (!g_net.dhcp_configured) return NET_ERR_NOT_CONFIGURED;

    ident = (uint16_t)(((uint16_t)g_net.mac[4] << 8) | g_net.mac[5]);
    seq = ++g_net.next_ping_seq;
    memset(&g_net.ping, 0, sizeof(g_net.ping));
    g_net.ping.active = 1;
    g_net.ping.ident = ident;
    g_net.ping.seq = seq;
    g_net.ping.start_ms = timer_get_uptime_ms();
    memcpy(g_net.ping.target_ip, addr, NET_IPV4_ADDR_LEN);

    if (net_send_icmp_echo_request(addr, ident, seq) != NET_OK) {
        memset(&g_net.ping, 0, sizeof(g_net.ping));
        return NET_ERR_GENERIC;
    }

    deadline = timer_get_uptime_ms() + wait_ms;
    while (timer_get_uptime_ms() < deadline) {
        net_poll();
        if (g_net.ping.done) break;
    }

    memset(out, 0, sizeof(*out));
    out->seq = seq;
    if (!g_net.ping.done || !g_net.ping.success) {
        memset(&g_net.ping, 0, sizeof(g_net.ping));
        return NET_ERR_TIMEOUT;
    }

    out->success = 1;
    out->ttl = g_net.ping.ttl;
    out->roundtrip_ms = (uint32_t)(timer_get_uptime_ms() - g_net.ping.start_ms);
    memset(&g_net.ping, 0, sizeof(g_net.ping));
    return NET_OK;
}

int net_tcp_connect_ipv4(const uint8_t addr[NET_IPV4_ADDR_LEN],
                         uint16_t port,
                         uint32_t timeout_ms) {
    struct net_tcp_conn *conn;
    struct process *proc = scheduler_current();
    uint64_t deadline;
    uint64_t resend_at = 0;
    uint32_t wait_ms = timeout_ms ? timeout_ms : NET_TCP_DEFAULT_TIMEOUT;
    uint16_t local_port;

    if (!addr || port == 0) return NET_ERR_INVALID;
    if (!g_net.ready) return NET_ERR_UNAVAILABLE;
    if (!g_net.dhcp_configured) return NET_ERR_NOT_CONFIGURED;

    local_port = tcp_pick_local_port();
    if (local_port == 0) return NET_ERR_GENERIC;

    conn = tcp_conn_alloc();
    if (!conn) return NET_ERR_GENERIC;

    conn->owner_pid = proc ? proc->pid : 0;
    conn->local_port = local_port;
    conn->remote_port = port;
    memcpy(conn->remote_ip, addr, NET_IPV4_ADDR_LEN);
    conn->iss = ((uint32_t)timer_get_uptime_ms() << 12) ^
                ((uint32_t)local_port << 4) ^
                port ^
                g_net.next_ip_id;
    if (conn->iss == 0) conn->iss = 0x4E554D31u;
    conn->snd_una = conn->iss;
    conn->snd_nxt = conn->iss;
    conn->state = NET_TCP_SYN_SENT;
    conn->last_activity_ms = timer_get_uptime_ms();

    deadline = timer_get_uptime_ms() + wait_ms;
    while (timer_get_uptime_ms() < deadline) {
        uint64_t now = timer_get_uptime_ms();

        if (conn->state == NET_TCP_ESTABLISHED) {
            return (int)(conn - g_net.tcp) + 1;
        }
        if (conn->state == NET_TCP_RESET) {
            tcp_conn_release(conn);
            return NET_ERR_GENERIC;
        }

        if (now >= resend_at) {
            conn->snd_nxt = conn->snd_una;
            if (net_send_tcp_segment(conn, TCP_FLAG_SYN, NULL, 0) != NET_OK) {
                tcp_conn_release(conn);
                return NET_ERR_GENERIC;
            }
            resend_at = now + 250u;
        }

        net_poll();
        schedule();
    }

    tcp_conn_release(conn);
    return NET_ERR_TIMEOUT;
}

ssize_t net_tcp_send(int handle, const void *buf, size_t len, uint32_t timeout_ms) {
    struct net_tcp_conn *conn = tcp_conn_from_handle(handle);
    const uint8_t *bytes = (const uint8_t *)buf;
    size_t total_sent = 0;
    uint32_t wait_ms = timeout_ms ? timeout_ms : NET_TCP_DEFAULT_TIMEOUT;

    if (!conn || !buf) return NET_ERR_INVALID;
    if (conn->state != NET_TCP_ESTABLISHED) return NET_ERR_CLOSED;
    if (conn->reset) return NET_ERR_GENERIC;

    while (total_sent < len) {
        size_t chunk = len - total_sent;
        uint32_t expected_ack;
        uint64_t deadline;
        uint64_t resend_at = 0;

        if (chunk > NET_TCP_TX_MSS) chunk = NET_TCP_TX_MSS;
        expected_ack = conn->snd_una + (uint32_t)chunk;
        deadline = timer_get_uptime_ms() + wait_ms;

        while (timer_get_uptime_ms() < deadline) {
            uint64_t now = timer_get_uptime_ms();

            if (conn->reset) {
                return total_sent ? (ssize_t)total_sent : NET_ERR_GENERIC;
            }
            if (conn->state != NET_TCP_ESTABLISHED && conn->state != NET_TCP_CLOSE_WAIT) {
                return total_sent ? (ssize_t)total_sent : NET_ERR_CLOSED;
            }
            if (conn->remote_closed) {
                return total_sent ? (ssize_t)total_sent : NET_ERR_CLOSED;
            }

            if (now >= resend_at) {
                conn->snd_nxt = conn->snd_una;
                if (net_send_tcp_segment(conn, TCP_FLAG_ACK | TCP_FLAG_PSH,
                                         bytes + total_sent, chunk) != NET_OK) {
                    return total_sent ? (ssize_t)total_sent : NET_ERR_GENERIC;
                }
                resend_at = now + 300u;
            }

            if (!tcp_seq_before(conn->snd_una, expected_ack)) break;

            net_poll();
            schedule();
        }

        if (tcp_seq_before(conn->snd_una, expected_ack)) {
            return total_sent ? (ssize_t)total_sent : NET_ERR_TIMEOUT;
        }

        total_sent += chunk;
    }

    return (ssize_t)total_sent;
}

ssize_t net_tcp_recv(int handle, void *buf, size_t len, uint32_t timeout_ms) {
    struct net_tcp_conn *conn = tcp_conn_from_handle(handle);
    uint8_t *out = (uint8_t *)buf;
    uint32_t wait_ms = timeout_ms ? timeout_ms : NET_TCP_DEFAULT_TIMEOUT;
    uint64_t deadline;

    if (!conn || !buf) return NET_ERR_INVALID;

    if (tcp_conn_rx_len(conn) > 0) {
        return (ssize_t)tcp_conn_dequeue(conn, out, len);
    }
    if (conn->reset) return NET_ERR_GENERIC;
    if (conn->remote_closed || conn->state == NET_TCP_CLOSED) return 0;

    deadline = timer_get_uptime_ms() + wait_ms;
    while (timer_get_uptime_ms() < deadline) {
        net_poll();
        if (tcp_conn_rx_len(conn) > 0) {
            return (ssize_t)tcp_conn_dequeue(conn, out, len);
        }
        if (conn->reset) return NET_ERR_GENERIC;
        if (conn->remote_closed || conn->state == NET_TCP_CLOSED) return 0;
        schedule();
    }

    return NET_ERR_TIMEOUT;
}

int net_tcp_close(int handle, uint32_t timeout_ms) {
    struct net_tcp_conn *conn = tcp_conn_from_handle(handle);
    uint32_t wait_ms = timeout_ms ? timeout_ms : NET_TCP_DEFAULT_TIMEOUT;
    uint64_t deadline;

    if (!conn) return NET_ERR_INVALID;

    if (conn->state == NET_TCP_RESET || conn->state == NET_TCP_CLOSED) {
        tcp_conn_release(conn);
        return NET_OK;
    }

    if (conn->state == NET_TCP_ESTABLISHED) {
        if (net_send_tcp_segment(conn, TCP_FLAG_ACK | TCP_FLAG_FIN, NULL, 0) != NET_OK) {
            return NET_ERR_GENERIC;
        }
        conn->state = NET_TCP_FIN_WAIT_1;
    } else if (conn->state == NET_TCP_CLOSE_WAIT) {
        if (net_send_tcp_segment(conn, TCP_FLAG_ACK | TCP_FLAG_FIN, NULL, 0) != NET_OK) {
            return NET_ERR_GENERIC;
        }
        conn->state = NET_TCP_LAST_ACK;
    }

    deadline = timer_get_uptime_ms() + wait_ms;
    while (timer_get_uptime_ms() < deadline) {
        net_poll();
        if (conn->state == NET_TCP_CLOSED || conn->state == NET_TCP_RESET) {
            tcp_conn_release(conn);
            return NET_OK;
        }
        schedule();
    }

    return NET_ERR_TIMEOUT;
}

int net_tcp_get_info(int handle, struct net_tcp_info *out) {
    struct net_tcp_conn *conn = tcp_conn_from_handle(handle);

    if (!conn || !out) return NET_ERR_INVALID;

    memset(out, 0, sizeof(*out));
    out->state = conn->state;
    out->reset = conn->reset;
    out->remote_closed = conn->remote_closed;
    out->local_port = conn->local_port;
    out->remote_port = conn->remote_port;
    out->recv_ready = tcp_conn_rx_len(conn);
    out->send_ready = NET_TCP_TX_MSS;
    memcpy(out->remote_ip, conn->remote_ip, NET_IPV4_ADDR_LEN);
    return NET_OK;
}

void net_print_status(void) {
    char ip_buf[16];
    char mask_buf[16];
    char gw_buf[16];
    char mac_buf[18];

    if (!g_net.present) {
        vga_writestring("NET: No supported network controller detected\n");
        return;
    }

    mac_to_str(g_net.mac, mac_buf, sizeof(mac_buf));
    ipv4_to_str(g_net.ipv4, ip_buf, sizeof(ip_buf));
    ipv4_to_str(g_net.netmask, mask_buf, sizeof(mask_buf));
    ipv4_to_str(g_net.gateway, gw_buf, sizeof(gw_buf));

    vga_writestring("NET: ");
    vga_writestring(g_net.interface_name);
    vga_writestring(" driver=");
    vga_writestring(g_net.driver);
    vga_writestring(" link=");
    vga_writestring(g_net.link_up ? "up" : "down");
    vga_writestring("\nNET: mac=");
    vga_writestring(mac_buf);
    vga_writestring(" ip=");
    vga_writestring(ip_buf);
    vga_writestring(" mask=");
    vga_writestring(mask_buf);
    vga_writestring(" gw=");
    vga_writestring(gw_buf);
    vga_writestring("\nNET: rx=");
    print_dec(g_net.rx_packets);
    vga_writestring(" tx=");
    print_dec(g_net.tx_packets);
    vga_writestring("\n");
}

void net_init(void) {
    memset(&g_net, 0, sizeof(g_net));
    g_net.next_tcp_port = NET_TCP_EPHEMERAL_BASE;

    if (e1000_probe_device() != NET_OK &&
        pcnet_probe_device() != NET_OK) {
        net_print_virtualbox_pcnet_hint();
        vga_writestring("NET: No supported NIC detected\n");
        return;
    }

    vga_writestring("NET: ");
    vga_writestring(g_net.interface_name);
    vga_writestring(" ready, MAC ");
    {
        char mac_buf[18];
        mac_to_str(g_net.mac, mac_buf, sizeof(mac_buf));
        vga_writestring(mac_buf);
    }
    vga_writestring("\n");

    vga_writestring("NET: DHCP idle, run connect --dhcp to configure IPv4\n");
}
