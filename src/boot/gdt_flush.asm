; gdt_flush.asm - Load new GDT and update segment registers
; Properly handles 64-bit long mode segment loading

bits 64

global gdt_flush_asm

section .text

gdt_flush_asm:
    ; RDI contains pointer to GDT pointer structure
    ; Load the new GDT
    lgdt [rdi]
    
    ; Update data segment registers
    ; In 64-bit mode, these are mostly ignored but should still be set
    mov ax, 0x10        ; Kernel data segment selector (entry 2, 0x10 = 2 * 8)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Update code segment by doing a far return
    ; We need to push the new CS and the return address, then do a far return
    ; This is the proper way to reload CS in 64-bit mode
    
    ; Push kernel code segment selector (entry 1, 0x08 = 1 * 8)
    push 0x08
    
    ; Push return address (use LEA with RIP-relative addressing)
    lea rax, [rel .flush]
    push rax
    
    ; Far return - pops return address and CS
    retfq
    
.flush:
    ; Now all segments are updated
    ret