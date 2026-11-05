#ifndef NUMOS_KERNEL_KPRINTF_H
#define NUMOS_KERNEL_KPRINTF_H

#include "libs/types.h"

typedef void (*kprintf_putchar_t)(char c);
void kprintf_register_putchar(kprintf_putchar_t fn);

int kvsnprintf(char* buf, size_t size, const char* fmt, va_list args);
int ksprintf(char* buf, const char* fmt, ...);
int kprintf(const char* fmt, ...);

#endif
