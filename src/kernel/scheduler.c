/*
 * scheduler.c - NumOS Round-Robin Process Scheduler
 *
 * Design overview
 * ---------------
 * Processes live in a fixed-size table (process_table[]).
 * The run-queue is a singly-linked circular list of READY/RUNNING
 * processes rooted at run_queue_head.
 *
 * Preemption is driven by scheduler_tick() which the timer IRQ calls
 * every tick (~10 ms at 100 Hz). When a slice expires, schedule()
 * selects the next READY process and calls context_switch().
 *
 * A single idle process (pid 0) runs when no user process is READY.
 * It executes HLT in a loop so the CPU sleeps between ticks.
 *
 * Each process owns a 16 KB kernel stack.  On first creation a
 * cpu_context frame is hand-crafted at the stack top with rip =
 * process_trampoline(), so context_switch() lands there on first dispatch.
 *
 * For user processes, process_trampoline() issues SYSRETQ into Ring 3
 * using the entry point and user stack pointer stored in the PCB.
 */

#include "kernel/scheduler.h"
#include "kernel/kernel.h"
#include "kernel/elf_loader.h"
#include "drivers/vga.h"
#include "drivers/timer.h"
#include "cpu/heap.h"
#include "cpu/paging.h"
#include "cpu/gdt.h"

/* =========================================================================
 * External symbol provided by syscall_entry.asm
 * Declared here at file scope so both process_trampoline() and schedule()
 * can reference it without repeating the extern inside function bodies.
 * ======================================================================= */
extern uint8_t *syscall_kernel_stack_top;

/* =========================================================================
 * Module state
 * ======================================================================= */

static struct process  process_table[MAX_PROCESSES]; /* all PCB slots        */
static struct process *run_queue_head = NULL;        /* circular READY list  */
static struct process *current_proc   = NULL;        /* currently executing  */
static struct process *idle_proc      = NULL;        /* always-ready idle    */
static struct sched_stats stats;                     /* lifetime counters    */
static int  scheduler_active = 0;                    /* set after init       */
static int  next_pid         = 1;                    /* monotonic PID counter*/

/* =========================================================================
 * Forward declarations of internal helpers
 * ======================================================================= */

static struct process *alloc_process(void);
static void            free_process(struct process *proc);
static void            enqueue(struct process *proc);
static void            dequeue(struct process *proc);
static struct process *pick_next(void);
static int             setup_kernel_stack(struct process *proc);
static int             alloc_pid(void);
static void            idle_loop(void);
static void            process_trampoline(void);

/* =========================================================================
 * process_trampoline
 *
 * Every new process's initial rip points here (set up in setup_kernel_stack).
 *
 * Kernel process: load_base holds the C function pointer; call it then exit.
 * User process:   transition to Ring 3 via SYSRETQ.
 * ======================================================================= */
static void process_trampoline(void) {
    struct process *proc = current_proc;

    if (proc->user_entry == 0) {
        /* Kernel process: load_base is repurposed as a function pointer */
        void (*fn)(void) = (void (*)(void))(uintptr_t)proc->load_base;
        fn();
        process_exit(0);
        while (1) __asm__ volatile("hlt");
    }

    /*
     * User process: transition to Ring 3 via SYSRETQ.
     *
     * Point syscall_kernel_stack_top at this process's kernel stack so
     * that the syscall entry stub switches to the correct stack on
     * the first system call from this process.
     *
     * SYSRETQ register requirements:
     *   RCX = user RIP (entry point)
     *   R11 = user RFLAGS
     *   RSP = user stack pointer
     *   IF  = 0 (cleared by CLI before SYSRETQ)
     */
    syscall_kernel_stack_top = proc->kernel_stack_top;

    uint64_t urip = proc->user_entry;
    uint64_t ursp = proc->user_stack_top;

    __asm__ volatile(
        "cli\n\t"
        "mov %[rip], %%rcx\n\t"   /* RCX <- user entry point */
        "mov $0x202, %%r11\n\t"   /* R11 <- RFLAGS: IF=1, bit1=1 */
        "mov %[rsp], %%rsp\n\t"   /* RSP <- user stack (last C stack ref) */
        "sysretq\n\t"
        :
        : [rip] "r"(urip), [rsp] "r"(ursp)
        : "rcx", "r11", "memory"
    );

    while (1) __asm__ volatile("hlt");  /* unreachable */
}

