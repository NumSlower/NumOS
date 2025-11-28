; syscall_entry.asm - System call entry point for INT 0x80
; System calls follow this calling convention:
;   rax = syscall number
;   rdi = arg1
;   rsi = arg2
;   rdx = arg3
;   r10 = arg4
;   r8  = arg5
;   Return value in rax

section .text
global syscall_entry_asm
extern syscall_handler

syscall_entry_asm:
    ; Save all registers that might be clobbered
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    
    ; Arguments are already in the correct registers for C calling convention
    ; rax = syscall number -> rdi (first arg)
    ; rdi = arg1 -> rsi (second arg)
    ; rsi = arg2 -> rdx (third arg)
    ; rdx = arg3 -> rcx (fourth arg)
    ; r10 = arg4 -> r8 (fifth arg)
    ; r8  = arg5 -> r9 (sixth arg)
    
    ; Set up arguments for syscall_handler
    mov r9, r8          ; arg5
    mov r8, r10         ; arg4
    mov rcx, rdx        ; arg3
    mov rdx, rsi        ; arg2
    mov rsi, rdi        ; arg1
    mov rdi, rax        ; syscall number
    
    ; Call the C handler
    call syscall_handler
    
    ; Return value is already in rax
    
    ; Restore registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    
    ; Return from interrupt
    iretq