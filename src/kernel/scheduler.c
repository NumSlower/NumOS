/*
 * scheduler.c - NumOS Round-Robin Process Scheduler
 *
 * Design overview
 * ---------------
 * Processes are stored in a fixed-size table (process_table[]).
 * The run-queue is a singly-linked circular list of READY/RUNNING
 * processes maintained in run_queue_head.
 *
 * Preemption is driven by scheduler_tick() which the timer IRQ calls
 * every tick (~10 ms at 100 Hz).  When a slice expires, schedule() is
 * called to pick the next READY process and perform a context_switch().
 *
 * A single "idle" process (pid 0) runs when no user processes are ready.
 * It simply executes HLT in a loop so the CPU can sleep between ticks.
 *
 * Each process has its own 16 KB kernel stack.  On first creation a
 * cpu_context frame is hand-crafted at the top of that stack with rip
 * pointing at process_trampoline() so that context_switch() will land
 * there on the first scheduling of the process.
 *
 * For user processes, process_trampoline() issues an IRETQ into Ring 3
 * using the entry point and user stack stored in the PCB.
 */

#include "kernel/scheduler.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "cpu/heap.h"
#include "cpu/paging.h"
#include "cpu/gdt.h"

/* -------------------------------------------------------------------------
 * Module state
 * ---------------------------------------------------------------------- */
static struct process  process_table[MAX_PROCESSES];
static struct process *run_queue_head = NULL;   /* head of circular READY list */
static struct process *current_proc   = NULL;   /* currently running process   */
static struct process *idle_proc      = NULL;   /* always-ready idle process   */
static struct sched_stats stats;
static int scheduler_active = 0;               /* set after scheduler_init()  */

/* -------------------------------------------------------------------------
 * Forward declarations of internal helpers
 * ---------------------------------------------------------------------- */
static struct process *alloc_process(void);
static void            free_process(struct process *proc);
static void            enqueue(struct process *proc);
static void            dequeue(struct process *proc);
static struct process *pick_next(void);
static void            idle_loop(void);
static void            process_trampoline(void);

/* =========================================================================
 * process_trampoline
 *
 * Every new process's initial rip points here.
 *
 * For KERNEL processes  – the PCB's user_entry holds the C function pointer;
 *                          we simply call it and then call process_exit(0).
 *
 * For USER processes    – we do a manual IRETQ into Ring 3 using the saved
 *                          entry point and user stack stored in the PCB.
 * ======================================================================= */
static void process_trampoline(void) {
    struct process *proc = current_proc;

    if (proc->user_entry == 0) {
        /*
         * Kernel process: user_entry is repurposed as a function pointer.
         * Cast and call it.
         */
        void (*fn)(void) = (void(*)(void))(uintptr_t)proc->load_base;
        fn();
        process_exit(0);
        /* Never reached */
        while(1) __asm__ volatile("hlt");
    }

    /*
     * User process: transition to Ring 3 via IRETQ.
     *
     * IRETQ pops (from current rsp upward): RIP, CS, RFLAGS, RSP, SS.
     *
     * Selectors with RPL=3:
     *   CS = GDT_USER_CODE | 3 = 0x20 | 3 = 0x23
     *   SS = GDT_USER_DATA | 3 = 0x18 | 3 = 0x1B
     *
     * CRITICAL: we must load all five values into physical registers BEFORE
     * we overwrite rsp, because the C local variables live on the old stack.
     * We use explicit register variables to force gcc to materialise each
     * value in a register prior to the asm block.
     */
    register uint64_t r_kstack  asm("rax") = (uint64_t)proc->kernel_stack_top;
    register uint64_t r_ss      asm("rbx") = (uint64_t)(GDT_USER_DATA | 3);
    register uint64_t r_ursp    asm("rcx") = proc->user_stack_top;
    register uint64_t r_rflags  asm("rdx") = 0x202UL;   /* IF=1, bit1 always 1 */
    register uint64_t r_cs      asm("rsi") = (uint64_t)(GDT_USER_CODE | 3);
    register uint64_t r_rip     asm("rdi") = proc->user_entry;

    /* Update the per-process syscall kernel-stack pointer */
    extern uint8_t *syscall_kernel_stack_top;
    syscall_kernel_stack_top = proc->kernel_stack_top;

    __asm__ volatile(
        /* Switch to this process's kernel stack (fresh, no saved frame) */
        "mov %0, %%rsp\n\t"
        /* Build the IRETQ frame on the new kernel stack */
        "push %1\n\t"       /* SS   */
        "push %2\n\t"       /* RSP  */
        "push %3\n\t"       /* RFLAGS */
        "push %4\n\t"       /* CS   */
        "push %5\n\t"       /* RIP  */
        "iretq\n\t"
        :
        : "r"(r_kstack),
          "r"(r_ss),
          "r"(r_ursp),
          "r"(r_rflags),
          "r"(r_cs),
          "r"(r_rip)
        : "memory"
    );

    /* Unreachable */
    while(1) __asm__ volatile("hlt");
}

