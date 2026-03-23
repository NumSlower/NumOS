#include "cpu/fpu.h"
#include "kernel/kernel.h"
#include "drivers/graphices/vga.h"

static bool fpu_ready = false;
static uint8_t default_fpu_state[FPU_STATE_SIZE] __attribute__((aligned(16)));

static inline void cpuid(uint32_t leaf, uint32_t *a, uint32_t *b,
                         uint32_t *c, uint32_t *d) {
    __asm__ volatile("cpuid"
                     : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
                     : "a"(leaf));
}

static inline uint64_t read_cr0(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

static inline void write_cr0(uint64_t value) {
    __asm__ volatile("mov %0, %%cr0" :: "r"(value) : "memory");
}

static inline uint64_t read_cr4(void) {
    uint64_t value;
    __asm__ volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

static inline void write_cr4(uint64_t value) {
    __asm__ volatile("mov %0, %%cr4" :: "r"(value) : "memory");
}

bool fpu_init(void) {
    uint32_t a, b, c, d;
    cpuid(1, &a, &b, &c, &d);

    const bool has_fxsr = (d & (1u << 24)) != 0;
    const bool has_sse  = (d & (1u << 25)) != 0;
    const bool has_sse2 = (d & (1u << 26)) != 0;

    if (!has_fxsr || !has_sse || !has_sse2) {
        fpu_ready = false;
        vga_writestring("FPU: CPU missing FXSR or SSE support\n");
        return false;
    }

    uint64_t cr0 = read_cr0();
    cr0 &= ~((uint64_t)1 << 2);  /* EM */
    cr0 &= ~((uint64_t)1 << 3);  /* TS */
    cr0 |=  ((uint64_t)1 << 1);  /* MP */
    cr0 |=  ((uint64_t)1 << 5);  /* NE */
    write_cr0(cr0);

    uint64_t cr4 = read_cr4();
    cr4 |= ((uint64_t)1 << 9);   /* OSFXSR */
    cr4 |= ((uint64_t)1 << 10);  /* OSXMMEXCPT */
    write_cr4(cr4);

    uint32_t mxcsr = FPU_MXCSR_DEFAULT;
    __asm__ volatile("fninit");
    __asm__ volatile("ldmxcsr (%0)" :: "r"(&mxcsr) : "memory");
    __asm__ volatile("fxsave (%0)" :: "r"(default_fpu_state) : "memory");

    fpu_ready = true;
    vga_writestring("FPU: x87 and SSE state enabled\n");
    return true;
}

bool fpu_is_available(void) {
    return fpu_ready;
}

void fpu_init_state(void *state) {
    if (!state) return;
    memset(state, 0, FPU_STATE_SIZE);
    if (!fpu_ready) return;
    memcpy(state, default_fpu_state, FPU_STATE_SIZE);
}

void fpu_save(void *state) {
    if (!fpu_ready || !state) return;
    __asm__ volatile("fxsave (%0)" :: "r"(state) : "memory");
}

void fpu_restore(const void *state) {
    if (!fpu_ready || !state) return;
    __asm__ volatile("fxrstor (%0)" :: "r"(state) : "memory");
}
