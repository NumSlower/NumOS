#include "drivers/pic.h"
#include "drivers/vga.h"

void pic_init(void) {
    /* Start initialization sequence (ICW1) */
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    
    /* ICW2: Master PIC vector offset (IRQ0-7 mapped to interrupts 32-39) */
    outb(PIC1_DATA, 32);
    /* ICW2: Slave PIC vector offset (IRQ8-15 mapped to interrupts 40-47) */
    outb(PIC2_DATA, 40);
    
    /* ICW3: Tell Master PIC there's a slave PIC at IRQ2 (0000 0100) */
    outb(PIC1_DATA, 4);
    /* ICW3: Tell Slave PIC its cascade identity (0000 0010) */
    outb(PIC2_DATA, 2);
    
    /* ICW4: 8086/88 (MCS-80/85) mode */
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);
    
    /* Mask all interrupts except cascade (initially) */
    outb(PIC1_DATA, 0xFB); /* Mask all except IRQ2 (cascade) */
    outb(PIC2_DATA, 0xFF); /* Mask all slave IRQs */
}

void pic_send_eoi(uint8_t irq) {
    /* If this was a slave IRQ (8-15), send EOI to slave controller too */
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    
    /* Send EOI to master PIC */
    outb(PIC1_COMMAND, PIC_EOI);
}

void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) | (1 << irq);
    outb(port, value);
}

void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;
    
    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void pic_disable(void) {
    /* Mask all interrupts on both PICs */
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);
}