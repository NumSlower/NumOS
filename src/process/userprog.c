/* NumOS — userprog.c — Ring 3 user-process creation
 *
 * Builds a user address space: clones the kernel page tables (supervisor),
 * copies the embedded user program into fresh frames mapped at
 * USER_CODE_BASE with user/exec permissions, maps a user stack, and crafts
 * the initial kernel stack so the context switch lands on
 * user_first_entry, which pops the register frame and iretqs into Ring 3.
 * The TSS RSP0 privilege-transition stack is armed per process by the
 * scheduler.
 */
#include "process/userprog.h"
#include "process/elf.h"
#include "process/scheduler.h"
#include "process/process.h"
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "cpu/paging.h"
#include "cpu/gdt.h"
#include "cpu/heap.h"
#include "libs/string.h"
#include "libs/kprintf.h"

#define USER_CS_SEL 0x23
#define USER_SS_SEL 0x1B

extern void user_first_entry(void);

/* Boot shell: shell.elf embedded as a binary blob (Makefile). */
extern const uint8_t _binary_shell_elf_start[];
extern const uint8_t _binary_shell_elf_end[];

const uint8_t* user_program_start = _binary_shell_elf_start;
const uint8_t* user_program_end = _binary_shell_elf_end;

static const uint64_t USER_RFLAGS = 0x202;

bool userprog_init(void) {
    if (user_program_end <= user_program_start) {
        kprintf("userprog: no embedded user program\n");
        return false;
    }
    return true;
}

/* Reserves a free fd slot; 0..2 are stdio. */
static int fd_alloc(struct process* proc, void* file) {
    for (uint32_t i = 3; i < PROC_MAX_FDS; i++) {
        if (!proc->fds[i]) {
            proc->fds[i] = file;
            return (int)i;
        }
    }
    return -1;
}

int user_fd_alloc(struct process* proc, void* file) {
    return fd_alloc(proc, file);
}

void user_fd_free(struct process* proc, int fd) {
    if (fd >= 0 && fd < PROC_MAX_FDS) proc->fds[fd] = NULL;
}

void* user_fd_get(struct process* proc, int fd) {
    if (fd < 0 || fd >= PROC_MAX_FDS) return NULL;
    return proc->fds[fd];
}

/* Shared path: build a user process from an ELF64 image already in kernel
 * memory. pml4 and validation are handled by the ELF loader. When
 * argv_len > 0, the prepared argv/envp block (System V layout, already
 * assembled with rsp offset in block[0]) is copied into the topmost user
 * stack frame and user_rsp is taken from block[0]'s first word. */
