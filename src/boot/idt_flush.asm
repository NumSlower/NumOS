; idt_flush.asm - Load new IDT
; Simple and straightforward IDT loading

bits 64

global idt_flush_asm

section .text

idt_flush_asm:
    ; RDI contains pointer to IDT pointer structure
    ; Load the new IDT
    lidt [rdi]
    ret