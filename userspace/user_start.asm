section .text.start
global _start
extern shell_main

_start:
    cld
    call shell_main
    
    ; Exit syscall
    mov rax, 4
    xor rdi, rdi
    int 0x80
    
.hang:
    hlt
    jmp .hang