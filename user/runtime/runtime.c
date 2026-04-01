/*
 * runtime.c - Startup support shared by all NumOS user programs.
 *
 * The runtime seeds stack protection, runs constructors, and converts the
 * kernel command line into the argc/argv view used by user code.
 */

#include "libc.h"

typedef void (*init_func_t)(void);

extern init_func_t __init_array_start[];
extern init_func_t __init_array_end[];

uintptr_t __stack_chk_guard = 0;
int __numos_user_argc = 0;
char **__numos_user_argv = 0;

static char __numos_user_cmdline[256];
static char *__numos_user_argv_storage[32];

/* Mix uptime and code addresses so each process gets a non-zero guard value. */
static uintptr_t __attribute__((no_stack_protector))
user_seed_guard(void) {
    uintptr_t seed = (uintptr_t)sys_uptime_ms();
    seed ^= (uintptr_t)&seed;
    seed ^= (uintptr_t)__init_array_start;
    seed ^= 0xD6E8FEB86659FD93ULL;
    if (seed == 0) seed = 0x13579BDF2468ACE0ULL;
    return seed;
}

/* Initialize one-time runtime state before libc helpers are used. */
void __attribute__((no_stack_protector)) __numos_user_runtime_init(void) {
    static int ready = 0;
    if (ready) return;
    __stack_chk_guard = user_seed_guard();
    ready = 1;
}

/* Walk the constructor array emitted by the linker for static initializers. */
void __numos_user_run_constructors(void) {
    for (init_func_t *fn = __init_array_start; fn < __init_array_end; fn++) {
        if (*fn) (*fn)();
    }
}

/* Tokenize the kernel-provided command line into a small argv array. */
void __numos_user_prepare_args(void) {
    size_t argc = 0;
    __numos_user_argv_storage[argc++] = (char *)"program";

    __numos_user_cmdline[0] = '\0';
    sys_get_cmdline(__numos_user_cmdline, sizeof(__numos_user_cmdline));

    char *p = __numos_user_cmdline;
    while (*p && argc + 1 < (sizeof(__numos_user_argv_storage) / sizeof(__numos_user_argv_storage[0]))) {
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        __numos_user_argv_storage[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p == '\0') break;
        *p++ = '\0';
    }

    __numos_user_argv_storage[argc] = 0;
    __numos_user_argc = (int)argc;
    __numos_user_argv = __numos_user_argv_storage;
}

/* Report stack corruption to stderr before terminating the process. */
void __attribute__((no_stack_protector)) __stack_chk_fail(void) {
    const char *msg = "numos: stack protector trapped corruption\n";
    sys_write(FD_STDERR, msg, strlen(msg));
    sys_exit(127);
    for (;;) {
        __asm__ volatile("hlt");
    }
}

void __stack_chk_fail_local(void) __attribute__((alias("__stack_chk_fail")));

/* Bridge a kernel-created user thread into the public thread entry contract. */
void __attribute__((noreturn))
__numos_user_thread_entry(numos_thread_start_t start, void *arg) {
    intptr_t rc = 0;
    if (start) rc = start(arg);
    thread_exit(rc);
    for (;;) {
        __asm__ volatile("hlt");
    }
}
