/* NumOS — process.c — Processes and kernel threads implementation
 *
 * Static PCB table with allocation from UNUSED slots, incrementing PIDs,
 * heap-allocated kernel stacks, and the entry trampoline contract: the
 * context switch resumes a new thread at thread_trampoline(), which calls
 * the thread entry and then process_exit() if the entry ever returns.
 */
#include "process/process.h"
#include "process/scheduler.h"
#include "cpu/heap.h"
#include "libs/string.h"
#include "libs/kprintf.h"

static struct process process_table[PROC_MAX_PROCS];
static uint32_t next_pid = 1;
static struct process* current = NULL;
static bool initialized = false;

void process_init(void) {
    memset(process_table, 0, sizeof(process_table));
    next_pid = 1;
    current = NULL;
    initialized = true;
}

struct process* process_create(const char* name, void (*entry)(void)) {
    if (!initialized || !name) return NULL;

    struct process* proc = NULL;
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        if (process_table[i].state == PROC_STATE_UNUSED) {
            proc = &process_table[i];
            break;
        }
    }
    if (!proc) {
        kprintf("process: table full - cannot create %s\n", name);
        return NULL;
    }

    void* stack = kmalloc(PROC_KERNEL_STACK_SIZE);
    if (!stack) {
        kprintf("process: no memory for %s kernel stack\n", name);
        return NULL;
    }

    memset(proc, 0, sizeof(*proc));
    proc->pid = next_pid++;
    strncpy(proc->name, name, PROC_NAME_MAX - 1);
    proc->name[PROC_NAME_MAX - 1] = '\0';
    proc->state = PROC_STATE_READY;
    proc->entry = entry;
    proc->quantum = PROC_DEFAULT_QUANTUM;
    proc->is_thread = true;
    proc->ppid = 0;
    proc->tty_index = 0;
    proc->terminate_requested = false;
    proc->kernel_stack_base = (uint64_t)stack;
    proc->kernel_stack_top = proc->kernel_stack_base + PROC_KERNEL_STACK_SIZE;
    proc->kernel_stack_top &= ~0xFull;

    /* Stack layout for first resume (growing down from the top):
     *   ret slot   -> thread_trampoline (calls entry in r12, then exits)
     *   rbp, rbx   -> 0
     *   r12        -> thread entry function
     *   r13, r14, r15 -> 0
     * proc_switch pops r15..rbp then rets into the trampoline. */
    uint64_t* sp = (uint64_t*)(proc->kernel_stack_top & ~0xFull);
    *--sp = (uint64_t)thread_trampoline;
    *--sp = (uint64_t)0;
    *--sp = (uint64_t)0;
    *--sp = (uint64_t)entry;
    *--sp = (uint64_t)0;
    *--sp = (uint64_t)0;
    *--sp = (uint64_t)0;
    proc->context.rsp = (uint64_t)sp;
    proc->context.rbp = proc->kernel_stack_top;
    proc->context.rbx = 0;
    proc->context.r12 = 0;
    proc->context.r13 = 0;
    proc->context.r14 = 0;
    proc->context.r15 = 0;

    scheduler_enqueue(proc);
    return proc;
}

void process_exit(void) {
    if (!current) return;

    current->state = PROC_STATE_ZOMBIE;
    kprintf("process: %s (pid %u) exited after %u yields\n",
            current->name, current->pid, current->yield_count);
    scheduler_schedule();
}

static bool parent_alive(uint32_t ppid) {
    if (ppid == 0) return false;
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        if (process_table[i].state != PROC_STATE_UNUSED &&
            process_table[i].state != PROC_STATE_ZOMBIE &&
            process_table[i].pid == ppid) {
            return true;
        }
    }
    return false;
}

void process_reap_zombies(struct process* exclude) {
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        if (&process_table[i] != exclude && process_table[i].state == PROC_STATE_ZOMBIE) {
            if (parent_alive(process_table[i].ppid)) continue; /* wait() will reap it */
            process_reap(&process_table[i]);
        }
    }
}

struct process* process_find_child_zombie(uint32_t ppid) {
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        if (process_table[i].state == PROC_STATE_ZOMBIE && process_table[i].ppid == ppid) {
            return &process_table[i];
        }
    }
    return NULL;
}

struct process* thread_create(const char* name, void (*entry)(void)) {
    return process_create(name, entry);
}

void process_reap(struct process* proc) {
    if (proc->kernel_stack_base) {
        kfree((void*)proc->kernel_stack_base);
    }
    memset(proc, 0, sizeof(*proc));
}

struct process* process_get_current(void) {
    return current;
}

void process_set_current(struct process* proc) {
    current = proc;
}

uint32_t process_get_next_pid(void) {
    return next_pid;
}

uint32_t process_get_count(void) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        if (process_table[i].state != PROC_STATE_UNUSED) count++;
    }
    return count;
}

struct process* process_get_by_index(uint32_t index) {
    uint32_t count = 0;
    for (uint32_t i = 0; i < PROC_MAX_PROCS; i++) {
        if (process_table[i].state != PROC_STATE_UNUSED) {
            if (count == index) return &process_table[i];
            count++;
        }
    }
    return NULL;
}
