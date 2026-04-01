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
#include "drivers/graphices/vga.h"
#include "drivers/timer.h"
#include "cpu/fpu.h"
#include "cpu/paging.h"
#include "cpu/tss.h"

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
static struct process_vm_space *alloc_vm_space(void);
static void            retain_vm_space(struct process_vm_space *vm);
static int             release_vm_space(struct process *proc);
static int             map_zeroed_user_range(uint64_t start, uint64_t end,
                                             uint64_t flags);
static int             map_main_thread_tls(struct process *proc);
static int             alloc_user_thread_region(struct process *proc);
static void            write_fs_base(uint64_t value);
static void            idle_loop(void);
static void            process_trampoline(void);
static void            copy_name(char *dst, const char *src, size_t cap);

#define IA32_FS_BASE_MSR 0xC0000100
#define USER_TLS_TCB_SIZE 8

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
        kernel_thread_entry_t fn =
            (kernel_thread_entry_t)(uintptr_t)proc->load_base;
        if (fn) {
            fn((void *)(uintptr_t)proc->kernel_arg);
        }
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
    uint64_t uarg0 = proc->user_arg0;
    uint64_t uarg1 = proc->user_arg1;
    uint64_t uarg2 = proc->user_arg2;

    write_fs_base(proc->user_fs_base);

    __asm__ volatile(
        "cli\n\t"
        "mov %[rip], %%rcx\n\t"   /* RCX <- user entry point */
        "mov $0x202, %%r11\n\t"   /* R11 <- RFLAGS: IF=1, bit1=1 */
        "mov %[arg0], %%rdi\n\t"
        "mov %[arg1], %%rsi\n\t"
        "mov %[arg2], %%rdx\n\t"
        "mov %[rsp], %%rsp\n\t"   /* RSP <- user stack (last C stack ref) */
        "sysretq\n\t"
        :
        : [rip] "r"(urip), [rsp] "r"(ursp),
          [arg0] "r"(uarg0), [arg1] "r"(uarg1), [arg2] "r"(uarg2)
        : "rcx", "r11", "rdi", "rsi", "rdx", "memory"
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
    proc->vm_space = NULL;
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

static void copy_name(char *dst, const char *src, size_t cap) {
    if (!dst || cap == 0) return;
    if (!src) { dst[0] = '\0'; return; }
    size_t i = 0;
    while (src[i] && i + 1 < cap) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
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

/* alloc_pid - return the lowest free PID (starting at 1). */
static int alloc_pid(void) {
    for (int pid = 1; pid < MAX_PROCESSES; pid++) {
        int used = 0;
        for (int i = 0; i < MAX_PROCESSES; i++) {
            if (process_table[i].state != PROC_UNUSED &&
                process_table[i].pid == pid) {
                used = 1;
                break;
            }
        }
        if (!used) return pid;
    }
    return -1;
}

static struct process_vm_space *alloc_vm_space(void) {
    struct process_vm_space *vm =
        (struct process_vm_space *)kzalloc(sizeof(*vm));
    if (!vm) return NULL;
    vm->ref_count = 1;
    return vm;
}

static void retain_vm_space(struct process_vm_space *vm) {
    if (vm) vm->ref_count++;
}

static int release_vm_space(struct process *proc) {
    if (!proc || !proc->vm_space) return 0;

    struct process_vm_space *vm = proc->vm_space;
    proc->vm_space = NULL;
    if (vm->ref_count == 0) {
        kfree(vm);
        return 1;
    }

    vm->ref_count--;
    if (vm->ref_count == 0) {
        uint64_t old_cr3 = paging_get_current_cr3();
        struct page_table *old_pml4 = paging_get_active_pml4();
        if (vm->cr3 && vm->cr3 != old_cr3) {
            paging_set_active_pml4((struct page_table *)(uintptr_t)vm->cr3);
            paging_switch_to(vm->cr3);
        }
        if (vm->load_end > vm->load_base) {
            elf_unload(vm->load_base, vm->load_end, 0, 0);
        }
        if (old_cr3 && old_cr3 != vm->cr3) {
            paging_set_active_pml4(old_pml4);
            paging_switch_to(old_cr3);
        }
        kfree(vm);
        return 1;
    }

    return 0;
}

static int map_zeroed_user_range(uint64_t start, uint64_t end, uint64_t flags) {
    if (end <= start) return 0;

    for (uint64_t virt = start; virt < end; virt += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return -1;
        if (paging_map_page(virt, phys, flags) != 0) {
            pmm_free_frame(phys);
            return -1;
        }
        memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
    }

    return 0;
}

static void write_fs_base(uint64_t value) {
    __asm__ volatile("wrmsr" :: "c"(IA32_FS_BASE_MSR),
                                "a"((uint32_t)value),
                                "d"((uint32_t)(value >> 32))
                     : "memory");
}

static int map_main_thread_tls(struct process *proc) {
    if (!proc || !proc->vm_space) return -1;

    proc->user_tls_bottom = proc->user_stack_bottom;
    proc->user_fs_base = 0;

    struct process_vm_space *vm = proc->vm_space;
    if (vm->tls_memsz == 0) {
        vm->stack_cursor = proc->user_stack_bottom;
        return 0;
    }

    uint64_t align = vm->tls_align ? vm->tls_align : 1;
    uint64_t tls_block_size = paging_align_up(vm->tls_memsz, align);
    uint64_t tls_top = proc->user_stack_bottom;
    uint64_t tls_data_start = tls_top - tls_block_size;
    uint64_t tls_bottom = paging_align_down(tls_data_start - USER_TLS_TCB_SIZE,
                                            PAGE_SIZE);

    if (tls_bottom < vm->load_end) return -1;
    if (map_zeroed_user_range(tls_bottom, tls_top,
                              PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
        return -1;
    }

    proc->user_tls_bottom = tls_bottom;
    proc->user_fs_base = tls_top - USER_TLS_TCB_SIZE;
    if (vm->tls_filesz != 0) {
        memcpy((void *)(uintptr_t)tls_data_start,
               (const void *)(uintptr_t)vm->tls_image_start,
               (size_t)vm->tls_filesz);
    }
    *(uint64_t *)(uintptr_t)proc->user_fs_base = proc->user_fs_base;
    vm->stack_cursor = tls_bottom;
    return 0;
}

static int alloc_user_thread_region(struct process *proc) {
    if (!proc || !proc->vm_space) return -1;

    struct process_vm_space *vm = proc->vm_space;
    uint64_t stack_top_page = vm->stack_cursor;
    uint64_t stack_bottom = stack_top_page - USER_STACK_SIZE;
    if (stack_bottom <= vm->load_end) return -1;

    if (map_zeroed_user_range(stack_bottom, stack_top_page,
                              PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
        return -1;
    }

    proc->user_stack_bottom = stack_bottom;
    proc->user_stack_top = (stack_top_page - 8) & ~(uint64_t)0xFULL;
    proc->user_tls_bottom = stack_bottom;
    proc->user_fs_base = 0;

    if (vm->tls_memsz != 0) {
        uint64_t align = vm->tls_align ? vm->tls_align : 1;
        uint64_t tls_block_size = paging_align_up(vm->tls_memsz, align);
        uint64_t tls_top = stack_bottom;
        uint64_t tls_data_start = tls_top - tls_block_size;
        uint64_t tls_bottom = paging_align_down(tls_data_start - USER_TLS_TCB_SIZE,
                                                PAGE_SIZE);
        if (tls_bottom <= vm->load_end) {
            elf_unload(0, 0, stack_bottom, stack_top_page);
            return -1;
        }

        if (map_zeroed_user_range(tls_bottom, tls_top,
                                  PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            elf_unload(0, 0, stack_bottom, stack_top_page);
            return -1;
        }

        proc->user_tls_bottom = tls_bottom;
        proc->user_fs_base = tls_top - USER_TLS_TCB_SIZE;
        if (vm->tls_filesz != 0) {
            memcpy((void *)(uintptr_t)tls_data_start,
                   (const void *)(uintptr_t)vm->tls_image_start,
                   (size_t)vm->tls_filesz);
        }
        *(uint64_t *)(uintptr_t)proc->user_fs_base = proc->user_fs_base;
        vm->stack_cursor = tls_bottom;
    } else {
        vm->stack_cursor = stack_bottom;
    }

    return 0;
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
    idle_proc->group_id        = 0;
    idle_proc->state           = PROC_READY;
    idle_proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    idle_proc->load_base       = (uint64_t)(uintptr_t)idle_loop;
    idle_proc->user_entry      = 0;  /* 0 = kernel process in trampoline */
    strncpy(idle_proc->name, "idle", PROCESS_NAME_LEN);
    idle_proc->name[PROCESS_NAME_LEN - 1] = '\0';
    strncpy(idle_proc->cmdline, "idle", PROCESS_CMDLINE_LEN);
    idle_proc->cmdline[PROCESS_CMDLINE_LEN - 1] = '\0';
    idle_proc->flags           = PROC_FLAG_VERIFIED | PROC_FLAG_IDLE;
    idle_proc->cr3             = paging_get_kernel_cr3();

    if (setup_kernel_stack(idle_proc) != 0) {
        panic("scheduler_init: cannot allocate idle kernel stack");
    }
    fpu_init_state(idle_proc->fpu_state);

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

    proc->pid = alloc_pid();
    if (proc->pid < 0) {
        free_process(proc);
        vga_writestring("Scheduler: no free pid\n");
        return NULL;
    }
    proc->group_id        = proc->pid;
    proc->state           = PROC_READY;
    proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms   = timer_get_uptime_ms();
    proc->user_entry        = entry;
    proc->user_stack_top    = stack_top;
    proc->user_stack_bottom = stack_bottom;
    proc->user_tls_bottom   = stack_bottom;
    strncpy(proc->name, name, PROCESS_NAME_LEN);
    proc->name[PROCESS_NAME_LEN - 1] = '\0';
    strncpy(proc->cmdline, name ? name : "", PROCESS_CMDLINE_LEN);
    proc->cmdline[PROCESS_CMDLINE_LEN - 1] = '\0';
    proc->cr3 = paging_get_current_cr3();

    if (setup_kernel_stack(proc) != 0) {
        free_process(proc);
        return NULL;
    }
    fpu_init_state(proc->fpu_state);

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

struct process *process_create_kernel(const char *name,
                                      kernel_thread_entry_t entry,
                                      void *arg) {
    if (!entry) return NULL;

    struct process *proc = alloc_process();
    if (!proc) {
        vga_writestring("Scheduler: process table full\n");
        return NULL;
    }

    proc->pid = alloc_pid();
    if (proc->pid < 0) {
        free_process(proc);
        vga_writestring("Scheduler: no free pid\n");
        return NULL;
    }

    proc->group_id = proc->pid;
    proc->state = PROC_READY;
    proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms = timer_get_uptime_ms();
    proc->user_entry = 0;
    proc->load_base = (uint64_t)(uintptr_t)entry;
    proc->kernel_arg = (uint64_t)(uintptr_t)arg;
    proc->cr3 = paging_get_kernel_cr3();
    proc->flags = PROC_FLAG_KERNEL_THREAD;
    strncpy(proc->name, name ? name : "kthread", PROCESS_NAME_LEN);
    proc->name[PROCESS_NAME_LEN - 1] = '\0';
    strncpy(proc->cmdline, proc->name, PROCESS_CMDLINE_LEN);
    proc->cmdline[PROCESS_CMDLINE_LEN - 1] = '\0';

    if (setup_kernel_stack(proc) != 0) {
        free_process(proc);
        return NULL;
    }
    fpu_init_state(proc->fpu_state);

    enqueue(proc);
    stats.processes_created++;
    stats.active_processes++;

    vga_writestring("Scheduler: Created kernel thread '");
    vga_writestring(proc->name);
    vga_writestring("' (pid ");
    print_dec((uint64_t)proc->pid);
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

struct process *process_spawn_user_thread(const char *name,
                                          uint64_t entry,
                                          uint64_t arg0,
                                          uint64_t arg1) {
    if (!current_proc || !current_proc->vm_space) return NULL;

    struct process *proc = alloc_process();
    if (!proc) return NULL;

    proc->pid = alloc_pid();
    if (proc->pid < 0) {
        free_process(proc);
        return NULL;
    }

    proc->group_id = current_proc->group_id;
    proc->state = PROC_READY;
    proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    proc->created_at_ms = timer_get_uptime_ms();
    proc->vm_space = current_proc->vm_space;
    retain_vm_space(proc->vm_space);
    proc->user_entry = entry;
    proc->user_arg0 = arg0;
    proc->user_arg1 = arg1;
    proc->load_base = proc->vm_space->load_base;
    proc->load_end = proc->vm_space->load_end;
    proc->cr3 = proc->vm_space->cr3;
    copy_name(proc->name, name ? name : current_proc->name, sizeof(proc->name));
    copy_name(proc->cmdline, current_proc->cmdline, sizeof(proc->cmdline));

    if (setup_kernel_stack(proc) != 0) {
        release_vm_space(proc);
        free_process(proc);
        return NULL;
    }
    fpu_init_state(proc->fpu_state);

    if (alloc_user_thread_region(proc) != 0) {
        if (proc->user_stack_bottom && proc->user_stack_top) {
            uint64_t stack_top_page =
                paging_align_up(proc->user_stack_top + 8, PAGE_SIZE);
            elf_unload(0, 0, proc->user_tls_bottom, stack_top_page);
        }
        release_vm_space(proc);
        free_process(proc);
        return NULL;
    }

    enqueue(proc);
    stats.processes_created++;
    stats.active_processes++;
    return proc;
}

struct process *process_spawn_kernel(const char *name,
                                     kernel_thread_entry_t entry,
                                     void *arg) {
    return process_create_kernel(name, entry, arg);
}

int process_configure_image(struct process *proc,
                            const struct elf_load_result *image,
                            uint64_t cr3) {
    if (!proc || !image || !cr3) return -1;

    struct process_vm_space *vm = alloc_vm_space();
    if (!vm) return -1;

    vm->cr3 = cr3;
    vm->load_base = image->load_base;
    vm->load_end = image->load_end;
    vm->stack_cursor = image->stack_bottom;
    vm->tls_image_start = image->tls_image_start;
    vm->tls_filesz = image->tls_filesz;
    vm->tls_memsz = image->tls_memsz;
    vm->tls_align = image->tls_align ? image->tls_align : 1;

    proc->vm_space = vm;
    proc->load_base = vm->load_base;
    proc->load_end = vm->load_end;
    proc->cr3 = vm->cr3;

    uint64_t old_cr3 = paging_get_current_cr3();
    struct page_table *old_pml4 = paging_get_active_pml4();

    __asm__ volatile("cli");
    paging_set_active_pml4((struct page_table *)(uintptr_t)cr3);
    paging_switch_to(cr3);
    int rc = map_main_thread_tls(proc);
    paging_set_active_pml4(old_pml4);
    paging_switch_to(old_cr3);
    __asm__ volatile("sti");

    if (rc != 0) {
        release_vm_space(proc);
        proc->load_base = 0;
        proc->load_end = 0;
        proc->cr3 = 0;
        return -1;
    }

    return 0;
}

/*
 * process_mark_zombie - transition proc to ZOMBIE, dequeue it, and free its
 * virtual address space.  Called from sys_exit() and the exception handler.
 */
void process_mark_zombie(struct process *proc, int exit_code) {
    if (!proc) return;

    proc->exit_code = exit_code;
    proc->thread_exit_value = (uint64_t)(int64_t)exit_code;
    proc->state     = PROC_ZOMBIE;
    dequeue(proc);
    stats.processes_exited++;
    if (stats.active_processes > 0) stats.active_processes--;

    if (proc->user_entry != 0) {
        uint64_t stack_page_top =
            paging_align_up(proc->user_stack_top + 8, PAGE_SIZE);
        elf_unload(0, 0, proc->user_tls_bottom, stack_page_top);
        proc->user_stack_top = 0;
        proc->user_stack_bottom = 0;
        proc->user_tls_bottom = 0;
        proc->user_fs_base = 0;
    }

    release_vm_space(proc);
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

void process_discard(struct process *proc) {
    if (!proc) return;

    __asm__ volatile("cli");
    dequeue(proc);
    release_vm_space(proc);
    if (stats.active_processes > 0) stats.active_processes--;
    free_process(proc);
    __asm__ volatile("sti");
}

/*
 * process_exit - terminate the calling process.  Never returns.
 */
void process_exit(int exit_code) {
    process_exit_value((uint64_t)(int64_t)exit_code);
}

void process_exit_value(uint64_t exit_value) {
    __asm__ volatile("cli");

    if (current_proc && current_proc != idle_proc) {
        vga_writestring("\nScheduler: Process '");
        vga_writestring(current_proc->name);
        vga_writestring("' (pid ");
        print_dec((uint64_t)current_proc->pid);
        vga_writestring(") exited with code ");
        print_dec(exit_value);
        vga_writestring("\n");

        current_proc->thread_exit_value = exit_value;
        process_mark_zombie(current_proc, (int)(int64_t)exit_value);
        current_proc->thread_exit_value = exit_value;
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
    tss_set_kernel_stack((uint64_t)(uintptr_t)next->kernel_stack_top);
    syscall_kernel_stack_top = next->kernel_stack_top;

    stats.context_switches++;
    stats.total_ticks++;

    fpu_save(old->fpu_state);
    paging_switch_to(next->cr3);
    write_fs_base(next->user_entry ? next->user_fs_base : 0);
    fpu_restore(next->fpu_state);

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

    /* Time slice accounting.
     *
     * Preemptive context switching from inside the timer IRQ requires an
     * interrupt-frame based switch path. The current scheduler uses a
     * call/ret based context frame, so preempting here can strand the IRQ
     * return path on the old kernel stack and stop further IRQ delivery.
     *
     * For now this is cooperative. Processes switch on explicit schedule()
     * calls such as sys_exit and sys_sleep_ms. */
    if (current_proc->ticks_remaining > 0) {
        current_proc->ticks_remaining--;
    }
    if (current_proc->ticks_remaining == 0) {
        current_proc->ticks_remaining = SCHED_TICKS_PER_SLICE;
    }
}

/* =========================================================================
 * Public accessors
 * ======================================================================= */

struct process *scheduler_current(void)   { return current_proc; }
struct process *scheduler_get_idle(void)  { return idle_proc;    }
void scheduler_get_stats(struct sched_stats *out) {
    if (!out) return;
    *out = stats;
}

int scheduler_list_processes(struct proc_info *out, int max) {
    if (!out || max <= 0) return 0;

    int count = 0;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &process_table[i];
        if (p->state == PROC_UNUSED) continue;
        if (count >= max) break;

        struct proc_info *dst = &out[count];
        memset(dst, 0, sizeof(*dst));
        dst->pid = p->pid;
        dst->state = (int)p->state;
        dst->flags = p->flags;
        dst->total_ticks = p->total_ticks;
        dst->created_at_ms = p->created_at_ms;
        dst->load_base = p->load_base;
        dst->load_end = p->load_end;
        dst->memory_bytes = 0;

        if (p->kernel_stack && p->kernel_stack_top) {
            dst->memory_bytes += KERNEL_STACK_SIZE;
        }
        if (p->load_end > p->load_base) {
            dst->memory_bytes += p->load_end - p->load_base;
        }
        if (p->user_stack_top > p->user_stack_bottom) {
            dst->memory_bytes += p->user_stack_top - p->user_stack_bottom;
        }
        if (p->user_stack_bottom > p->user_tls_bottom) {
            dst->memory_bytes += p->user_stack_bottom - p->user_tls_bottom;
        }

        copy_name(dst->name, p->name, PROCINFO_NAME_LEN);
        count++;
    }
    return count;
}

struct process *scheduler_find_process(int pid) {
    if (pid < 0) return NULL;
    for (int i = 0; i < MAX_PROCESSES; i++) {
        if (process_table[i].state != PROC_UNUSED &&
            process_table[i].pid == pid) {
            return &process_table[i];
        }
    }
    return NULL;
}

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
    vga_writestring("  PID  STATE     TICKS  MEM(KiB)  VER  NAME\n");
    vga_writestring("  ---  --------  -----  --------  ---  ----\n");

    for (int i = 0; i < MAX_PROCESSES; i++) {
        struct process *p = &process_table[i];
        if (p->state == PROC_UNUSED) continue;
        uint64_t mem_bytes = 0;

        if (p->kernel_stack && p->kernel_stack_top) {
            mem_bytes += KERNEL_STACK_SIZE;
        }
        if (p->load_end > p->load_base) {
            mem_bytes += p->load_end - p->load_base;
        }
        if (p->user_stack_top > p->user_stack_bottom) {
            mem_bytes += p->user_stack_top - p->user_stack_bottom;
        }
        if (p->user_stack_bottom > p->user_tls_bottom) {
            mem_bytes += p->user_stack_bottom - p->user_tls_bottom;
        }

        vga_writestring("  ");
        print_dec((uint64_t)p->pid);
        vga_writestring("    ");

        uint8_t st = (uint8_t)p->state;
        vga_writestring(st < 5 ? state_names[st] : "?       ");

        vga_writestring("  ");
        print_dec(p->total_ticks);
        vga_writestring("  ");
        print_dec(mem_bytes / 1024);
        vga_writestring("       ");
        vga_writestring((p->flags & PROC_FLAG_VERIFIED) ? "YES" : "NO ");
        vga_writestring("  ");
        if (p->flags & PROC_FLAG_IDLE) vga_writestring("idle");
        else                           vga_writestring(p->name);
        vga_writestring("\n");
    }
}
