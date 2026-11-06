/* NumOS — scheduler.c — Ready-queue round-robin scheduler implementation
 *
 * FIFO ready queue over the PCB queue_next links. scheduler_start() enters
 * the first thread; yield swaps contexts via proc_switch. Reaping runs on
 * every schedule pass: zombie stacks are freed and their PCBs returned to
 * UNUSED. The PIT drives scheduler_tick(); quantum expiry raises a
 * preemption request honoured at the thread's next yield (cooperative
 * round-robin; IRQ-driven preemption lands with the Ring 3 work).
 */
#include "process/scheduler.h"
#include "process/process.h"
#include "kernel/vmm.h"
#include "cpu/gdt.h"
#include "cpu/heap.h"
#include "libs/string.h"
#include "libs/kprintf.h"

static struct process* ready_head = NULL;
static struct process* ready_tail = NULL;
static struct cpu_context boot_context;
static uint64_t context_switches = 0;
static uint64_t uptime_ticks = 0;
static bool preemption_requested = false;
static bool scheduler_ready = false;
static uint64_t boot_cr3 = 0;
static uint64_t preemptions = 0;

bool scheduler_init(void) {
    ready_head = NULL;
    ready_tail = NULL;
    context_switches = 0;
    uptime_ticks = 0;
    preemption_requested = false;
    scheduler_ready = true;
    boot_cr3 = vmm_get_cr3();
    return true;
}

uint64_t scheduler_get_preemptions(void) {
    return preemptions;
}

static void ready_push(struct process* proc) {
    proc->queue_next = NULL;
    proc->state = PROC_STATE_READY;
    if (ready_tail) {
        ready_tail->queue_next = proc;
    } else {
        ready_head = proc;
    }
    ready_tail = proc;
}

void scheduler_enqueue(struct process* proc) {
    if (!proc || proc->state != PROC_STATE_READY) return;
    ready_push(proc);
}

static struct process* ready_pop(void) {
    struct process* proc = ready_head;
    if (!proc) return NULL;
    ready_head = proc->queue_next;
    if (!ready_head) ready_tail = NULL;
    proc->queue_next = NULL;
    return proc;
}

/* PIT hook: returns true when a Ring 3 quantum expired and the IRQ should
 * call scheduler_preempt_from_irq(). */
bool scheduler_tick(void) {
    uptime_ticks++;
    if (scheduler_ready) {
        struct process* current = process_get_current();
        if (current && current->state == PROC_STATE_RUNNING) {
            current->ticks_used++;
            if (current->ticks_used >= current->quantum) {
                preemption_requested = true;
                return true;
            }
        }
    }
    return false;
}

/* Called from the PIT IRQ after a full GP-register frame was pushed. Only
 * preempts when the interrupted context was actually executing in Ring 3
 * (its full user state is in the IRQ frame): preempting a thread inside a
 * kernel syscall or inside ring-0 scheduler code would suspend ring-0
 * state that the callee-saved-only switch cannot safely resume on a
 * different stack. Kernel threads stay cooperative. */
void scheduler_preempt_from_irq(bool from_ring3) {
    struct process* current = process_get_current();
    if (!scheduler_ready || !current || !current->is_user || !from_ring3) return;
    if (!preemption_requested) return;
    current->ticks_used = 0;
    preemption_requested = false;
    preemptions++;
    scheduler_schedule();
}

bool scheduler_preemption_requested(void) {
    return preemption_requested;
}

/* Arms the target address space and privilege-transition stack. */
static void scheduler_arm(struct process* proc) {
    if (proc->is_user) {
        if (proc->cr3 != vmm_get_cr3()) {
            vmm_set_cr3(proc->cr3);
        }
        tss_set_rsp0(proc->kernel_stack_top);
    } else if (process_get_current() && process_get_current()->is_user) {
        /* Leaving a user process for a kernel thread: keep the kernel
         * tables so ring-0-only code sees the full identity map. */
        vmm_set_cr3(boot_cr3);
        tss_set_rsp0(0);
    }
}

void scheduler_schedule(void) {
    if (!scheduler_ready) return;

    struct process* prev = process_get_current();

    /* Reap zombies, but never the one we are about to switch away from:
     * the exiting thread is still executing on its own stack here. Its
     * PCB is reaped on a later schedule pass (or after scheduler_start). */
    process_reap_zombies(prev);

    struct process* next = ready_pop();
    /* Skip entries that died between enqueue and dispatch (parked as
     * zombies): a later reap pass collects them. */
    while (next && next->state != PROC_STATE_READY) {
        next = ready_pop();
    }
    if (!next) {
        if (prev && prev->state == PROC_STATE_RUNNING) {
            /* Nothing else ready: keep running the current thread. */
            return;
        }
        /* Current exited and nothing is ready: switch back to the boot
         * context so kernel_main can finish (or idle). */
        preemption_requested = false;
        if (prev) {
            process_set_current(NULL);
            proc_switch(&prev->context, &boot_context);
        }
        return;
    }

    preemption_requested = false;
    next->state = PROC_STATE_RUNNING;
    next->ticks_used = 0;
    process_set_current(next);

    scheduler_arm(next);

    if (prev != next) {
        context_switches++;
        if (prev) {
            if (prev->state == PROC_STATE_RUNNING) {
                ready_push(prev);
                prev->ticks_used = 0;
            }
            proc_switch(&prev->context, &next->context);
        } else {
            /* Boot path entering the scheduler for the first time. */
            proc_switch(&boot_context, &next->context);
        }
    }
}

void scheduler_yield(void) {
    if (!process_get_current()) return;
    process_get_current()->yield_count++;
    scheduler_schedule();
}

void scheduler_start(void) {
    struct process* first = ready_pop();
    if (!first) {
        kprintf("scheduler: no threads to run\n");
        return;
    }
    first->state = PROC_STATE_RUNNING;
    first->ticks_used = 0;
    process_set_current(first);
    scheduler_arm(first);
    context_switches++;
    kprintf("scheduler: starting %s (pid %u)\n", first->name, first->pid);
    proc_switch(&boot_context, &first->context);
    /* Control returns here when every thread has exited. */
    process_reap_zombies(NULL);
    kprintf("scheduler: all threads exited, back in kernel_main\n");
}

uint64_t scheduler_get_context_switches(void) {
    return context_switches;
}

uint64_t scheduler_get_uptime_ticks(void) {
    return uptime_ticks;
}