static struct process* spawn_from_image(const char* name, const uint8_t* image, uint64_t size,
                                        const char* const* argv, const char* const* envp,
                                        uint32_t ppid, uint8_t tty_index) {
    uint64_t* pml4 = vmm_clone_kernel_space();
    if (!pml4) {
        kprintf("userprog: page-table clone failed for %s\n", name);
        return NULL;
    }

    uint64_t top_frame = 0;
    for (uint32_t i = 0; i < USER_STACK_PAGES; i++) {
        uint64_t frame = pmm_alloc_frame();
        if (!frame) {
            kprintf("userprog: out of frames for %s stack\n", name);
            vmm_free_address_space(pml4);
            return NULL;
        }
        if (i == 0) top_frame = frame; /* topmost user stack page */
        memset((void*)frame, 0, 4096);
        vmm_map_in(pml4, USER_STACK_TOP - (uint64_t)(i + 1) * 4096, frame,
                   PAGE_USER | PAGE_WRITE);
    }

    uint64_t entry = 0;
    int rc = elf_load(pml4, image, size, &entry);
    if (rc != ELF_LOAD_OK) {
        kprintf("userprog: %s: ELF load failed: %s\n", name, elf_error_string(rc));
        vmm_free_address_space(pml4);
        return NULL;
    }

    struct process* proc = process_create(name, NULL);
    if (!proc) {
        vmm_free_address_space(pml4);
        return NULL;
    }

    proc->is_user = true;
    proc->cr3 = (uint64_t)pml4;
    proc->user_rip = entry;
    proc->user_stack_top = USER_STACK_TOP;
    proc->user_heap_end = USER_HEAP_BASE;
    proc->fds[0] = NULL;
    proc->fds[1] = (void*)1;
    proc->fds[2] = (void*)2;
    proc->ppid = ppid;
    proc->tty_index = tty_index;
    proc->terminate_requested = false;

    /* Initial user stack: argv/envp block (System V entry layout) when
     * provided, else plain entry. The block is copied into the topmost
     * user stack frame (identity-mapped physical address). */
    uint64_t user_rsp = USER_STACK_TOP - 16;
    if (argv || envp) {
        static uint8_t blk[4096];
        uint64_t strings_off = 4096;
        uint64_t slots[64];
        uint32_t nslots = 0;
        uint32_t argc = 0, envc = 0;

        slots[nslots++] = 0; /* argc placeholder at block start */
        while (argv && argv[argc] && nslots < 40) {
            uint32_t l = strlen(argv[argc]) + 1;
            if (strings_off < l + 1) break;
            strings_off -= l;
            memcpy(blk + strings_off, argv[argc], l);
            slots[nslots++] = USER_STACK_TOP - 4096 + strings_off;
            argc++;
        }
            slots[nslots++] = 0; /* argv NULL */
        slots[0] = argc; /* fill the argc placeholder */
        while (envp && envp[envc] && nslots < 60) {
            uint32_t l = strlen(envp[envc]) + 1;
            if (strings_off < l + 1) break;
            strings_off -= l;
            memcpy(blk + strings_off, envp[envc], l);
            slots[nslots++] = USER_STACK_TOP - 4096 + strings_off;
            envc++;
        }
        slots[nslots++] = 0; /* envp NULL */

        uint64_t array_bytes = nslots * 8;
        if (strings_off < array_bytes + 16) {
            kprintf("userprog: %s: argv/envp block overflow\n", name);
            vmm_free_address_space(pml4);
            /* Already on the ready queue (process_create enqueued it):
             * park it as a parentless zombie so a later schedule pass
             * skips and reaps it. */
            proc->state = PROC_STATE_ZOMBIE;
            proc->ppid = 0;
            return NULL;
        }
        uint64_t array_off = strings_off - array_bytes;
        array_off &= ~15ULL;
        memcpy(blk + array_off, slots, array_bytes);

        memcpy((void*)top_frame, blk, 4096);
        user_rsp = USER_STACK_TOP - 4096 + array_off;
    }

    /* Initial kernel stack for the first ring-3 entry. After proc_switch
     * rets into user_first_entry, rsp points at the GP register block:
     *   [r15][r14][r13][r12][r11][r10][r9][r8][rbp][rdi][rsi][rdx][rcx]
     *   [rbx][rax][int_no][err][rip][cs][rflags][rsp][ss]
     * ascending. Six context words sit below it for proc_switch itself. */
    uint64_t sp = proc->kernel_stack_top;

    sp -= 8; *(uint64_t*)sp = USER_SS_SEL;
    sp -= 8; *(uint64_t*)sp = user_rsp;
    sp -= 8; *(uint64_t*)sp = USER_RFLAGS;
    sp -= 8; *(uint64_t*)sp = USER_CS_SEL;
    sp -= 8; *(uint64_t*)sp = proc->user_rip;
    sp -= 8; *(uint64_t*)sp = 0;
    sp -= 8; *(uint64_t*)sp = 0;

    sp -= 8; *(uint64_t*)sp = 0;                 /* rax */
    sp -= 8; *(uint64_t*)sp = 0;                 /* rbx */
    sp -= 8; *(uint64_t*)sp = 0;                 /* rcx */
    sp -= 8; *(uint64_t*)sp = 0;                 /* rdx */
    sp -= 8; *(uint64_t*)sp = 0;                 /* rsi */
    sp -= 8; *(uint64_t*)sp = 0;                 /* rdi */
    sp -= 8; *(uint64_t*)sp = 0;                 /* rbp */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r8  */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r9  */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r10 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r11 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r12 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r13 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r14 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* r15 */

    /* Context block: proc_switch pops r15..rbp then rets. Push order
     * (descending) matches: ret slot first (lowest), then rbp, rbx,
     * r12, r13, r14, r15 last (highest) — context.rsp points at r15. */
    sp -= 8; *(uint64_t*)sp = (uint64_t)user_first_entry;
    sp -= 8; *(uint64_t*)sp = 0;                 /* ctx rbp */
    sp -= 8; *(uint64_t*)sp = 0;                 /* ctx rbx */
    sp -= 8; *(uint64_t*)sp = 0;                 /* ctx r12 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* ctx r13 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* ctx r14 */
    sp -= 8; *(uint64_t*)sp = 0;                 /* ctx r15 */

    proc->context.rsp = sp;
    proc->context.rbp = 0;
    proc->context.rbx = 0;
    proc->context.r12 = 0;
    proc->context.r13 = 0;
    proc->context.r14 = 0;
    proc->context.r15 = 0;

    /* NOTE: process_create() already enqueues new PCBs; enqueueing here
     * too would double-link the same PCB and corrupt the ready queue. */
    return proc;
}

struct process* user_process_create(const char* name) {
    return spawn_from_image(name, user_program_start,
                            (uint64_t)(user_program_end - user_program_start),
                            NULL, NULL, 0, 0);
}

struct process* user_process_create_image(const char* name, const uint8_t* image, uint64_t size) {
    return spawn_from_image(name, image, size, NULL, NULL, 0, 0);
}

struct process* user_process_exec(const char* name, const uint8_t* image, uint64_t size,
                                  const char* const* argv, const char* const* envp,
                                  uint32_t ppid, uint8_t tty_index) {
    return spawn_from_image(name, image, size, argv, envp, ppid, tty_index);
}

/* Spawn the session shell for terminal N (called on first switch to an
 * empty session). argv carries the session index; env is minimal. */
struct process* user_shell_spawn(uint8_t tty_index) {
    static char idx_buf[2];
    idx_buf[0] = (char)('0' + tty_index);
    idx_buf[1] = '\0';
    const char* argv[3] = {"sh", idx_buf, NULL};
    const char* envp[2] = {"PATH=/BIN", NULL};
    return user_process_exec("sh", user_program_start,
                             (uint64_t)(user_program_end - user_program_start),
                             argv, envp, 0, tty_index);
}