/* =========================================================================
 * idle_loop – runs when no user process is READY
 * ======================================================================= */
static void idle_loop(void) {
    while (1) {
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

/* =========================================================================
 * Internal run-queue helpers
 * ======================================================================= */

/* Find a free slot in process_table */
static struct process *alloc_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            memset(&process_table[i], 0, sizeof(struct process));
            return &process_table[i];
        }
    }
    return NULL;
}

static void free_process(struct process *proc) {
    if (proc->kernel_stack) {
        kfree(proc->kernel_stack);
        proc->kernel_stack     = NULL;
        proc->kernel_stack_top = NULL;
    }
    proc->state = PROC_UNUSED;
}

/* Append proc to the tail of the circular run-queue */
static void enqueue(struct process *proc) {
    if (!run_queue_head) {
        proc->next      = proc;   /* circular: points to itself */
        run_queue_head  = proc;
        return;
    }
    /* Walk to the tail (the node whose ->next is the head) */
    struct process *tail = run_queue_head;
    while (tail->next != run_queue_head) {
        tail = tail->next;
    }
    tail->next = proc;
    proc->next = run_queue_head;
}

/* Remove proc from the circular run-queue */
static void dequeue(struct process *proc) {
    if (!run_queue_head) return;

    if (run_queue_head == proc && proc->next == proc) {
        /* Only element */
        run_queue_head = NULL;
        proc->next     = NULL;
        return;
    }

    /* Find the node before proc */
    struct process *prev = run_queue_head;
    while (prev->next != proc) {
        prev = prev->next;
        if (prev == run_queue_head) {
            /* proc not in queue */
            return;
        }
    }
    prev->next = proc->next;
    if (run_queue_head == proc) {
        run_queue_head = proc->next;
    }
    proc->next = NULL;
}

/*
 * Pick the next READY process after current_proc in the run-queue.
 * Unblocks sleeping processes whose wake_at_ms has passed.
 * Falls back to idle_proc if nothing is runnable.
 */
static struct process *pick_next(void) {
    if (!run_queue_head) return idle_proc;

    uint64_t now = timer_get_uptime_ms();

    /* First pass: unblock any sleeping processes that are due */
    struct process *p = run_queue_head;
    do {
        if (p->state == PROC_BLOCKED && p->wake_at_ms != 0 &&
            now >= p->wake_at_ms) {
            p->state      = PROC_READY;
            p->wake_at_ms = 0;
        }
        p = p->next;
    } while (p != run_queue_head);

    /* Second pass: find next READY process after current_proc */
    struct process *start = current_proc ? current_proc->next : run_queue_head;
    if (!start) start = run_queue_head;

    p = start;
    do {
        if (p->state == PROC_READY) return p;
        p = p->next;
    } while (p != start);

