/* NumOS — spinlock.c — Basic spinlock implementation */
#include "kernel/spinlock.h"
#include "kernel/io.h"

void spin_lock(spinlock_t* lock) {
    uint64_t flags;
    // Save RFLAGS and disable interrupts
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags) : : "memory"
    );
    
    // Attempt to acquire lock using xchg
    uint32_t current = 1;
    while (current) {
        __asm__ volatile (
            "xchg %0, %1"
            : "=r"(current), "+m"(lock->lock)
            : "0"(current)
            : "memory"
        );
        if (current) {
            __asm__ volatile ("pause" ::: "memory"); // cpu relax
        }
    }
    lock->flags = flags;
}

void spin_unlock(spinlock_t* lock) {
    uint64_t flags = lock->flags;
    
    // Release the lock
    __asm__ volatile (
        "movl $0, %0"
        : "=m"(lock->lock) : : "memory"
    );
    
    // Restore RFLAGS
    __asm__ volatile (
        "push %0\n\t"
        "popfq"
        : : "r"(flags) : "memory", "cc"
    );
}

bool spin_try_lock(spinlock_t* lock) {
    uint64_t flags;
    // Save RFLAGS and disable interrupts
    __asm__ volatile (
        "pushfq\n\t"
        "pop %0\n\t"
        "cli"
        : "=r"(flags) : : "memory"
    );
    
    uint32_t current = 1;
    __asm__ volatile (
        "xchg %0, %1"
        : "=r"(current), "+m"(lock->lock)
        : "0"(current)
        : "memory"
    );
    
    if (current == 0) {
        lock->flags = flags;
        return true;
    } else {
        // Restore RFLAGS immediately if acquisition failed
        __asm__ volatile (
            "push %0\n\t"
            "popfq"
            : : "r"(flags) : "memory", "cc"
        );
        return false;
    }
}
