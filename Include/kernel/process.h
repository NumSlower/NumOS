/*
 * process.h - User process management
 */

#ifndef PROCESS_H
#define PROCESS_H

#include "lib/base.h"

/* Process states */
#define PROCESS_STATE_READY     0
#define PROCESS_STATE_RUNNING   1
#define PROCESS_STATE_BLOCKED   2
#define PROCESS_STATE_ZOMBIE    3

/* Maximum number of processes */
#define MAX_PROCESSES 64

/* Process control block */
struct process {
    uint32_t pid;              /* Process ID */
    uint32_t state;            /* Process state */
    
    /* CPU context */
    uint64_t rip;              /* Instruction pointer */
    uint64_t rsp;              /* Stack pointer */
    uint64_t rbp;              /* Base pointer */
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi;
    uint64_t r8, r9, r10, r11, r12, r13, r14, r15;
    uint64_t rflags;
    
    /* Memory management */
    uint64_t cr3;              /* Page table base */
    uint64_t stack_base;       /* User stack base */
    uint64_t stack_size;       /* User stack size */
    
    /* Process info */
    char name[32];             /* Process name */
    uint32_t parent_pid;       /* Parent process ID */
    int exit_code;             /* Exit code */
};

/* Process management functions */
int process_init(void);
struct process* process_create(const char *name, uint64_t entry_point, uint64_t stack_top);
int process_exec(struct process *proc);
void process_exit(struct process *proc, int exit_code);
struct process* process_get_current(void);
void process_list(void);

/* Context switching */
void process_switch_to_user(uint64_t entry_point, uint64_t stack_pointer);

#endif /* PROCESS_H */