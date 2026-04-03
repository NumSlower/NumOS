#include "kernel/arm64.h"

uint64_t arm64_exception_level(void) {
    uint64_t value = 0;
    __asm__ volatile("mrs %0, CurrentEL" : "=r"(value));
    return (value >> 2) & 0x3u;
}

uint64_t arm64_core_id(void) {
    uint64_t value = 0;
    __asm__ volatile("mrs %0, mpidr_el1" : "=r"(value));
    return value & 0xffu;
}

uint64_t arm64_counter_frequency(void) {
    uint64_t value = 0;
    __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(value));
    return value;
}

uint64_t arm64_counter_value(void) {
    uint64_t value = 0;
    __asm__ volatile("mrs %0, cntpct_el0" : "=r"(value));
    return value;
}

void arm64_set_vector_base(uint64_t addr) {
    __asm__ volatile(
        "msr vbar_el1, %0\n\t"
        "isb"
        :
        : "r"(addr)
        : "memory");
}