    return idle_proc;
}

/* =========================================================================
 * Allocate and prepare the kernel stack for a new process.
 *
 * We place a hand-crafted cpu_context frame at the top of the kernel stack
 * so that the very first context_switch() into this process will pop the
 * registers and ret into process_trampoline().
 * ======================================================================= */
static int setup_kernel_stack(struct process *proc) {
    proc->kernel_stack = (uint8_t *)kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) return -1;

    memset(proc->kernel_stack, 0, KERNEL_STACK_SIZE);
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;

    /*
     * Build a fake cpu_context at the top of the kernel stack so that the
     * very first context_switch() into this process pops registers and
     * rets into process_trampoline().
     *
     * context_switch.asm pushes in this order AFTER the call instruction
     * pushes rip:
     *   push r15  → rsp -= 8
     *   push r14  → rsp -= 8
     *   push r13  → rsp -= 8
     *   push r12  → rsp -= 8
     *   push rbx  → rsp -= 8
     *   push rbp  → rsp -= 8   ← final rsp (saved as context pointer)
     *
     * So memory layout from [rsp] upward (i.e. frame[0] = lowest addr):
     *   frame[0] = rbp   (top of push sequence, lowest address)
     *   frame[1] = rbx
     *   frame[2] = r12
     *   frame[3] = r13
     *   frame[4] = r14
     *   frame[5] = r15
     *   frame[6] = rip   (pushed by the call instruction, highest addr)
     *
     * context_switch pops in the reverse order:
     *   pop rbp → frame[0]
     *   pop rbx → frame[1]
     *   pop r12 → frame[2]
     *   pop r13 → frame[3]
     *   pop r14 → frame[4]
     *   pop r15 → frame[5]
     *   ret     → frame[6] = process_trampoline
     */
    uint64_t *frame = (uint64_t *)(proc->kernel_stack_top -
                                   sizeof(struct cpu_context));
    frame[0] = 0;  /* rbp */
    frame[1] = 0;  /* rbx */
    frame[2] = 0;  /* r12 */
    frame[3] = 0;  /* r13 */
    frame[4] = 0;  /* r14 */
    frame[5] = 0;  /* r15 */
    frame[6] = (uint64_t)(uintptr_t)process_trampoline;   /* rip */

    proc->context = (struct cpu_context *)frame;
    return 0;
}

/* =========================================================================
 * Static PID counter
 * ======================================================================= */
static int next_pid = 1;

static int alloc_pid(void) {
    return next_pid++;
}

/* =========================================================================
 * scheduler_init
 * ======================================================================= */
void scheduler_init(void) {
    memset(process_table, 0, sizeof(process_table));
    memset(&stats, 0, sizeof(stats));
    run_queue_head  = NULL;
    current_proc    = NULL;
    scheduler_active = 0;

    /* Create the idle process (pid 0) */
    idle_proc = alloc_process();
    idle_proc->pid   = 0;
    strncpy(idle_proc->name, "idle", PROCESS_NAME_LEN);
    idle_proc->state = PROC_READY;
    idle_proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    idle_proc->load_base       = (uint64_t)(uintptr_t)idle_loop;

    if (setup_kernel_stack(idle_proc) != 0) {
        panic("scheduler_init: cannot allocate idle stack");
    }

    /* Idle uses a kernel function, not a user entry */
    idle_proc->user_entry = 0;

    enqueue(idle_proc);
    current_proc = idle_proc;
    current_proc->state = PROC_RUNNING;
    scheduler_active = 1;

    vga_writestring("Scheduler: Initialized (max ");
    print_dec(MAX_PROCESSES);
    vga_writestring(" processes, ");
    print_dec(SCHED_TICKS_PER_SLICE);
    vga_writestring(" ticks/slice)\n");
}

/* =========================================================================
 * process_create_kernel
 * ======================================================================= */
