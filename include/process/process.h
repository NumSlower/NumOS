/* NumOS — process.h — Processes and kernel threads
 *
 * Process control blocks, PID allocation, kernel stacks, and the saved
 * CPU context used by the context switch. Ring 0 only in this phase:
 * every process is a kernel thread sharing the kernel address space.
 */
#ifndef NUMOS_PROCESS_PROCESS_H
#define NUMOS_PROCESS_PROCESS_H

#include "libs/types.h"

#define PROC_MAX_PROCS 16
#define PROC_NAME_MAX 32
#define PROC_KERNEL_STACK_SIZE 0x4000
#define PROC_DEFAULT_QUANTUM 3
#define PROC_MAX_FDS 8

#define PROC_STATE_UNUSED   0
#define PROC_STATE_READY    1
#define PROC_STATE_RUNNING  2
#define PROC_STATE_BLOCKED  3
#define PROC_STATE_ZOMBIE   4

struct cpu_context {
    uint64_t rsp;
    uint64_t rbp;
    uint64_t rbx;
    uint64_t r12;
    uint64_t r13;
    uint64_t r14;
    uint64_t r15;
};

struct process {
    uint32_t pid;
    char name[PROC_NAME_MAX];
    uint8_t state;
    struct cpu_context context;
    uint64_t kernel_stack_base;
    uint64_t kernel_stack_top;
    void (*entry)(void);
    uint32_t quantum;
    uint32_t ticks_used;
    uint32_t yield_count;
    bool is_thread;
    uint32_t ppid;             /* parent pid, 0 = kernel-spawned */
    uint8_t tty_index;         /* controlling terminal for user processes */
    struct process* queue_next;
    /* Ring 3 state (Phase 10). cr3 == 0 for kernel threads. */
    bool is_user;
    uint64_t cr3;
    uint64_t user_rip;
    uint64_t user_stack_top;
    uint64_t user_heap_end;
    int exit_code;
    bool terminate_requested;
    void* fds[PROC_MAX_FDS];
};

void process_init(void);
struct process* process_create(const char* name, void (*entry)(void));
void process_exit(void);
void process_reap(struct process* proc);
struct process* process_get_current(void);
uint32_t process_get_count(void);
struct process* process_get_by_index(uint32_t index);
uint32_t process_get_next_pid(void);
void process_set_current(struct process* proc);
void process_reap_zombies(struct process* exclude);
/* Zombie reaping skips zombies whose parent is alive (they await wait());
 * orphans are reaped immediately. */
struct process* process_find_child_zombie(uint32_t ppid);
void proc_switch(struct cpu_context* from, struct cpu_context* to);
void thread_trampoline(void);
void user_first_entry(void);

#endif
