/*
 * syscalls.c - Userspace system call wrapper implementations
 * These functions make INT 0x80 calls to the kernel
 */

#include "kernel/syscall.h"

/* System call wrappers - pure userspace implementations */

int64_t syscall0(uint64_t num) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num)
        : "memory"
    );
    return result;
}

int64_t syscall1(uint64_t num, uint64_t arg1) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1)
        : "memory"
    );
    return result;
}

int64_t syscall2(uint64_t num, uint64_t arg1, uint64_t arg2) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2)
        : "memory"
    );
    return result;
}

int64_t syscall3(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    int64_t result;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3)
        : "memory"
    );
    return result;
}

int64_t syscall4(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, uint64_t arg4) {
    int64_t result;
    register uint64_t r10 __asm__("r10") = arg4;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10)
        : "memory"
    );
    return result;
}

int64_t syscall5(uint64_t num, uint64_t arg1, uint64_t arg2, uint64_t arg3, 
                uint64_t arg4, uint64_t arg5) {
    int64_t result;
    register uint64_t r10 __asm__("r10") = arg4;
    register uint64_t r8 __asm__("r8") = arg5;
    __asm__ volatile(
        "int $0x80"
        : "=a"(result)
        : "a"(num), "D"(arg1), "S"(arg2), "d"(arg3), "r"(r10), "r"(r8)
        : "memory"
    );
    return result;
}