struct process *process_create_kernel(const char *name, void (*func)(void)) {
    struct process *proc = alloc_process();
    if (!proc) {
        vga_writestring("Scheduler: process table full\n");
        return NULL;
    }

    proc->pid   = alloc_pid();
    strncpy(proc->name, name, PROCESS_NAME_LEN);
    proc->state            = PROC_READY;
    proc->ticks_remaining  = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms    = timer_get_uptime_ms();

    /* For kernel processes we reuse load_base to store the function ptr */
    proc->load_base  = (uint64_t)(uintptr_t)func;
    proc->user_entry = 0;   /* 0 → kernel process in trampoline */

    if (setup_kernel_stack(proc) != 0) {
        free_process(proc);
        return NULL;
    }

    enqueue(proc);
    stats.processes_created++;
    stats.active_processes++;

    vga_writestring("Scheduler: Created kernel process '");
    vga_writestring(name);
    vga_writestring("' (pid ");
    print_dec((uint64_t)proc->pid);
    vga_writestring(")\n");

    return proc;
}

/* =========================================================================
 * process_create_user
 * ======================================================================= */
struct process *process_create_user(const char *name,
                                    uint64_t entry,
                                    uint64_t stack_top) {
    struct process *proc = alloc_process();
    if (!proc) {
        vga_writestring("Scheduler: process table full\n");
        return NULL;
    }

    proc->pid   = alloc_pid();
    strncpy(proc->name, name, PROCESS_NAME_LEN);
    proc->state            = PROC_READY;
    proc->ticks_remaining  = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms    = timer_get_uptime_ms();
    proc->user_entry       = entry;
    proc->user_stack_top   = stack_top;

    if (setup_kernel_stack(proc) != 0) {
        free_process(proc);
        return NULL;
    }

    enqueue(proc);
    stats.processes_created++;
    stats.active_processes++;

    vga_writestring("Scheduler: Created user process '");
    vga_writestring(name);
    vga_writestring("' (pid ");
    print_dec((uint64_t)proc->pid);
    vga_writestring(", entry=0x");
    print_hex(entry);
    vga_writestring(", stack=0x");
    print_hex(stack_top);
    vga_writestring(")\n");

    return proc;
}

/* =========================================================================
 * process_spawn – convenience wrapper used by the ELF loader path
 * ======================================================================= */
struct process *process_spawn(const char *name,
                               uint64_t entry,
                               uint64_t stack_top) {
    return process_create_user(name, entry, stack_top);
}

/* =========================================================================
 * process_mark_zombie – called by sys_exit() in syscall.c
 * ======================================================================= */
void process_mark_zombie(struct process *proc, int exit_code) {
    proc->exit_code = exit_code;
    proc->state     = PROC_ZOMBIE;
    dequeue(proc);
    stats.processes_exited++;
    if (stats.active_processes > 0) stats.active_processes--;
}

/* =========================================================================
 * process_exit – called from process code (or process_trampoline)
 * Never returns.
 * ======================================================================= */
void process_exit(int exit_code) {
    __asm__ volatile("cli");

    if (current_proc && current_proc != idle_proc) {
        vga_writestring("\nScheduler: Process '");
        vga_writestring(current_proc->name);
        vga_writestring("' (pid ");
        print_dec((uint64_t)current_proc->pid);
        vga_writestring(") exited with code ");
        print_dec((uint64_t)(uint32_t)exit_code);
        vga_writestring("\n");

        process_mark_zombie(current_proc, exit_code);
    }

    /* Force a reschedule – will never return to this process */
    __asm__ volatile("sti");
    schedule();

    /* Should never reach here */
    while (1) __asm__ volatile("hlt");
}

/* =========================================================================
 * process_sleep_until
 * ======================================================================= */
