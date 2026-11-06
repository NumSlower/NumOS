; NumOS — switch.asm — Context switch primitive
;
; void proc_switch(struct cpu_context* from, struct cpu_context* to)
;
; Saves the callee-saved register set plus the return address on the
; current kernel stack, stores the stack pointer in `from`, loads `to`'s
; saved stack pointer, and restores its saved state. A freshly created
; process has its stack crafted so the restore resumes at its entry
; trampoline, which calls process_exit() on return.

global proc_switch
global thread_trampoline

extern process_exit

thread_trampoline:
    call r12
    call process_exit
    hlt

proc_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, [rsi]

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret
