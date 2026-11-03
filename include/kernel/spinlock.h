#ifndef NUMOS_KERNEL_SPINLOCK_H
#define NUMOS_KERNEL_SPINLOCK_H

#include "kernel/types.h"

typedef struct { 
    volatile uint32_t lock; 
    volatile uint64_t flags; 
} spinlock_t;

#define SPINLOCK_INIT { .lock = 0, .flags = 0 }

void spin_lock(spinlock_t* lock);
void spin_unlock(spinlock_t* lock);
bool spin_try_lock(spinlock_t* lock);

#endif