/* =========================================================================
 * idle_loop - executes when no user process is READY
 * ======================================================================= */
static void idle_loop(void) {
    while (1) {
        __asm__ volatile("sti; hlt" ::: "memory");
    }
}

/* =========================================================================
 * Internal run-queue helpers
 * ======================================================================= */

/* alloc_process - find and zero a free slot in process_table. */
static struct process *alloc_process(void) {
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == PROC_UNUSED) {
            memset(&process_table[i], 0, sizeof(struct process));
            return &process_table[i];
        }
    }
    return NULL;
}

/* free_process - release the kernel stack and mark the slot UNUSED. */
static void free_process(struct process *proc) {
    if (proc->kernel_stack) {
        kfree(proc->kernel_stack);
        proc->kernel_stack     = NULL;
        proc->kernel_stack_top = NULL;
    }
    proc->state = PROC_UNUSED;
}

/* enqueue - append proc to the tail of the circular run-queue. */
static void enqueue(struct process *proc) {
    if (!run_queue_head) {
        proc->next     = proc;   /* single element: points to itself */
        run_queue_head = proc;
        return;
    }

    struct process *tail = run_queue_head;
    while (tail->next != run_queue_head) {
        tail = tail->next;
    }
    tail->next = proc;
    proc->next = run_queue_head;
}

/* dequeue - remove proc from the circular run-queue. */
static void dequeue(struct process *proc) {
    if (!run_queue_head) return;

    if (run_queue_head == proc && proc->next == proc) {
        /* Only element in the queue */
        run_queue_head = NULL;
        proc->next     = NULL;
        return;
    }

    struct process *prev = run_queue_head;
    while (prev->next != proc) {
        prev = prev->next;
        if (prev == run_queue_head) return;  /* proc not in queue */
    }

    prev->next = proc->next;
    if (run_queue_head == proc) {
        run_queue_head = proc->next;
    }
    proc->next = NULL;
}

/*
 * pick_next - choose the next READY process to run.
 *
 * First unblocks any sleeping processes whose wake_at_ms has passed,
 * then picks the first READY process after current_proc in the queue.
 * Falls back to idle_proc if nothing is runnable.
 */
static struct process *pick_next(void) {
    if (!run_queue_head) return idle_proc;

    uint64_t now = timer_get_uptime_ms();

    /* Unblock sleeping processes */
    struct process *p = run_queue_head;
    do {
        if (p->state == PROC_BLOCKED && p->wake_at_ms != 0 &&
            now >= p->wake_at_ms) {
            p->state      = PROC_READY;
            p->wake_at_ms = 0;
        }
        p = p->next;
    } while (p != run_queue_head);

    /* Find next READY process after current_proc */
    struct process *start = current_proc ? current_proc->next : run_queue_head;
    if (!start) start = run_queue_head;

    p = start;
    do {
        if (p->state == PROC_READY) return p;
        p = p->next;
    } while (p != start);

    return idle_proc;
}

/* alloc_pid - return the next monotonically increasing PID. */
static int alloc_pid(void) {
    return next_pid++;
}

/* =========================================================================
 * Kernel stack initialisation
 *
 * Places a hand-crafted cpu_context frame at the top of the kernel stack
 * so that the first context_switch() into this process pops registers
 * and returns into process_trampoline().
 *
 * Memory layout matches context_switch.asm push sequence:
 *   frame[0] = rbp   (lowest address, top of push sequence)
 *   frame[1] = rbx
 *   frame[2] = r12
 *   frame[3] = r13
 *   frame[4] = r14
 *   frame[5] = r15
 *   frame[6] = rip   (return address, pushed by the call instruction)
 * ======================================================================= */
static int setup_kernel_stack(struct process *proc) {
    proc->kernel_stack = (uint8_t *)kmalloc(KERNEL_STACK_SIZE);
    if (!proc->kernel_stack) return -1;

    memset(proc->kernel_stack, 0, KERNEL_STACK_SIZE);
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;

    uint64_t *frame = (uint64_t *)(proc->kernel_stack_top -
                                   sizeof(struct cpu_context));
    frame[0] = 0;                                         /* rbp */
    frame[1] = 0;                                         /* rbx */
    frame[2] = 0;                                         /* r12 */
    frame[3] = 0;                                         /* r13 */
    frame[4] = 0;                                         /* r14 */
    frame[5] = 0;                                         /* r15 */
    frame[6] = (uint64_t)(uintptr_t)process_trampoline;  /* rip */

    proc->context = (struct cpu_context *)frame;
    return 0;
}

