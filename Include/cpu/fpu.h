#ifndef CPU_FPU_H
#define CPU_FPU_H

#include "lib/base.h"

#define FPU_STATE_SIZE 512
#define FPU_MXCSR_DEFAULT 0x1F80u

bool fpu_init(void);
bool fpu_is_available(void);
void fpu_init_state(void *state);
void fpu_save(void *state);
void fpu_restore(const void *state);

#endif
