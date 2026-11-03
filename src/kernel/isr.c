/* NumOS — src/kernel/isr.c — Exception handling */
#include "kernel/isr.h"
#include "kernel/idt.h"
#include "kernel/kprintf.h"
#include "kernel/io.h"

extern void isr0(void);
extern void isr1(void);
extern void isr2(void);
extern void isr3(void);
extern void isr4(void);
extern void isr5(void);
extern void isr6(void);
extern void isr7(void);
extern void isr8(void);
extern void isr9(void);
extern void isr10(void);
extern void isr11(void);
extern void isr12(void);
extern void isr13(void);
extern void isr14(void);
extern void isr15(void);
extern void isr16(void);
extern void isr17(void);
extern void isr18(void);
extern void isr19(void);
extern void isr20(void);
extern void isr21(void);
extern void isr22(void);
extern void isr23(void);
extern void isr24(void);
extern void isr25(void);
extern void isr26(void);
extern void isr27(void);
extern void isr28(void);
extern void isr29(void);
extern void isr30(void);
extern void isr31(void);

static const char* exception_names[32] = {
    "Divide by Zero", "Debug", "NMI", "Breakpoint",
    "Overflow", "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS", "Segment Not Present",
    "Stack-Segment Fault", "General Protection Fault", "Page Fault", "Reserved",
    "x87 Floating-Point Exception", "Alignment Check", "Machine Check", "SIMD Floating-Point Exception",
    "Virtualization Exception", "Control Protection Exception", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Hypervisor Injection Exception", "VMM Communication Exception", "Security Exception", "Reserved"
};

void exceptions_init(void) {
    idt_set_gate(0, (uint64_t)isr0, 0x08, 0x8E, 0);
    idt_set_gate(1, (uint64_t)isr1, 0x08, 0x8E, 0);
    idt_set_gate(2, (uint64_t)isr2, 0x08, 0x8E, 1);
    idt_set_gate(3, (uint64_t)isr3, 0x08, 0x8E, 0);
    idt_set_gate(4, (uint64_t)isr4, 0x08, 0x8E, 0);
    idt_set_gate(5, (uint64_t)isr5, 0x08, 0x8E, 0);
    idt_set_gate(6, (uint64_t)isr6, 0x08, 0x8E, 0);
    idt_set_gate(7, (uint64_t)isr7, 0x08, 0x8E, 0);
    idt_set_gate(8, (uint64_t)isr8, 0x08, 0x8E, 2);
    idt_set_gate(9, (uint64_t)isr9, 0x08, 0x8E, 0);
    idt_set_gate(10, (uint64_t)isr10, 0x08, 0x8E, 0);
    idt_set_gate(11, (uint64_t)isr11, 0x08, 0x8E, 0);
    idt_set_gate(12, (uint64_t)isr12, 0x08, 0x8E, 0);
    idt_set_gate(13, (uint64_t)isr13, 0x08, 0x8E, 0);
    idt_set_gate(14, (uint64_t)isr14, 0x08, 0x8E, 0);
    idt_set_gate(15, (uint64_t)isr15, 0x08, 0x8E, 0);
    idt_set_gate(16, (uint64_t)isr16, 0x08, 0x8E, 0);
    idt_set_gate(17, (uint64_t)isr17, 0x08, 0x8E, 0);
    idt_set_gate(18, (uint64_t)isr18, 0x08, 0x8E, 3);
    idt_set_gate(19, (uint64_t)isr19, 0x08, 0x8E, 0);
    idt_set_gate(20, (uint64_t)isr20, 0x08, 0x8E, 0);
    idt_set_gate(21, (uint64_t)isr21, 0x08, 0x8E, 0);
    idt_set_gate(22, (uint64_t)isr22, 0x08, 0x8E, 0);
    idt_set_gate(23, (uint64_t)isr23, 0x08, 0x8E, 0);
    idt_set_gate(24, (uint64_t)isr24, 0x08, 0x8E, 0);
    idt_set_gate(25, (uint64_t)isr25, 0x08, 0x8E, 0);
    idt_set_gate(26, (uint64_t)isr26, 0x08, 0x8E, 0);
    idt_set_gate(27, (uint64_t)isr27, 0x08, 0x8E, 0);
    idt_set_gate(28, (uint64_t)isr28, 0x08, 0x8E, 0);
    idt_set_gate(29, (uint64_t)isr29, 0x08, 0x8E, 0);
    idt_set_gate(30, (uint64_t)isr30, 0x08, 0x8E, 0);
    idt_set_gate(31, (uint64_t)isr31, 0x08, 0x8E, 0);
}

void isr_handler(registers_t* regs) {
    kprintf("\n!!! CPU EXCEPTION: %s (vector %lu, error 0x%lx)\n",
            exception_names[regs->int_no], regs->int_no, regs->err_code);
    kprintf("  RIP: 0x%016lx  CS:  0x%04lx  RFLAGS: 0x%016lx\n",
            regs->rip, regs->cs, regs->rflags);
    kprintf("  RSP: 0x%016lx  SS:  0x%04lx\n", regs->rsp, regs->ss);
    kprintf("  RAX: 0x%016lx  RBX: 0x%016lx  RCX: 0x%016lx\n",
            regs->rax, regs->rbx, regs->rcx);
    kprintf("  RDX: 0x%016lx  RSI: 0x%016lx  RDI: 0x%016lx\n",
            regs->rdx, regs->rsi, regs->rdi);
    kprintf("  RBP: 0x%016lx  R8:  0x%016lx  R9:  0x%016lx\n",
            regs->rbp, regs->r8, regs->r9);
    kprintf("  R10: 0x%016lx  R11: 0x%016lx  R12: 0x%016lx\n",
            regs->r10, regs->r11, regs->r12);
    kprintf("  R13: 0x%016lx  R14: 0x%016lx  R15: 0x%016lx\n",
            regs->r13, regs->r14, regs->r15);

    if (regs->int_no == 14) {
        uint64_t cr2 = read_cr2();
        kprintf("  CR2 (fault addr): 0x%016lx\n", cr2);
        kprintf("  Page Fault: %s %s %s%s%s\n",
                (regs->err_code & 1) ? "protection" : "not-present",
                (regs->err_code & 2) ? "write" : "read",
                (regs->err_code & 4) ? "user " : "kernel ",
                (regs->err_code & 8) ? "reserved-bit " : "",
                (regs->err_code & 16) ? "instruction-fetch" : "");
    }

    kprintf("System halted.\n");
    for (;;) {
        __asm__ volatile("cli; hlt");
    }
}
