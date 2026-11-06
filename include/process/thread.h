/* NumOS — thread.h — Kernel thread creation API */
#ifndef NUMOS_PROCESS_THREAD_H
#define NUMOS_PROCESS_THREAD_H

#include "libs/types.h"

struct process* thread_create(const char* name, void (*entry)(void));

#endif
