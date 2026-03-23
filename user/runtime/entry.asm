; user/runtime/entry.asm - NumOS user entry for C programs

bits 64
global _start
extern main
extern __numos_user_runtime_init
extern __numos_user_run_constructors
extern __numos_user_prepare_args
extern __numos_user_argc
extern __numos_user_argv

section .text
_start:
    xor rbp, rbp
    call __numos_user_runtime_init
    call __numos_user_run_constructors
    call __numos_user_prepare_args
    mov edi, dword [rel __numos_user_argc]
    mov rsi, qword [rel __numos_user_argv]
    call main
    mov rdi, rax
    mov rax, 60
    syscall
.hang:
    hlt
    jmp .hang
