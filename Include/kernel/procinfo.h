#ifndef PROCINFO_H
#define PROCINFO_H

#include "lib/base.h"

#define PROCINFO_NAME_LEN 32

struct proc_info {
    int      pid;
    int      state;
    uint32_t flags;
    uint64_t total_ticks;
    uint64_t created_at_ms;
    uint64_t load_base;
    uint64_t load_end;
    uint64_t memory_bytes;
    char     name[PROCINFO_NAME_LEN];
};

#endif /* PROCINFO_H */
