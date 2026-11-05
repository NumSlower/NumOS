/* NumOS — pit.h — Programmable Interval Timer (PIT) */
#ifndef NUMOS_PIT_H
#define NUMOS_PIT_H

#include "libs/types.h"
#include "cpu/isr.h"

void pit_init(uint32_t freq_hz);
uint64_t pit_get_ticks(void);
void pit_sleep_ms(uint64_t ms);

#endif
