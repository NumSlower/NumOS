#include "syscalls.h"

#define MAX_PROCS 32
#define MAX_PID   128
#define REFRESH_MS 200
#define TICKS_PER_SECOND 100
#define PROC_FLAG_IDLE 0x02

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

int main(void) {
    struct hwinfo info;
    struct proc_info procs[MAX_PROCS];
    static uint64_t last_ticks[MAX_PID];
    uint64_t last_uptime_ms = 0;

    for (int i = 0; i < MAX_PID; i++) last_ticks[i] = 0;

    for (;;) {
        info.size = sizeof(info);
        info.version = NUMOS_HWINFO_VERSION;
        sys_hwinfo(&info, sizeof(info));

        int64_t count = sys_proclist(procs, MAX_PROCS);
        if (count < 0) count = 0;

        sys_fb_clear();
        write_str("btop\n\n");

        write_str("uptime: ");
        write_uptime(info.uptime_ms);
        write_str("\n");

        write_str("cpu: ");
        write_u64_padded(info.cpu_count, 0);
        write_str(" cores");
        if (info.flags & HWINFO_HAS_CPU) {
            if (info.cpu_vendor[0]) {
                write_str("  ");
                write_str(info.cpu_vendor);
            }
        }
        write_str("\n");

        uint64_t mem_used = 0;
        if (info.mem_total >= info.mem_free) mem_used = info.mem_total - info.mem_free;
        write_str("mem: ");
        write_u64_padded(bytes_to_mib(mem_used), 0);
        write_str(" MiB used / ");
        write_u64_padded(bytes_to_mib(info.mem_total), 0);
        write_str(" MiB total  ");
        write_u64_padded(percent_u64(mem_used, info.mem_total), 0);
        write_str("%\n");

        write_str("heap: ");
        write_u64_padded(bytes_to_mib(info.heap_used), 0);
        write_str(" MiB used / ");
        write_u64_padded(bytes_to_mib(info.heap_total), 0);
        write_str(" MiB total  ");
        write_u64_padded(percent_u64(info.heap_used, info.heap_total), 0);
        write_str("%\n");

        write_str("proc: ");
        write_u64_padded(info.process_count, 0);
        write_str(" / ");
        write_u64_padded(info.process_max, 0);
        write_str("\n");

        if (info.flags & HWINFO_HAS_FORM_FACTOR) {
            write_str("form: ");
            write_str(form_name(info.form_factor));
            write_str("\n");
        }
        if (info.flags & HWINFO_HAS_POWER) {
            write_str("power: ");
            write_str(power_name(info.power_source));
            write_str("\n");
        }
        if (info.flags & HWINFO_HAS_BATTERY) {
            write_str("battery: ");
            write_u64_padded((uint64_t)info.battery_percent, 0);
            write_str("%\n");
        }
        if (info.flags & HWINFO_HAS_HYPERVISOR) {
            if (info.hypervisor[0]) {
                write_str("hypervisor: ");
                write_str(info.hypervisor);
                write_str("\n");
            }
        }
        if (info.machine[0]) {
            write_str("machine: ");
            write_str(info.machine);
            write_str("\n");
        }

        write_str("\n");

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

        write_str("cpu total: ");
        write_u64_padded((busy_ticks * 100ULL) / window_ticks, 0);
        write_str("% busy  ");
        write_u64_padded((idle_ticks * 100ULL) / window_ticks, 0);
        write_str("% idle\n");

        write_str("pid   state  ticks     cpu%  mem    name\n");
        write_str("----  -----  --------  ----  -----  ----\n");

        for (int i = 0; i < count; i++) {
            uint64_t cpu_pct = 0;
            if (window_ticks > 0) cpu_pct = (deltas[i] * 100ULL) / window_ticks;

            write_u64_padded((uint64_t)procs[i].pid, 4);
            write_pad(2);
            write_str(state_name(procs[i].state));
            write_pad(2);
            write_u64_padded(procs[i].total_ticks, 8);
            write_pad(2);
            write_u64_padded(cpu_pct, 4);
            write_pad(2);
            write_mem_value(procs[i].memory_bytes);
            write_pad(5);
            write_str(proc_label(&procs[i]));
            write_str("\n");
        }

        write_str("\npress q to quit\n");

        char c = 0;
        if (sys_input_peek(&c) > 0) {
            if (c == 'q' || c == 'Q') break;
        }

        sys_sleep_ms(REFRESH_MS);
    }

    return 0;
}
