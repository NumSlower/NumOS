; =============================================================================
; src/boot/context_switch.asm
;
; void context_switch(struct cpu_context **old_ctx,
;                     struct cpu_context  *new_ctx);
;
; Calling convention (System V x86-64):
;   rdi = pointer to old process's context pointer  (struct cpu_context **)
;   rsi = new  process's cpu_context pointer        (struct cpu_context  *)
;
; What this does:
;   1. Push all callee-saved registers + the implicit return address onto the
;      CURRENT (old) kernel stack, forming a struct cpu_context in place.
;   2. Store rsp (pointing at the freshly-built context) into *old_ctx.
;   3. Load rsp from new_ctx  (new process's saved rsp).
;   4. Pop the new process's callee-saved registers + ret → resume new proc.
;
; The struct cpu_context layout (from scheduler.h, low→high):
;   r15, r14, r13, r12, rbx, rbp, rip  (7 × 8 = 56 bytes)
;
; rip is naturally provided by the `call` instruction pushing a return
; address, so we only need to push the six callee-saved GPRs.
; =============================================================================

bits 64

global context_switch

section .text

context_switch:
    ; ---- Save old context -----------------------------------------------
    ; Push callee-saved registers (in the order matching struct cpu_context).
    ; The call instruction already pushed the return address (rip field).
    push    r15
    push    r14
    push    r13
    push    r12
    push    rbx
    push    rbp

    ; *old_ctx = rsp  (save the stack pointer into the old PCB's context ptr)
    mov     [rdi], rsp

    ; ---- Load new context -----------------------------------------------
    mov     rsp, rsi        ; switch to new process's kernel stack

    ; Pop callee-saved registers from the new process's context frame.
    pop     rbp
    pop     rbx
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    ; ret pops the saved rip (return address) and jumps there.
    ; For a brand-new process this is process_trampoline (set in
    ; process_create_kernel / process_create_user); for an existing process
    ; it is wherever context_switch was called from.
    ret