void process_sleep_until(uint64_t wake_ms) {
    __asm__ volatile("cli");
    if (current_proc && current_proc != idle_proc) {
        current_proc->state      = PROC_BLOCKED;
        current_proc->wake_at_ms = wake_ms;
        dequeue(current_proc);
        /* Re-enqueue as blocked (pick_next will re-check wake time) */
        enqueue(current_proc);
    }
    __asm__ volatile("sti");
    schedule();
}

/* =========================================================================
 * schedule – pick the next READY process and switch to it
 * ======================================================================= */
void schedule(void) {
    if (!scheduler_active) return;

    __asm__ volatile("cli");

    struct process *next = pick_next();

    if (next == current_proc) {
        /* Nothing to switch to */
        __asm__ volatile("sti");
        return;
    }

    struct process *old   = current_proc;
    current_proc          = next;

    if (old->state == PROC_RUNNING) {
        old->state = PROC_READY;
    }
    next->state            = PROC_RUNNING;
    next->ticks_remaining  = SCHED_TICKS_PER_SLICE;

    stats.context_switches++;
    stats.total_ticks++;

    __asm__ volatile("sti");

    /* Perform the actual CPU context switch */
    context_switch(&old->context, next->context);

    /*
     * We return here when 'old' is scheduled again.
     * At that point current_proc == old (restored by the next
     * context_switch call that picks us).
     */
}

/* =========================================================================
 * scheduler_tick – called from the timer IRQ every tick
 * ======================================================================= */
void scheduler_tick(void) {
    if (!scheduler_active || !current_proc) return;

    current_proc->total_ticks++;
    stats.total_ticks++;

    /* Unblock any sleeping processes */
    uint64_t now = timer_get_uptime_ms();
    if (run_queue_head) {
        struct process *p = run_queue_head;
        do {
            if (p->state == PROC_BLOCKED && p->wake_at_ms != 0 &&
                now >= p->wake_at_ms) {
                p->state      = PROC_READY;
                p->wake_at_ms = 0;
            }
            p = p->next;
        } while (p != run_queue_head);
    }

    /* Decrement the current slice */
    if (current_proc->ticks_remaining > 0) {
        current_proc->ticks_remaining--;
    }

    if (current_proc->ticks_remaining == 0) {
        schedule();
    }
}

/* =========================================================================
 * Public accessors
 * ======================================================================= */
struct process *scheduler_current(void) {
    return current_proc;
}

struct process *scheduler_get_idle(void) {
    return idle_proc;
}

struct sched_stats scheduler_get_stats(void) {
    return stats;
}

/* =========================================================================
 * Diagnostics
 * ======================================================================= */
void scheduler_print_stats(void) {
    vga_writestring("\nScheduler Statistics:\n");
    vga_writestring("  Context switches:  ");
    print_dec(stats.context_switches);
    vga_writestring("\n  Total ticks:       ");
    print_dec(stats.total_ticks);
    vga_writestring("\n  Processes created: ");
    print_dec(stats.processes_created);
    vga_writestring("\n  Processes exited:  ");
    print_dec(stats.processes_exited);
    vga_writestring("\n  Active processes:  ");
    print_dec(stats.active_processes);
    vga_writestring("\n");
}

void scheduler_print_processes(void) {
    vga_writestring("\nProcess Table:\n");
    vga_writestring("  PID  STATE     TICKS  NAME\n");
    vga_writestring("  ---  --------  -----  ----\n");

    const char *state_names[] = {
        "UNUSED  ", "READY   ", "RUNNING ", "BLOCKED ", "ZOMBIE  "
    };

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &process_table[i];
        if (p->state == PROC_UNUSED) continue;

        vga_writestring("  ");
        print_dec((uint64_t)p->pid);
        vga_writestring("    ");

        uint8_t st = (uint8_t)p->state;
        if (st < 5) vga_writestring(state_names[st]);
        else        vga_writestring("?       ");

        vga_writestring("  ");
        print_dec(p->total_ticks);
        vga_writestring("     ");
        vga_writestring(p->name);
        vga_writestring("\n");
    }
}