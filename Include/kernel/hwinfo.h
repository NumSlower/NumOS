#ifndef HWINFO_H
#define HWINFO_H

#include "lib/base.h"

#define NUMOS_HWINFO_VERSION 1

#define HWINFO_STR_VENDOR_LEN 16
#define HWINFO_STR_NAME_LEN   32

enum hwinfo_form_factor {
    HWINFO_FORM_UNKNOWN = 0,
    HWINFO_FORM_DESKTOP = 1,
    HWINFO_FORM_LAPTOP  = 2,
    HWINFO_FORM_SERVER  = 3,
    HWINFO_FORM_VM      = 4
};

enum hwinfo_power_source {
    HWINFO_POWER_UNKNOWN = 0,
    HWINFO_POWER_AC      = 1,
    HWINFO_POWER_BATTERY = 2
};

enum hwinfo_battery_state {
    HWINFO_BATTERY_NONE        = 0,
    HWINFO_BATTERY_PRESENT     = 1 << 0,
    HWINFO_BATTERY_CHARGING    = 1 << 1,
    HWINFO_BATTERY_DISCHARGING = 1 << 2
};

enum hwinfo_flags {
    HWINFO_HAS_FORM_FACTOR = 1 << 0,
    HWINFO_HAS_POWER       = 1 << 1,
    HWINFO_HAS_BATTERY     = 1 << 2,
    HWINFO_HAS_CPU         = 1 << 3,
    HWINFO_HAS_MEMORY      = 1 << 4,
    HWINFO_HAS_HYPERVISOR  = 1 << 5,
    HWINFO_HAS_HEAP        = 1 << 6,
    HWINFO_HAS_UPTIME      = 1 << 7,
    HWINFO_HAS_PROCESSES   = 1 << 8
};

struct hwinfo {
    uint32_t size;
    uint32_t version;
    uint32_t flags;
    uint32_t form_factor;
    uint32_t power_source;
    int32_t  battery_percent;
    uint32_t battery_state;
    uint32_t cpu_count;
    uint32_t cpu_mhz;
    uint64_t mem_total;
    uint64_t mem_free;
    uint64_t heap_total;
    uint64_t heap_used;
    uint64_t uptime_ms;
    uint32_t process_count;
    uint32_t process_max;
    char     cpu_vendor[HWINFO_STR_VENDOR_LEN];
    char     hypervisor[HWINFO_STR_NAME_LEN];
    char     machine[HWINFO_STR_NAME_LEN];
};

#endif /* HWINFO_H */
