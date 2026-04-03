#include "cpu/tss.h"

void tss_init(void) {
}

void tss_set_kernel_stack(uint64_t rsp0) {
    (void)rsp0;
}

void tss_get_descriptor(uint64_t *base, uint32_t *limit) {
    if (base) *base = 0;
    if (limit) *limit = 0;
}
