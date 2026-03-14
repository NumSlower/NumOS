/*
 * pic.c - Programmable Interrupt Controller (8259A) driver
 *
 * Remaps IRQ 0-7  to IDT vectors 32-39 (master PIC)
 * Remaps IRQ 8-15 to IDT vectors 40-47 (slave PIC)
 *
 * Default x86 mapping (IRQ 0-7 -> vectors 8-15) conflicts with CPU
 * exception vectors and must be changed before enabling interrupts.
 *
 * ICW sequence (Initialization Command Words):
 *   ICW1: start initialization, cascade mode, ICW4 required
 *   ICW2: interrupt base vector for this PIC
 *   ICW3: master -> slave IRQ line (4); slave -> cascade identity (2)
 *   ICW4: 8086 mode (not MCS-80), not-auto-EOI
 *
 * After init all IRQs are masked.  Callers use pic_unmask_irq() to
 * enable individual lines.
 */

#include "drivers/pic.h"
#include "drivers/vga.h"

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * pic_init - reinitialise both PICs and remap vectors to 32-47.
 * All IRQs start masked except IRQ 2 (cascade) on the master.
 */
void pic_init(void) {
    /* ICW1: start initialization in cascade mode, ICW4 needed */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    /* ICW2: base interrupt vector
     *   Master: IRQ 0-7  -> vectors 32-39
     *   Slave:  IRQ 8-15 -> vectors 40-47 */
    outb(PIC1_DATA, 32);
    outb(PIC2_DATA, 40);

    /* ICW3: cascade wiring
     *   Master: slave connected at IRQ 2 (bit 2 = 0b00000100)
     *   Slave:  cascade identity = 2 */
    outb(PIC1_DATA, 4);
    outb(PIC2_DATA, 2);

    /* ICW4: 8086/88 mode, normal (non-auto) EOI */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    /* Mask all IRQs; callers must explicitly unmask what they need.
     * Keep IRQ 2 (cascade) unmasked on the master so the slave can fire. */
    outb(PIC1_DATA, 0xFB);   /* 1111 1011: all masked except IRQ 2 */
    outb(PIC2_DATA, 0xFF);   /* 1111 1111: all slave IRQs masked    */
}

/* =========================================================================
 * EOI
 * ======================================================================= */

/*
 * pic_send_eoi - acknowledge the end of an interrupt.
 * For slave IRQs (8-15) both PICs must receive an EOI command.
 */
void pic_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);  /* slave first */
    }
    outb(PIC1_COMMAND, PIC_EOI);      /* master always */
}

/* =========================================================================
 * IRQ masking
 * ======================================================================= */

/*
 * pic_mask_irq - set the mask bit for irq (0-15), disabling that IRQ.
 */
void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    outb(port, inb(port) | (uint8_t)(1 << irq));
}

/*
 * pic_unmask_irq - clear the mask bit for irq (0-15), enabling that IRQ.
 */
void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    outb(port, inb(port) & (uint8_t)~(1 << irq));
}

/*
 * pic_disable - mask all IRQs on both PICs.
 * Used before switching to APIC or when halting.
 */
void pic_disable(void) {
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}