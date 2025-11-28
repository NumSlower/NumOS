/*
 * process.c - User process management implementation
 */

#include "kernel/process.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/gdt.h"

/* Process table */
static struct process process_table[MAX_PROCESSES];
static uint32_t next_pid = 1;
static struct process *current_process = NULL;

int process_init(void) {
    vga_writestring("Process: Initializing process management...\n");
    
    /* Clear process table */
    memset(process_table, 0, sizeof(process_table));
    next_pid = 1;
    current_process = NULL;
    
    vga_writestring("Process: Process management initialized\n");
    return 0;
}

struct process* process_create(const char *name, uint64_t entry_point, uint64_t stack_top) {
    /* Find free process slot */
    struct process *proc = NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state == 0) {
            proc = &process_table[i];
            break;
        }
    }
    
    if (!proc) {
        vga_writestring("Process: ERROR - No free process slots\n");
        return NULL;
    }
    
    /* Initialize process */
    memset(proc, 0, sizeof(struct process));
    proc->pid = next_pid++;
    proc->state = PROCESS_STATE_READY;
    proc->rip = entry_point;
    proc->rsp = stack_top;
    proc->rbp = stack_top;
    proc->rflags = 0x202;  /* IF=1 (interrupts enabled), reserved bit 1 */
    
    if (name) {
        strncpy(proc->name, name, sizeof(proc->name) - 1);
        proc->name[sizeof(proc->name) - 1] = '\0';
    }
    
    vga_writestring("Process: Created process '");
    vga_writestring(proc->name);
    vga_writestring("' (PID ");
    print_dec(proc->pid);
    vga_writestring(")\n");
    vga_writestring("  Entry: 0x");
    print_hex(entry_point);
    vga_writestring("\n  Stack: 0x");
    print_hex(stack_top);
    vga_writestring("\n");
    
    return proc;
}

int process_exec(struct process *proc) {
    if (!proc) {
        return -1;
    }
    
    vga_writestring("Process: Executing process ");
    print_dec(proc->pid);
    vga_writestring(" (");
    vga_writestring(proc->name);
    vga_writestring(")\n");
    vga_writestring("  Entry point: 0x");
    print_hex(proc->rip);
    vga_writestring("\n  Stack pointer: 0x");
    print_hex(proc->rsp);
    vga_writestring("\n");
    
    /* Set as current process */
    current_process = proc;
    proc->state = PROCESS_STATE_RUNNING;
    
    /* Switch to user mode and execute */
    vga_writestring("Process: Switching to ring 3 (user mode)...\n\n");
    process_switch_to_user(proc->rip, proc->rsp);
    
    /* Should not return unless process exits */
    return 0;
}

void process_exit(struct process *proc, int exit_code) {
    if (!proc) {
        return;
    }
    
    vga_writestring("Process: Process ");
    print_dec(proc->pid);
    vga_writestring(" exiting with code ");
    print_dec(exit_code);
    vga_writestring("\n");
    
    proc->state = PROCESS_STATE_ZOMBIE;
    proc->exit_code = exit_code;
    
    /* TODO: Free process resources (pages, etc.) */
    
    /* Mark as free */
    proc->state = 0;
    
    if (current_process == proc) {
        current_process = NULL;
    }
}

struct process* process_get_current(void) {
    return current_process;
}

void process_list(void) {
    vga_writestring("Process List:\n");
    vga_writestring("PID  State    Name\n");
    vga_writestring("---  -------  ----\n");
    
    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != 0) {
            print_dec(process_table[i].pid);
            vga_writestring("  ");
            
            switch (process_table[i].state) {
                case PROCESS_STATE_READY:
                    vga_writestring("Ready   ");
                    break;
                case PROCESS_STATE_RUNNING:
                    vga_writestring("Running ");
                    break;
                case PROCESS_STATE_BLOCKED:
                    vga_writestring("Blocked ");
                    break;
                case PROCESS_STATE_ZOMBIE:
                    vga_writestring("Zombie  ");
                    break;
            }
            
            vga_writestring(" ");
            vga_writestring(process_table[i].name);
            vga_writestring("\n");
            count++;
        }
    }
    
    if (count == 0) {
        vga_writestring("(no processes)\n");
    }
}

/* Assembly helper to switch to user mode - defined in process_switch.asm */
extern void _process_switch_to_user(uint64_t entry, uint64_t stack, 
                                    uint64_t user_ds, uint64_t user_cs);

void process_switch_to_user(uint64_t entry_point, uint64_t stack_pointer) {
    /* User mode segments from GDT */
    uint64_t user_ds = GDT_USER_DATA | 3;  /* RPL=3 for ring 3 */
    uint64_t user_cs = GDT_USER_CODE | 3;  /* RPL=3 for ring 3 */
    
    vga_writestring("Process: Entry=0x");
    print_hex(entry_point);
    vga_writestring(" Stack=0x");
    print_hex(stack_pointer);
    vga_writestring(" CS=0x");
    print_hex(user_cs);
    vga_writestring(" DS=0x");
    print_hex(user_ds);
    vga_writestring("\n");
    
    /* Switch to user mode */
    _process_switch_to_user(entry_point, stack_pointer, user_ds, user_cs);
}