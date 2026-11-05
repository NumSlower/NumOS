/* NumOS — panic.c — Kernel panic handler */
#include "kernel/panic.h"
#include "libs/kprintf.h"
#include "libs/io.h"

void dump_registers(void* regs_ptr) {
    if (!regs_ptr) return;
    registers_t* regs = (registers_t*)regs_ptr;
    kprintf("Registers:\n");
    kprintf("RAX: %p  RBX: %p  RCX: %p\n", regs->rax, regs->rbx, regs->rcx);
    kprintf("RDX: %p  RSI: %p  RDI: %p\n", regs->rdx, regs->rsi, regs->rdi);
    kprintf("RBP: %p  RSP: %p  R8 : %p\n", regs->rbp, regs->rsp, regs->r8);
    kprintf("R9 : %p  R10: %p  R11: %p\n", regs->r9, regs->r10, regs->r11);
    kprintf("R12: %p  R13: %p  R14: %p\n", regs->r12, regs->r13, regs->r14);
    kprintf("R15: %p  RIP: %p  RFL: %p\n", regs->r15, regs->rip, regs->rflags);
    kprintf("CS : %x  SS : %x  INT: %x  ERR: %x\n", regs->cs, regs->ss, regs->int_no, regs->err_code);
}

void stack_trace(void) {
    uint64_t* rbp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(rbp));
    kprintf("Stack Trace:\n");
    int frame = 0;
    while (rbp && frame < 20) {
        uint64_t rip = rbp[1];
        if (rip == 0) break;
        kprintf("  [%d] %p\n", frame++, rip);
        rbp = (uint64_t*)rbp[0];
    }
}

NORETURN void kpanic(const char* fmt, ...) {
    cli();
    kprintf("\n================================================================================\n");
    kprintf("KERNEL PANIC\n");
    kprintf("================================================================================\n");
    
    va_list args;
    va_start(args, fmt);
    // Print panic message directly using a buffer, or a kvprintf that outputs directly.
    char buf[512];
    kvsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    kprintf("%s\n\n", buf);
    
    stack_trace();
    
    kprintf("System Halted.\n");
    while (1) {
        hlt();
    }
}

NORETURN void kpanic_at(const char* file, int line, const char* fmt, ...) {
    cli();
    kprintf("\n================================================================================\n");
    kprintf("KERNEL PANIC at %s:%d\n", file, line);
    kprintf("================================================================================\n");
    
    va_list args;
    va_start(args, fmt);
    char buf[512];
    kvsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    kprintf("%s\n\n", buf);
    
    stack_trace();
    
    kprintf("System Halted.\n");
    while (1) {
        hlt();
    }
}
