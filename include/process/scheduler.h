/* NumOS — scheduler.h — Ready-queue round-robin scheduler
 *
 * Cooperative initially: threads call scheduler_yield() and the PIT feeds
 * quantum accounting via scheduler_tick(), raising a preemption request
 * that threads honour at their next yield. Timer-driven preemption inside
 * the IRQ path arrives with the privilege transition work.
 */
#ifndef NUMOS_PROCESS_SCHEDULER_H
#define NUMOS_PROCESS_SCHEDULER_H

#include "libs/types.h"
#include "process/process.h"

bool scheduler_init(void);
void scheduler_enqueue(struct process* proc);
void scheduler_start(void);
void scheduler_yield(void);
bool scheduler_tick(void);
void scheduler_preempt_from_irq(bool from_ring3);
void scheduler_schedule(void);
bool scheduler_preemption_requested(void);
uint64_t scheduler_get_context_switches(void);
uint64_t scheduler_get_uptime_ticks(void);
uint64_t scheduler_get_preemptions(void);

#endif
