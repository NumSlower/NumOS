; NumOS 64-bit Entry Point
; This is where we transition from assembly to C code

bits 64

global long_mode_start
extern kernel_main

section .text
long_mode_start:
    ; Clear segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up stack
    mov rsp, stack_top
    
    ; Call kernel main function
    call kernel_main
    
    ; Halt if kernel returns
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 4096 * 4
stack_top: