#include "kernel/kernel.h"
#include "drivers/graphices/vga.h"

typedef void (*init_func_t)(void);

extern init_func_t __init_array_start[];
extern init_func_t __init_array_end[];

uintptr_t __stack_chk_guard = 0;

static uintptr_t __attribute__((no_stack_protector))
runtime_seed_guard(void) {
    uint32_t lo = 0;
    uint32_t hi = 0;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));

    uintptr_t seed = ((uintptr_t)hi << 32) | (uintptr_t)lo;
    seed ^= (uintptr_t)&seed;
    seed ^= (uintptr_t)__init_array_start;
    seed ^= 0x9E3779B97F4A7C15ULL;

    if (seed == 0) seed = 0xA5A55A5AF0F00F0FULL;
    return seed;
}

void __attribute__((no_stack_protector)) runtime_init(void) {
    static int runtime_ready = 0;
    if (runtime_ready) return;

    __stack_chk_guard = runtime_seed_guard();

    for (init_func_t *fn = __init_array_start; fn < __init_array_end; fn++) {
        if (*fn) (*fn)();
    }

    runtime_ready = 1;
}

void __attribute__((no_stack_protector)) __stack_chk_fail(void) {
    uintptr_t ret_addr = (uintptr_t)__builtin_return_address(0);
    uintptr_t rsp = 0;
    __asm__ volatile("mov %%rsp, %0" : "=r"(rsp));

    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\n===== STACK SMASH DETECTED =====\n");
    vga_writestring("Compiler stack protector trapped a corrupted return path.\n");
    vga_writestring("Return address: ");
    print_hex(ret_addr);
    vga_writestring("\nStack pointer : ");
    print_hex(rsp);
    vga_writestring("\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    panic("stack protector failure");
}

void __stack_chk_fail_local(void) __attribute__((alias("__stack_chk_fail")));
