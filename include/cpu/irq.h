/* NumOS — include/kernel/irq.h — IRQ dispatch header */
#ifndef NUMOS_KERNEL_IRQ_H
#define NUMOS_KERNEL_IRQ_H

#include "libs/types.h"
#include "cpu/isr.h"

typedef void (*irq_handler_t)(registers_t* regs);

void irq_init(void);
void irq_register_handler(uint8_t irq, irq_handler_t handler);
void irq_unregister_handler(uint8_t irq);
void irq_handler(registers_t* regs);

#endif
