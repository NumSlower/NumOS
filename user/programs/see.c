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

static int parse_u32(const char *s, uint32_t *out) {
    uint32_t value = 0;

    if (!s || !*s) return 0;

    while (*s) {
        if (*s < '0' || *s > '9') return 0;
        value = (value * 10u) + (uint32_t)(*s - '0');
        s++;
    }

    *out = value;
    return 1;
}

static void print_help(void) {
    write_str("usage: see A.B.C.D\n");
    write_str("       see -c COUNT A.B.C.D\n");
}

int main(void) {
    char cmdline[128];
    char host[64];
    uint8_t ip[4];
    struct numos_net_info info;
    uint32_t count = 4;
    uint32_t sent = 0;
    uint32_t received = 0;
    uint32_t total_ms = 0;
    uint32_t min_ms = 0;
    uint32_t max_ms = 0;

    if (sys_get_cmdline(cmdline, sizeof(cmdline)) < 0 || cmdline[0] == '\0') {
        print_help();
        return 1;
    }

    if (strncmp(cmdline, "-c ", 3) == 0) {
        char count_text[16];
        char *space = cmdline + 3;
        size_t i = 0;

        while (*space == ' ') space++;
        while (*space && *space != ' ' && i + 1 < sizeof(count_text)) {
            count_text[i++] = *space++;
        }
        count_text[i] = '\0';
        while (*space == ' ') space++;

        if (!parse_u32(count_text, &count) || count == 0 || *space == '\0') {
            print_help();
            return 1;
        }

        strncpy(host, space, sizeof(host));
        host[sizeof(host) - 1] = '\0';
    } else {
        strncpy(host, cmdline, sizeof(host));
        host[sizeof(host) - 1] = '\0';
    }

    if (!parse_ipv4(host, ip)) {
        write_str("see: invalid IPv4 address\n");
        return 1;
    }

    if (sys_net_info(&info) != 0 || !info.present) {
        write_str("see: no supported NIC detected\n");
        return 1;
    }

    if (!info.dhcp_configured) {
        write_str("see: network is not configured\n");
        write_str("see: run connect --dhcp first\n");
        return 1;
    }

    write_str("PING ");
    write_ipv4(ip);
    write_str(" (");
    write_ipv4(ip);
    write_str(")\n");

    for (uint32_t i = 0; i < count; i++) {
        struct numos_net_ping_result result;
        int ok;

        sent++;
        ok = (sys_net_ping(ip, 2000, &result) == 0 && result.success);
        if (ok) {
            received++;
            total_ms += result.roundtrip_ms;
            if (received == 1 || result.roundtrip_ms < min_ms) min_ms = result.roundtrip_ms;
            if (received == 1 || result.roundtrip_ms > max_ms) max_ms = result.roundtrip_ms;

            write_str("64 bytes from ");
            write_ipv4(ip);
            write_str(": icmp_seq=");
            write_dec(result.seq);
            write_str(" ttl=");
            write_dec(result.ttl);
            write_str(" time=");
            write_dec(result.roundtrip_ms);
            write_str(" ms\n");
        } else {
            write_str("Request timeout for icmp_seq ");
            write_dec(i + 1);
            write_str("\n");
        }

        if (i + 1 < count) sys_sleep_ms(1000);
    }

    write_str("--- ");
    write_ipv4(ip);
    write_str(" ping statistics ---\n");
    write_dec(sent);
    write_str(" packets transmitted, ");
    write_dec(received);
    write_str(" packets received, ");
    if (sent == 0) {
        write_str("0");
    } else {
        write_dec(((uint64_t)(sent - received) * 100u) / sent);
    }
    write_str("% packet loss\n");

    if (received > 0) {
        write_str("round-trip min/avg/max = ");
        write_dec(min_ms);
        write_str("/");
        write_dec(total_ms / received);
        write_str("/");
        write_dec(max_ms);
        write_str(" ms\n");
        return 0;
    }

    return 1;
}
