; IDT Flush Assembly Code
; This code loads the new IDT

bits 64

global idt_flush_asm

section .text

idt_flush_asm:
    ; Load the new IDT
    lidt [rdi]          ; rdi contains pointer to IDT pointer structure
    ret