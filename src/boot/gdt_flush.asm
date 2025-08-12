; GDT Flush Assembly Code
; This code loads the new GDT and updates segment registers

bits 64

global gdt_flush_asm

section .text

gdt_flush_asm:
    ; Load the new GDT
    lgdt [rdi]          ; rdi contains pointer to GDT pointer structure
    
    ; Update data segment registers
    mov ax, 0x10        ; Kernel data segment selector (entry 2)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Update code segment by doing a far jump
    ; We need to push the new CS and the return address, then do a far return
    push 0x08           ; Kernel code segment selector (entry 1)
    lea rax, [rel .flush]
    push rax
    retfq               ; Far return - loads CS with new selector
    
.flush:
    ret