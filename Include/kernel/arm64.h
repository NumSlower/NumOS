#ifndef NUMOS_KERNEL_ARM64_H
#define NUMOS_KERNEL_ARM64_H

#include "lib/base.h"

#define NUMOS_ARM64_QEMU_VIRT_UART0_BASE 0x09000000UL

struct arm64_exception_frame {
    uint64_t x[31];
    uint64_t sp_el0;
    uint64_t elr_el1;
    uint64_t spsr_el1;
    uint64_t esr_el1;
    uint64_t far_el1;
};

uint64_t arm64_exception_level(void);
uint64_t arm64_core_id(void);
uint64_t arm64_counter_frequency(void);
uint64_t arm64_counter_value(void);
void arm64_set_vector_base(uint64_t addr);
void arm64_handle_exception(struct arm64_exception_frame *frame);
int arm64_run_init_program(const char *path, const char *cmdline);

#endif /* NUMOS_KERNEL_ARM64_H */
