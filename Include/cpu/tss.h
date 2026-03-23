#ifndef TSS_H
#define TSS_H

#include "lib/base.h"

void tss_init(void);
void tss_set_kernel_stack(uint64_t rsp0);
void tss_get_descriptor(uint64_t *base, uint32_t *limit);

#endif /* TSS_H */
