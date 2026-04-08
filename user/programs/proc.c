#include "syscalls.h"
#include "program_version.h"

#define MAX_PROCS 32
#define MAX_PID   128
#define REFRESH_MS 200
#define TICKS_PER_SECOND 100
#define PROC_FLAG_IDLE 0x02
#define BAR_WIDTH 20

static size_t str_len(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

static void write_str(const char *s) {
    sys_write(FD_STDOUT, s, str_len(s));
}

static void write_ch(char c) {
    sys_write(FD_STDOUT, &c, 1);
}

static size_t u64_to_str(uint64_t v, char *buf, size_t cap) {
    if (cap == 0) return 0;
    if (v == 0) {
        if (cap > 1) { buf[0] = '0'; buf[1] = '\0'; return 1; }
        buf[0] = '\0';
        return 0;
    }
    char tmp[32];
    size_t i = 0;
    while (v > 0 && i < sizeof(tmp)) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    size_t len = (i < cap - 1) ? i : cap - 1;
    for (size_t j = 0; j < len; j++) {
        buf[j] = tmp[i - 1 - j];
    }
    buf[len] = '\0';
    return len;
}

static void write_pad(int count) {
    for (int i = 0; i < count; i++) write_ch(' ');
}

static void write_u64_padded(uint64_t v, int width) {
    char buf[32];
    size_t len = u64_to_str(v, buf, sizeof(buf));
    write_str(buf);
    int pad = width - (int)len;
    if (pad > 0) write_pad(pad);
}

static uint64_t bytes_to_mib(uint64_t bytes) {
    return bytes / (1024ULL * 1024ULL);
}

static uint64_t bytes_to_kib(uint64_t bytes) {
    return bytes / 1024ULL;
}

static uint64_t bytes_to_gib(uint64_t bytes) {
    return bytes / (1024ULL * 1024ULL * 1024ULL);
}

static uint64_t percent_u64(uint64_t part, uint64_t total) {
    if (total == 0) return 0;
    return (part * 100ULL) / total;
}

static const char *state_name(int state) {
    switch (state) {
        case 1: return "READY";
        case 2: return "RUN";
        case 3: return "BLOCK";
        case 4: return "ZOMB";
        default: return "UNK";
    }
}

static const char *form_name(uint32_t v) {
    switch (v) {
        case 1: return "DESKTOP";
        case 2: return "LAPTOP";
        case 3: return "SERVER";
        case 4: return "VM";
        default: return "UNKNOWN";
    }
}

static const char *power_name(uint32_t v) {
    switch (v) {
        case 1: return "AC";
        case 2: return "BATTERY";
        default: return "UNKNOWN";
    }
}

static const char *proc_label(const struct proc_info *proc) {
    if (!proc) return "unknown";
    if (proc->flags & PROC_FLAG_IDLE) return "idle";
    if (proc->name[0]) return proc->name;
    return "unnamed";
}

static void write_mem_value(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL) {
        write_u64_padded(bytes_to_mib(bytes), 0);
        write_str("M");
        return;
    }

    write_u64_padded(bytes_to_kib(bytes), 0);
    write_str("K");
}

static void write_size_value(uint64_t bytes) {
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        write_u64_padded(bytes_to_gib(bytes), 0);
        write_str(" GiB");
        return;
    }
    if (bytes >= 1024ULL * 1024ULL) {
        write_u64_padded(bytes_to_mib(bytes), 0);
        write_str(" MiB");
        return;
    }
    if (bytes >= 1024ULL) {
        write_u64_padded(bytes_to_kib(bytes), 0);
        write_str(" KiB");
        return;
    }
    write_u64_padded(bytes, 0);
    write_str(" B");
}

static void write_rate_value(uint64_t bytes_per_sec) {
    write_size_value(bytes_per_sec);
    write_str("/s");
}

