#ifndef SCHEDULER_H
#define SCHEDULER_H

#include "lib/base.h"

/* =========================================================================
 * NumOS Process Scheduler
 *
 * Simple round-robin preemptive scheduler.
 *
 * Process lifecycle:
 *   UNUSED → READY → RUNNING → (BLOCKED | READY) → ZOMBIE → UNUSED
 *
 * Scheduling is triggered by the timer IRQ every tick via
 * scheduler_tick().  Direct yields are also possible via schedule().
 *
 * Context switching is done in context_switch.asm:
 *   void context_switch(struct cpu_context **old_ctx,
 *                       struct cpu_context  *new_ctx);
 * ========================================================================= */

/* ---- Process limits ------------------------------------------------------ */
#define MAX_PROCESSES       16      /* Maximum concurrent processes           */
#define KERNEL_STACK_SIZE   16384   /* 16 KB kernel stack per process         */
#define USER_STACK_SIZE     65536   /* 64 KB user stack                       */
#define PROCESS_NAME_LEN    32      /* Max process name length                */

/* ---- Scheduling parameters ---------------------------------------------- */
#define SCHED_TICKS_PER_SLICE   10  /* Timer ticks per time-slice (10ms each
                                       = 100 ms at 100 Hz)                   */

/* ---- Process states ------------------------------------------------------- */
typedef enum {
    PROC_UNUSED  = 0,   /* Slot is free                                       */
    PROC_READY   = 1,   /* Runnable, waiting for CPU                          */
    PROC_RUNNING = 2,   /* Currently executing                                */
    PROC_BLOCKED = 3,   /* Waiting for an event (sleep, I/O)                  */
    PROC_ZOMBIE  = 4,   /* Exited but not yet reaped                          */
} proc_state_t;

/* ---- Saved register state (callee-saved + rsp + rip) --------------------- */
/* This is what context_switch() saves/restores on the kernel stack.
 *
 * Memory layout matches the push sequence in context_switch.asm:
 *   push r15, r14, r13, r12, rbx, rbp  (after call pushed rip)
 * Stack grows down, so [rsp+0]=rbp … [rsp+40]=r15, [rsp+48]=rip.
 */
struct cpu_context {
    uint64_t rbp;   /* [rsp+0]  – top of push sequence (lowest address)   */
    uint64_t rbx;   /* [rsp+8]                                             */
    uint64_t r12;   /* [rsp+16]                                            */
    uint64_t r13;   /* [rsp+24]                                            */
    uint64_t r14;   /* [rsp+32]                                            */
    uint64_t r15;   /* [rsp+40]                                            */
    uint64_t rip;   /* [rsp+48] – return address pushed by call instruction */
};

/* ---- Process Control Block (PCB) ----------------------------------------- */
struct process {
    /* Identity */
    int      pid;                           /* Process ID (1-based)           */
    char     name[PROCESS_NAME_LEN];        /* Human-readable name            */
    proc_state_t state;                     /* Current state                  */
    int      exit_code;                     /* Exit status (set on ZOMBIE)    */

    /* Scheduling */
    int      ticks_remaining;              /* Ticks left in current slice     */
    uint64_t total_ticks;                  /* Lifetime tick count             */
    uint64_t created_at_ms;               /* Uptime at creation               */

    /* Kernel stack – used during syscalls and context switches */
    uint8_t *kernel_stack;                 /* kmalloc'd kernel stack base     */
    uint8_t *kernel_stack_top;             /* kernel_stack + KERNEL_STACK_SIZE */
    struct cpu_context *context;           /* Saved context (on kernel stack) */

    /* User address space */
    uint64_t user_entry;                   /* ELF entry point (virtual)       */
    uint64_t user_stack_top;              /* Top of user stack (virtual)      */
    uint64_t load_base;                   /* Lowest mapped virtual address    */
    uint64_t load_end;                    /* Highest mapped virtual address   */

    /* Sleep support */
    uint64_t wake_at_ms;                  /* Uptime (ms) to unblock at        */

    /* Linked list for run-queue */
    struct process *next;
};

/* ---- Scheduler statistics ------------------------------------------------- */
struct sched_stats {
    uint64_t context_switches;
    uint64_t total_ticks;
    uint64_t processes_created;
    uint64_t processes_exited;
    uint32_t active_processes;
};

/* =========================================================================
 * Public API
 * ======================================================================= */

/* Initialise the scheduler; must be called once during kernel_init()       */
void scheduler_init(void);

/* Create a new kernel-mode process running func()
 * Returns the new process, or NULL on failure.                             */
struct process *process_create_kernel(const char *name,
                                      void (*func)(void));

/* Create a user-mode process from a loaded ELF image.
 * entry    – virtual address of _start
 * stack    – virtual address of top of user stack (already mapped)
 * Returns the new process, or NULL on failure.                             */
struct process *process_create_user(const char *name,
                                    uint64_t entry,
                                    uint64_t stack_top);

/* Called by the ELF loader after successfully loading an image.
 * Convenience wrapper: calls process_create_user() then makes it READY.   */
struct process *process_spawn(const char *name,
                               uint64_t entry,
                               uint64_t stack_top);

/* Mark the current process as ZOMBIE and yield the CPU.
 * Never returns.                                                           */
void process_exit(int exit_code);

/* Block the current process until uptime_ms >= wake_ms                    */
void process_sleep_until(uint64_t wake_ms);

/* Called from the timer IRQ handler every tick.
 * Decrements the current slice; calls schedule() when it expires.         */
void scheduler_tick(void);

/* Voluntarily yield the CPU; picks the next READY process.                */
void schedule(void);

/* Return the currently running process (NULL before scheduler_init)       */
struct process *scheduler_current(void);

/* Return the idle (kernel) process                                         */
struct process *scheduler_get_idle(void);

/* Diagnostics                                                              */
void scheduler_print_stats(void);
void scheduler_print_processes(void);
struct sched_stats scheduler_get_stats(void);

/* ---- Assembly context switch (defined in context_switch.asm) ------------ */
/* Saves callee-saved registers + rip of *old onto old's kernel stack,
 * then restores the same from new's kernel stack and returns into new.     */
extern void context_switch(struct cpu_context **old_ctx,
                            struct cpu_context  *new_ctx);

/* ---- Helper used by process_exit() in syscall.c ------------------------- */
void process_mark_zombie(struct process *proc, int exit_code);

#endif /* SCHEDULER_H */