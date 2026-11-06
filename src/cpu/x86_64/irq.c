/* NumOS — src/kernel/irq.c — IRQ dispatch implementation */
#include "cpu/irq.h"
#include "cpu/idt.h"
#include "kernel/pic.h"

extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

static irq_handler_t irq_handlers[16] = {0};

void irq_init(void) {
    idt_set_gate(32, (uint64_t)irq0, 0x08, 0x8E, 0);
    idt_set_gate(33, (uint64_t)irq1, 0x08, 0x8E, 0);
    idt_set_gate(34, (uint64_t)irq2, 0x08, 0x8E, 0);
    idt_set_gate(35, (uint64_t)irq3, 0x08, 0x8E, 0);
    idt_set_gate(36, (uint64_t)irq4, 0x08, 0x8E, 0);
    idt_set_gate(37, (uint64_t)irq5, 0x08, 0x8E, 0);
    idt_set_gate(38, (uint64_t)irq6, 0x08, 0x8E, 0);
    idt_set_gate(39, (uint64_t)irq7, 0x08, 0x8E, 0);
    idt_set_gate(40, (uint64_t)irq8, 0x08, 0x8E, 0);
    idt_set_gate(41, (uint64_t)irq9, 0x08, 0x8E, 0);
    idt_set_gate(42, (uint64_t)irq10, 0x08, 0x8E, 0);
    idt_set_gate(43, (uint64_t)irq11, 0x08, 0x8E, 0);
    idt_set_gate(44, (uint64_t)irq12, 0x08, 0x8E, 0);
    idt_set_gate(45, (uint64_t)irq13, 0x08, 0x8E, 0);
    idt_set_gate(46, (uint64_t)irq14, 0x08, 0x8E, 0);
    idt_set_gate(47, (uint64_t)irq15, 0x08, 0x8E, 0);
}

void irq_register_handler(uint8_t irq, irq_handler_t handler) {
    if (irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_unregister_handler(uint8_t irq) {
    if (irq < 16) {
        irq_handlers[irq] = NULL;
    }
}

void irq_handler(registers_t* regs) {
    uint8_t irq = regs->int_no - 32;
    
    if (irq == 7) {
        if (!(pic_get_isr() & (1 << 7))) {
            return; /* Spurious IRQ7 */
        }
    } else if (irq == 15) {
        if (!(pic_get_isr() & (1 << 15))) {
            pic_send_eoi(7); /* Send EOI to master only */
            return; /* Spurious IRQ15 */
        }
    }

    /* EOI before dispatch: handlers may context-switch away (timer
     * preemption switches to another process and never returns through
     * this frame), so the EOI must not wait for the handler's return. */
    if (irq < 16 && irq_handlers[irq]) {
        pic_send_eoi(irq);
        irq_handlers[irq](regs);
    } else {
        pic_send_eoi(irq);
    }
}