static void write_ipv4(const uint8_t ip[4]) {
    write_u64_padded(ip[0], 0);
    write_ch('.');
    write_u64_padded(ip[1], 0);
    write_ch('.');
    write_u64_padded(ip[2], 0);
    write_ch('.');
    write_u64_padded(ip[3], 0);
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

static int ipv4_is_zero(const uint8_t ip[4]) {
    return ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0;
}

static void write_bar(uint64_t used, uint64_t total, int width) {
    uint64_t filled = 0;
    if (total > 0) filled = (used * (uint64_t)width) / total;
    if (filled > (uint64_t)width) filled = (uint64_t)width;

    write_ch('[');
    for (int i = 0; i < width; i++) {
        write_ch((uint64_t)i < filled ? '#' : '.');
    }
    write_ch(']');
}

static void write_meter_line(const char *label, uint64_t used, uint64_t total) {
    write_str("  ");
    write_str(label);
    write_pad(7 - (int)str_len(label));
    write_bar(used, total, BAR_WIDTH);
    write_str("  ");
    write_u64_padded(bytes_to_mib(used), 0);
    write_str(" / ");
    write_u64_padded(bytes_to_mib(total), 0);
    write_str(" MiB  ");
    write_u64_padded(percent_u64(used, total), 0);
    write_str("%\n");
}

static void write_key(const char *label) {
    write_str("  ");
    write_str(label);
    write_pad(9 - (int)str_len(label));
}

static void write_uptime(uint64_t ms) {
    uint64_t total = ms / 1000ULL;
    uint64_t sec = total % 60ULL;
    uint64_t min = (total / 60ULL) % 60ULL;
    uint64_t hour = total / 3600ULL;

    write_u64_padded(hour, 0);
    write_ch('h');
    write_u64_padded(min, 0);
    write_ch('m');
    write_u64_padded(sec, 0);
    write_ch('s');
}

static void write_traffic_line(const char *label,
                               uint64_t packets,
                               uint64_t bytes,
                               uint64_t rate) {
    write_key(label);
    write_u64_padded(packets, 0);
    write_str(" pkts, ");
    write_size_value(bytes);
    write_str(", ");
    write_rate_value(rate);
    write_ch('\n');
}

int main(int argc, char **argv) {
    struct hwinfo info;
    struct proc_info procs[MAX_PROCS];
    struct numos_net_info net;
    static uint64_t last_ticks[MAX_PID];
    uint64_t last_uptime_ms = 0;
    uint64_t last_rx_bytes = 0;
    uint64_t last_tx_bytes = 0;
    int net_seen = 0;

    if (argc >= 2 && numos_is_version_flag(argv[1])) {
        numos_print_program_version("proc");
        return 0;
    }

    for (int i = 0; i < MAX_PID; i++) last_ticks[i] = 0;

    for (;;) {
        info.size = sizeof(info);
        info.version = NUMOS_HWINFO_VERSION;
        sys_hwinfo(&info, sizeof(info));
        int64_t net_rc = sys_net_info(&net);

        int64_t count = sys_proclist(procs, MAX_PROCS);
        if (count < 0) count = 0;

        sys_fb_clear();
        write_str("NumOS proc monitor\n");
        write_str("==================\n\n");

        write_str("system\n");
        write_key("uptime");
        write_uptime(info.uptime_ms);
        write_str("\n");

        write_key("cpu");
        write_u64_padded(info.cpu_count, 0);
        write_str(" cores");
        if (info.flags & HWINFO_HAS_CPU) {
            if (info.cpu_vendor[0]) {
                write_str(", ");
                write_str(info.cpu_vendor);
            }
        }
        write_str("\n");

        uint64_t mem_used = 0;
        if (info.mem_total >= info.mem_free) mem_used = info.mem_total - info.mem_free;
        write_meter_line("mem", mem_used, info.mem_total);
        write_meter_line("heap", info.heap_used, info.heap_total);

        write_key("proc");
        write_u64_padded(info.process_count, 0);
        write_str(" / ");
        write_u64_padded(info.process_max, 0);
        write_str("\n");

        if (info.flags & HWINFO_HAS_FORM_FACTOR) {
            write_key("form");
            write_str(form_name(info.form_factor));
            write_str("\n");
        }
        if (info.flags & HWINFO_HAS_POWER) {
            write_key("power");
            write_str(power_name(info.power_source));
            write_str("\n");
        }
        if (info.flags & HWINFO_HAS_BATTERY) {
            write_key("battery");
            write_u64_padded((uint64_t)info.battery_percent, 0);
            write_str("%\n");
        }
        if (info.flags & HWINFO_HAS_HYPERVISOR) {
            if (info.hypervisor[0]) {
                write_key("hyper");
                write_str(info.hypervisor);
                write_str("\n");
            }
        }
        if (info.machine[0]) {
            write_key("machine");
            write_str(info.machine);
            write_str("\n");
        }

        write_str("\n");

        write_str("network\n");
        if (net_rc != 0 || !net.present) {
            write_key("state");
            write_str("unavailable\n");
            net_seen = 0;
        } else {
            uint64_t elapsed_ms = 0;
            uint64_t rx_rate = 0;
            uint64_t tx_rate = 0;

            if (info.uptime_ms >= last_uptime_ms) elapsed_ms = info.uptime_ms - last_uptime_ms;
            if (net_seen && elapsed_ms > 0) {
                uint64_t rx_delta = 0;
                uint64_t tx_delta = 0;
                if (net.rx_bytes >= last_rx_bytes) rx_delta = net.rx_bytes - last_rx_bytes;
                if (net.tx_bytes >= last_tx_bytes) tx_delta = net.tx_bytes - last_tx_bytes;
                rx_rate = (rx_delta * 1000ULL) / elapsed_ms;
                tx_rate = (tx_delta * 1000ULL) / elapsed_ms;
            }
            last_rx_bytes = net.rx_bytes;
            last_tx_bytes = net.tx_bytes;
            net_seen = 1;

            write_key("state");
            write_str(net.link_up ? "up" : "down");
            write_str(", dhcp ");
            write_str(net.dhcp_configured ? "ready" : "idle");
            write_ch('\n');

            write_key("iface");
            if (net.interface_name[0]) write_str(net.interface_name);
            else write_str("unknown");
            write_str(", ");
            if (net.driver[0]) write_str(net.driver);
            else write_str("driver?");
            write_ch('\n');

            write_key("ipv4");
            if (ipv4_is_zero(net.ipv4)) write_str("not set");
            else write_ipv4(net.ipv4);
            write_ch('\n');

            write_key("route");
            write_str("gw ");
            if (ipv4_is_zero(net.gateway)) write_str("0.0.0.0");
            else write_ipv4(net.gateway);
            write_str(", mask ");
            if (ipv4_is_zero(net.netmask)) write_str("0.0.0.0");
            else write_ipv4(net.netmask);
            write_ch('\n');

            write_key("mac");
            write_mac(net.mac);
            write_ch('\n');

            write_traffic_line("rx", net.rx_packets, net.rx_bytes, rx_rate);
            write_traffic_line("tx", net.tx_packets, net.tx_bytes, tx_rate);
        }

        write_str("\n");

        write_str("cpu\n");
        uint64_t deltas[MAX_PROCS];
        uint64_t elapsed_ms = 0;
        uint64_t busy_ticks = 0;
        uint64_t idle_ticks = 0;
        if (info.uptime_ms >= last_uptime_ms) elapsed_ms = info.uptime_ms - last_uptime_ms;
        last_uptime_ms = info.uptime_ms;
        uint64_t window_ticks = (elapsed_ms * TICKS_PER_SECOND) / 1000ULL;
        if (window_ticks == 0) window_ticks = 1;

        for (int i = 0; i < count; i++) {
            int pid = procs[i].pid;
            uint64_t prev = 0;
            if (pid >= 0 && pid < MAX_PID) prev = last_ticks[pid];
            uint64_t delta = 0;
            if (procs[i].total_ticks >= prev) delta = procs[i].total_ticks - prev;
            if (pid >= 0 && pid < MAX_PID) last_ticks[pid] = procs[i].total_ticks;
            deltas[i] = delta;

            if (procs[i].flags & PROC_FLAG_IDLE) idle_ticks += delta;
            else busy_ticks += delta;
        }

        write_key("total");
        write_u64_padded((busy_ticks * 100ULL) / window_ticks, 0);
        write_str("% busy, ");
        write_u64_padded((idle_ticks * 100ULL) / window_ticks, 0);
        write_str("% idle\n");

        write_str("\n");
        write_str("tasks\n");
        write_str("pid   state  ticks     cpu  mem    name\n");
        write_str("----  -----  --------  ---  -----  ----------------\n");

        for (int i = 0; i < count; i++) {
            uint64_t cpu_pct = 0;
            if (window_ticks > 0) cpu_pct = (deltas[i] * 100ULL) / window_ticks;

            write_u64_padded((uint64_t)procs[i].pid, 4);
            write_pad(2);
            write_str(state_name(procs[i].state));
            write_pad(2);
            write_u64_padded(procs[i].total_ticks, 8);
            write_pad(2);
            write_u64_padded(cpu_pct, 3);
            write_ch('%');
            write_pad(2);
            write_mem_value(procs[i].memory_bytes);
            write_pad(5);
            write_str(proc_label(&procs[i]));
            write_str("\n");
        }

        write_str("\nq to quit\n");

        char c = 0;
        if (sys_input_peek(&c) > 0) {
            if (c == 'q' || c == 'Q') break;
        }

        sys_sleep_ms(REFRESH_MS);
    }

    return 0;
}
