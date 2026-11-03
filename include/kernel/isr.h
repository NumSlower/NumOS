/* NumOS — include/kernel/isr.h — Exception and interrupt structures */
#ifndef NUMOS_KERNEL_ISR_H
#define NUMOS_KERNEL_ISR_H

#include "kernel/types.h"

typedef struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
} registers_t;

void exceptions_init(void);
void isr_handler(registers_t* regs);

#endif
