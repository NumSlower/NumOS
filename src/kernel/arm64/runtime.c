#include "kernel/kernel.h"
#include "kernel/arm64.h"

typedef void (*init_func_t)(void);

extern init_func_t __init_array_start[];
extern init_func_t __init_array_end[];

uintptr_t __stack_chk_guard = 0;

static uintptr_t __attribute__((no_stack_protector))
runtime_seed_guard(void) {
    uintptr_t seed = arm64_counter_value();
    seed ^= (uintptr_t)&seed;
    seed ^= (uintptr_t)__init_array_start;
    seed ^= 0x9E3779B97F4A7C15ULL;

    if (seed == 0) seed = 0xA5A55A5AF0F00F0FULL;
    return seed;
}

void __attribute__((no_stack_protector)) runtime_init(void) {
    static int runtime_ready = 0;
    if (runtime_ready) return;

    __stack_chk_guard = runtime_seed_guard();

    for (init_func_t *fn = __init_array_start; fn < __init_array_end; fn++) {
        if (*fn) (*fn)();
    }

    runtime_ready = 1;
}

void __attribute__((no_stack_protector)) __stack_chk_fail(void) {
    panic("stack protector failure");
}

void __stack_chk_fail_local(void) __attribute__((alias("__stack_chk_fail")));
