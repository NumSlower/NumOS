#include "libc.h"
#include "syscalls.h"

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, strlen(s));
}

static void write_ch(char c) {
    sys_write(FD_STDOUT, &c, 1);
}

static void write_dec(uint64_t value) {
    char buf[32];
    char tmp[32];
    int pos = 0;
    int t = 0;

    if (value == 0) {
        write_ch('0');
        return;
    }

    while (value > 0 && t < (int)sizeof(tmp)) {
        tmp[t++] = (char)('0' + (value % 10));
        value /= 10;
    }
    while (t > 0) buf[pos++] = tmp[--t];
    sys_write(FD_STDOUT, buf, (size_t)pos);
}

static void write_ipv4(const uint8_t ip[4]) {
    write_dec(ip[0]);
    write_ch('.');
    write_dec(ip[1]);
    write_ch('.');
    write_dec(ip[2]);
    write_ch('.');
    write_dec(ip[3]);
}

static void write_mac(const uint8_t mac[6]) {
    static const char hex[] = "0123456789ABCDEF";
    char out[17];
    int pos = 0;

    for (int i = 0; i < 6; i++) {
        out[pos++] = hex[(mac[i] >> 4) & 0xF];
        out[pos++] = hex[mac[i] & 0xF];
        if (i != 5) out[pos++] = ':';
    }

    sys_write(FD_STDOUT, out, (size_t)pos);
}

static int parse_ipv4(const char *s, uint8_t out[4]) {
    int part = 0;
    int value = 0;
    int have_digit = 0;

    while (*s) {
        if (*s >= '0' && *s <= '9') {
            value = value * 10 + (*s - '0');
            if (value > 255) return 0;
            have_digit = 1;
        } else if (*s == '.') {
            if (!have_digit || part >= 3) return 0;
            out[part++] = (uint8_t)value;
            value = 0;
            have_digit = 0;
        } else {
            return 0;
        }
        s++;
    }

    if (!have_digit || part != 3) return 0;
    out[part] = (uint8_t)value;
    return 1;
}

static int load_info(struct numos_net_info *info) {
    if (sys_net_info(info) != 0 || !info->present) {
        write_str("net: no supported NIC detected\n");
        return 0;
    }
    return 1;
}

static void print_help(void) {
    write_str("usage: net help\n");
    write_str("       net show\n");
    write_str("       net link\n");
    write_str("       net mac\n");
    write_str("       net ipv4\n");
    write_str("       net mask\n");
    write_str("       net gateway\n");
    write_str("       net dhcp\n");
    write_str("       net server\n");
    write_str("       net pci\n");
    write_str("       net stats\n");
    write_str("       net ping A.B.C.D\n");
}

static void print_show(const struct numos_net_info *info) {
    write_str("interface ");
    write_str(info->interface_name);
    write_str("\n");
    write_str("driver ");
    write_str(info->driver);
    write_str("\n");
    write_str("link ");
    write_str(info->link_up ? "up" : "down");
    write_str("\n");
    write_str("mac ");
    write_mac(info->mac);
    write_str("\n");
    write_str("ipv4 ");
    write_ipv4(info->ipv4);
    write_str("\n");
    write_str("mask ");
    write_ipv4(info->netmask);
    write_str("\n");
    write_str("gateway ");
    write_ipv4(info->gateway);
    write_str("\n");
    write_str("dhcp ");
    write_str(info->dhcp_configured ? "configured" : "not-configured");
    write_str("\n");
    write_str("server ");
    write_ipv4(info->dhcp_server);
    write_str("\n");
    write_str("pci ");
    write_dec(info->pci_bus);
    write_ch(':');
    write_dec(info->pci_slot);
    write_ch('.');
    write_dec(info->pci_func);
    write_str("\n");
    write_str("rx ");
    write_dec(info->rx_packets);
    write_str(" packets, ");
    write_dec(info->rx_bytes);
    write_str(" bytes\n");
    write_str("tx ");
    write_dec(info->tx_packets);
    write_str(" packets, ");
    write_dec(info->tx_bytes);
    write_str(" bytes\n");
}

static int command_ping(const char *arg) {
    uint8_t ip[4];
    struct numos_net_ping_result result;

    if (!arg || !parse_ipv4(arg, ip)) {
        write_str("net: invalid IPv4 address\n");
        return 1;
    }

    if (sys_net_ping(ip, 2000, &result) == 0 && result.success) {
        write_str("reply from ");
        write_ipv4(ip);
        write_str(": seq=");
        write_dec(result.seq);
        write_str(" ttl=");
        write_dec(result.ttl);
        write_str(" time=");
        write_dec(result.roundtrip_ms);
        write_str(" ms\n");
        return 0;
    }

    write_str("net: ping timeout\n");
    return 1;
}

int main(void) {
    char cmdline[128];
    struct numos_net_info info;

    if (sys_get_cmdline(cmdline, sizeof(cmdline)) < 0 ||
        cmdline[0] == '\0') {
        print_help();
        return 0;
    }

    if (strcmp(cmdline, "help") == 0) {
        print_help();
        return 0;
    }

    if (strcmp(cmdline, "dhcp") == 0 || strcmp(cmdline, "renew") == 0) {
        if (sys_net_dhcp(4000) == 0) {
            write_str("net: DHCP lease acquired\n");
            if (load_info(&info)) print_show(&info);
            return 0;
        }
        write_str("net: DHCP request failed\n");
        return 1;
    }

    if (strncmp(cmdline, "ping ", 5) == 0) {
        return command_ping(cmdline + 5);
    }

    if (!load_info(&info)) return 1;

    if (strcmp(cmdline, "show") == 0 || strcmp(cmdline, "status") == 0) {
        print_show(&info);
        return 0;
    }

    if (strcmp(cmdline, "link") == 0) {
        write_str(info.link_up ? "up\n" : "down\n");
        return 0;
    }

    if (strcmp(cmdline, "mac") == 0) {
        write_mac(info.mac);
        write_str("\n");
        return 0;
    }

    if (strcmp(cmdline, "ipv4") == 0) {
        write_ipv4(info.ipv4);
        write_str("\n");
        return 0;
    }

    if (strcmp(cmdline, "mask") == 0) {
        write_ipv4(info.netmask);
        write_str("\n");
        return 0;
    }

    if (strcmp(cmdline, "gateway") == 0) {
        write_ipv4(info.gateway);
        write_str("\n");
        return 0;
    }

    if (strcmp(cmdline, "server") == 0) {
        write_ipv4(info.dhcp_server);
        write_str("\n");
        return 0;
    }

    if (strcmp(cmdline, "pci") == 0) {
        write_dec(info.pci_bus);
        write_ch(':');
        write_dec(info.pci_slot);
        write_ch('.');
        write_dec(info.pci_func);
        write_str("\n");
        return 0;
    }

    if (strcmp(cmdline, "stats") == 0) {
        write_str("rx ");
        write_dec(info.rx_packets);
        write_str(" packets, ");
        write_dec(info.rx_bytes);
        write_str(" bytes\n");
        write_str("tx ");
        write_dec(info.tx_packets);
        write_str(" packets, ");
        write_dec(info.tx_bytes);
        write_str(" bytes\n");
        return 0;
    }

    print_help();
    return 1;
}
