; NumOS 64-bit Entry Point - Fixed version
; This is where we transition from assembly to C code

bits 64

global long_mode_start
extern kernel_main
extern runtime_init
extern mb2_info_ptr

section .text
long_mode_start:
    ; Clear segment registers
    xor ax, ax
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up stack (use our own stack in this file)
    mov rsp, stack_space + 16384
    
    ; Ensure stack is 16-byte aligned (required by System V ABI)
    and rsp, -16
    
    ; Clear the direction flag (C code expects this)
    cld

    ; Seed the global stack protector guard before entering protected C code.
    call runtime_init

    ; Restore the original multiboot2 info pointer for kernel_main().
    mov rdi, [rel mb2_info_ptr]
    
    ; Call kernel main function
    call kernel_main
    
    ; If kernel_main returns, halt
    cli
.hang:
    hlt
    jmp .hang

; Stack space for kernel
section .bss
align 16
stack_space:
    resb 16384      ; 16KB stack
