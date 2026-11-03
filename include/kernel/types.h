#ifndef NUMOS_KERNEL_TYPES_H
#define NUMOS_KERNEL_TYPES_H

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef unsigned long long  uint64_t;
typedef signed char         int8_t;
typedef signed short        int16_t;
typedef signed int          int32_t;
typedef signed long long    int64_t;

typedef uint64_t            uintptr_t;
typedef int64_t             intptr_t;
typedef uint64_t            size_t;
typedef int64_t             ssize_t;
typedef int64_t             ptrdiff_t;

typedef _Bool               bool;
#define true  1
#define false 0

#define NULL ((void*)0)

#define ALIGN_UP(x, align)   (((x) + ((align) - 1)) & ~((align) - 1))
#define ALIGN_DOWN(x, align) ((x) & ~((align) - 1))
#define PAGE_SIZE            4096ULL
#define PACKED               __attribute__((packed))
#define UNUSED               __attribute__((unused))
#define NORETURN             __attribute__((noreturn))
#define ALIGNED(n)           __attribute__((aligned(n)))

#define va_list   __builtin_va_list
#define va_start  __builtin_va_start
#define va_end    __builtin_va_end
#define va_arg    __builtin_va_arg

#endif