/* =========================================================================
 * Public API
 * ======================================================================= */

/*
 * scheduler_init - create the idle process and prepare the run-queue.
 * Must be called once during kernel_init() before any process is spawned.
 */
void scheduler_init(void) {
    memset(process_table, 0, sizeof(process_table));
    memset(&stats, 0, sizeof(stats));
    run_queue_head   = NULL;
    current_proc     = NULL;
    scheduler_active = 0;

    idle_proc = alloc_process();
    idle_proc->pid             = 0;
    idle_proc->state           = PROC_READY;
    idle_proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    idle_proc->load_base       = (uint64_t)(uintptr_t)idle_loop;
    idle_proc->user_entry      = 0;  /* 0 = kernel process in trampoline */
    strncpy(idle_proc->name, "idle", PROCESS_NAME_LEN);

    if (setup_kernel_stack(idle_proc) != 0) {
        panic("scheduler_init: cannot allocate idle kernel stack");
    }

    enqueue(idle_proc);
    current_proc        = idle_proc;
    current_proc->state = PROC_RUNNING;
    scheduler_active    = 1;

    vga_writestring("Scheduler: Initialized (max ");
    print_dec(MAX_PROCESSES);
    vga_writestring(" processes, ");
    print_dec(SCHED_TICKS_PER_SLICE);
    vga_writestring(" ticks/slice)\n");
}

/*
 * process_create_kernel - create a kernel-mode process running func().
 * Returns the new PCB, or NULL if the process table is full.
 */
struct process *process_create_kernel(const char *name, void (*func)(void)) {
    struct process *proc = alloc_process();
    if (!proc) {
        vga_writestring("Scheduler: process table full\n");
        return NULL;
    }

