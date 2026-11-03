/* NumOS — include/kernel/pic.h — 8259 PIC header */
#ifndef NUMOS_KERNEL_PIC_H
#define NUMOS_KERNEL_PIC_H

#include "kernel/types.h"

void pic_init(void);
void pic_send_eoi(uint8_t irq);
void pic_disable(void);
void pic_set_mask(uint8_t irq);
void pic_clear_mask(uint8_t irq);
uint16_t pic_get_isr(void);

#endif
