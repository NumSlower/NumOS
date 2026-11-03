/* NumOS — src/kernel/nmi.c — NMI initialization and control */
#include "kernel/nmi.h"
#include "kernel/io.h"

void nmi_enable(void) {
    outb(0x70, inb(0x70) & 0x7F);
}

void nmi_disable(void) {
    outb(0x70, inb(0x70) | 0x80);
}

void nmi_init(void) {
    nmi_enable();
}