    proc->pid            = alloc_pid();
    proc->state          = PROC_READY;
    proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms  = timer_get_uptime_ms();
    proc->load_base      = (uint64_t)(uintptr_t)func;
    proc->user_entry     = 0;  /* 0 = kernel process */
    strncpy(proc->name, name, PROCESS_NAME_LEN);

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

/*
 * process_create_user - create a user-mode process from a loaded ELF image.
 * entry:        virtual address of _start
 * stack_top:    initial RSP value (top of the user stack)
 * stack_bottom: lowest mapped virtual address of the user stack
 */
struct process *process_create_user(const char *name,
                                    uint64_t    entry,
                                    uint64_t    stack_top,
                                    uint64_t    stack_bottom) {
    struct process *proc = alloc_process();
    if (!proc) {
        vga_writestring("Scheduler: process table full\n");
        return NULL;
    }

    proc->pid             = alloc_pid();
    proc->state           = PROC_READY;
    proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms   = timer_get_uptime_ms();
    proc->user_entry       = entry;
    proc->user_stack_top   = stack_top;
    proc->user_stack_bottom = stack_bottom;
    strncpy(proc->name, name, PROCESS_NAME_LEN);

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

/*
 * process_spawn - convenience wrapper: create a user process and make it READY.
 */
struct process *process_spawn(const char *name,
                              uint64_t    entry,
                              uint64_t    stack_top,
                              uint64_t    stack_bottom) {
    return process_create_user(name, entry, stack_top, stack_bottom);
}

/*
 * process_mark_zombie - transition proc to ZOMBIE, dequeue it, and free its
 * virtual address space.  Called from sys_exit() and the exception handler.
 */
void process_mark_zombie(struct process *proc, int exit_code) {
    proc->exit_code = exit_code;
    proc->state     = PROC_ZOMBIE;
    dequeue(proc);
    stats.processes_exited++;
    if (stats.active_processes > 0) stats.active_processes--;

    /* Free user virtual pages so the address range can be reused */
    if (proc->user_entry != 0) {
        uint64_t stack_page_top =
            paging_align_up(proc->user_stack_top + 8, PAGE_SIZE);
        elf_unload(proc->load_base,       proc->load_end,
                   proc->user_stack_bottom, stack_page_top);
    }
}

/*
 * process_reap - free the kernel stack and mark the PCB slot UNUSED.
 * Call after process_mark_zombie() once the exit code has been read.
 */
void process_reap(struct process *proc) {
    if (!proc) return;

    __asm__ volatile("cli");
    if (proc->state == PROC_ZOMBIE) {
        dequeue(proc);     /* defensive: already dequeued by mark_zombie */
        free_process(proc);
    }
    __asm__ volatile("sti");
}

/*
 * process_exit - terminate the calling process.  Never returns.
 */
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

    __asm__ volatile("sti");
    schedule();

    while (1) __asm__ volatile("hlt");  /* unreachable */
}

/*
 * process_sleep_until - block the calling process until uptime_ms >= wake_ms.
 */
void process_sleep_until(uint64_t wake_ms) {
    __asm__ volatile("cli");
    if (current_proc && current_proc != idle_proc) {
        current_proc->state      = PROC_BLOCKED;
        current_proc->wake_at_ms = wake_ms;
        dequeue(current_proc);
        enqueue(current_proc);  /* re-enqueue as BLOCKED so pick_next can see it */
    }
    __asm__ volatile("sti");
    schedule();
}

/*
 * schedule - pick the next READY process and perform a context switch.
 * Safe to call from both voluntary yield and timer-IRQ preemption.
 */
void schedule(void) {
    if (!scheduler_active) return;

    __asm__ volatile("cli");

    struct process *next = pick_next();

    if (next == current_proc) {
        __asm__ volatile("sti");
        return;  /* nothing to switch to */
    }

    struct process *old = current_proc;
    current_proc        = next;

    if (old->state == PROC_RUNNING) old->state = PROC_READY;
    next->state            = PROC_RUNNING;
    next->ticks_remaining  = SCHED_TICKS_PER_SLICE;

    /* Update both ring-3 entry paths to use the new kernel stack */
    tss_set_rsp0((uint64_t)(uintptr_t)next->kernel_stack_top);
    syscall_kernel_stack_top = next->kernel_stack_top;

    stats.context_switches++;
    stats.total_ticks++;

    __asm__ volatile("sti");

    /* Perform the CPU context switch; returns when old is scheduled again */
    context_switch(&old->context, next->context);
}

/*
 * scheduler_tick - called from the timer IRQ every tick.
 * Wakes sleeping processes and preempts the current process when its
 * time slice expires.
 */
void scheduler_tick(void) {
    if (!scheduler_active || !current_proc) return;

    current_proc->total_ticks++;
    stats.total_ticks++;

    /* Unblock sleeping processes that are due */
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

    /* Preempt if the time slice has expired */
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

struct process *scheduler_current(void)   { return current_proc; }
struct process *scheduler_get_idle(void)  { return idle_proc;    }
struct sched_stats scheduler_get_stats(void) { return stats;     }

/* =========================================================================
 * Diagnostics
 * ======================================================================= */

void scheduler_print_stats(void) {
    vga_writestring("\nScheduler Statistics:\n");
    vga_writestring("  Context switches:  "); print_dec(stats.context_switches);  vga_writestring("\n");
    vga_writestring("  Total ticks:       "); print_dec(stats.total_ticks);        vga_writestring("\n");
    vga_writestring("  Processes created: "); print_dec(stats.processes_created);  vga_writestring("\n");
    vga_writestring("  Processes exited:  "); print_dec(stats.processes_exited);   vga_writestring("\n");
    vga_writestring("  Active processes:  "); print_dec(stats.active_processes);   vga_writestring("\n");
}

void scheduler_print_processes(void) {
    static const char *state_names[] = {
        "UNUSED  ", "READY   ", "RUNNING ", "BLOCKED ", "ZOMBIE  "
    };

    vga_writestring("\nProcess Table:\n");
    vga_writestring("  PID  STATE     TICKS  NAME\n");
    vga_writestring("  ---  --------  -----  ----\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &process_table[i];
        if (p->state == PROC_UNUSED) continue;

        vga_writestring("  ");
        print_dec((uint64_t)p->pid);
        vga_writestring("    ");

        uint8_t st = (uint8_t)p->state;
        vga_writestring(st < 5 ? state_names[st] : "?       ");

        vga_writestring("  ");
        print_dec(p->total_ticks);
        vga_writestring("     ");
        vga_writestring(p->name);
        vga_writestring("\n");
    }
}