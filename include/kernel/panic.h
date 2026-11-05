#ifndef NUMOS_KERNEL_PANIC_H
#define NUMOS_KERNEL_PANIC_H

#include "libs/types.h"
#include "cpu/isr.h"

NORETURN void kpanic(const char* fmt, ...);
NORETURN void kpanic_at(const char* file, int line, const char* fmt, ...);
void stack_trace(void);
void dump_registers(void* regs);

#define kassert(expr) do { if (!(expr)) kpanic_at(__FILE__, __LINE__, "Assertion failed: %s", #expr); } while(0)

#endif
