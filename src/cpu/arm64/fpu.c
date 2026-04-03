#include "cpu/fpu.h"

static int fpu_ready = 0;

bool fpu_init(void) {
    uint64_t value = 0;

    __asm__ volatile("mrs %0, cpacr_el1" : "=r"(value));
    value |= (3UL << 20);
    __asm__ volatile(
        "msr cpacr_el1, %0\n\t"
        "isb"
        :
        : "r"(value)
        : "memory");

    fpu_ready = 1;
    return true;
}

bool fpu_is_available(void) {
    return fpu_ready != 0;
}

void fpu_init_state(void *state) {
    if (!state) return;
    unsigned char *bytes = (unsigned char *)state;
    for (size_t i = 0; i < FPU_STATE_SIZE; i++) bytes[i] = 0;
}

void fpu_save(void *state) {
    (void)state;
}

void fpu_restore(const void *state) {
    (void)state;
}
