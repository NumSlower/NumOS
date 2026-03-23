#ifndef SYSINFO_H
#define SYSINFO_H

#include "lib/base.h"

#define NUMOS_SYSINFO_VERSION_LEN 32

struct sysinfo {
    uint64_t uptime_ms;
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
    uint64_t total_frames;
    uint64_t used_frames;
    uint64_t free_frames;
    uint64_t heap_total;
    uint64_t heap_used;
    uint64_t heap_free;
    uint64_t page_faults;
    uint64_t pages_mapped;
    uint64_t pages_unmapped;
    uint64_t tlb_flushes;
    uint64_t processes_active;
    uint64_t processes_max;
    char     version[NUMOS_SYSINFO_VERSION_LEN];
};

#endif /* SYSINFO_H */
