#ifndef NUMOS_KERNEL_ARM64_H
#define NUMOS_KERNEL_ARM64_H

#include "lib/base.h"

#define NUMOS_ARM64_QEMU_VIRT_UART0_BASE 0x09000000UL

uint64_t arm64_exception_level(void);
uint64_t arm64_core_id(void);
uint64_t arm64_counter_frequency(void);
uint64_t arm64_counter_value(void);
void arm64_set_vector_base(uint64_t addr);

#endif /* NUMOS_KERNEL_ARM64_H */
