; NumOS — src/boot/idt_flush.asm — IDT flush routine

[bits 64]
section .text
global idt_load

idt_load:
    lidt [rdi]
    ret
