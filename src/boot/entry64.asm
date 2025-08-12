; NumOS 64-bit Entry Point
bits 64

global long_mode_start
extern kernel_main

section .text
long_mode_start:
    ; Debug: Print 'E' for Entry64
    mov word [0xb800e], 0x0f45
    
    ; Clear segment registers
    mov ax, 0
    mov ss, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Debug: Print 'K' for Kernel call
    mov word [0xb8010], 0x0f4b
    
    ; Set up proper 64-bit stack
    mov rsp, stack_top
    
    ; Align stack to 16 bytes (required for x86_64 ABI)
    and rsp, -16
    
    ; Call kernel main function
    call kernel_main
    
    ; Debug: Print 'H' for Halt
    mov word [0xb8012], 0x0f48
    
    ; Halt if kernel returns
    cli
.hang:
    hlt
    jmp .hang

section .bss
align 16
stack_bottom:
    resb 16384  ; 16KB stack
stack_top: