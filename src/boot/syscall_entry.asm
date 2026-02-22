; =============================================================================
; src/boot/syscall_entry.asm
;
; x86-64 SYSCALL entry point.
;
; When userland executes the SYSCALL instruction:
;   • RCX  ← user RIP  (return address)
;   • R11  ← user RFLAGS
;   • RIP  ← LSTAR MSR value  (this label)
;   • CS   ← STAR[47:32]      (kernel code selector)
;   • CPL  ← 0
;
; Interrupts are disabled on entry (SFMASK MSR clears IF).
; We must:
;   1. Swap to the kernel syscall stack.
;   2. Save all user registers.
;   3. Call syscall_dispatch(struct syscall_regs *).
;   4. Restore user registers, put return value in rax.
;   5. SYSRETQ back to userland.
; =============================================================================

bits 64

global syscall_entry
extern syscall_dispatch

; Per-CPU kernel stack pointer for syscall entries.
; In a single-CPU kernel this is just one qword in .bss.
global syscall_kernel_stack_top

section .bss
align 16
syscall_kernel_stack_space:
    resb 16384          ; 16 KB kernel stack for syscall handling
syscall_kernel_stack_top:

section .text

syscall_entry:
    ; ---- swap user rsp for kernel rsp -------------------------
    ; We need a scratch register to do the swap.
    ; rcx holds the user RIP already (set by the cpu), so use r15
    ; as a temporary — we'll save it immediately after.
    ;
    ; Canonical pattern: xchg user rsp with kernel rsp stored in
    ; a well-known location.  Here we use a simple absolute addr
    ; because we have no per-CPU segment in this single-CPU kernel.

    ; Save user rsp in r15 temporarily
    mov     r15, rsp

    ; Switch to kernel syscall stack
    mov     rsp, [rel syscall_kernel_stack_top]

    ; Align stack to 16 bytes (System V ABI requirement for calls)
    and     rsp, -16

    ; ---- build struct syscall_regs on the kernel stack ---------
    ; Layout (low → high addresses, i.e. push order):
    ;   rsp, r15, r14, r13, r12, rbp, rbx, r11, rcx,
    ;   r9,  r8,  r10, rdx, rsi, rdi, rax
    ; (We push in reverse order so the struct fields match
    ;  the C declaration top-to-bottom.)

    push    r15         ; rsp  (user stack pointer, saved in r15 above)
    push    r14
    push    r13
    push    r12
    push    rbp
    push    rbx
    push    r11         ; user RFLAGS (saved by SYSCALL)
    push    rcx         ; user RIP   (saved by SYSCALL)
    push    r9
    push    r8
    push    r10
    push    rdx
    push    rsi
    push    rdi
    push    rax         ; syscall number

    ; ---- call C dispatcher -------------------------------------
    ; First argument (rdi) = pointer to struct syscall_regs
    mov     rdi, rsp
    call    syscall_dispatch

    ; Return value from C is in rax — that's exactly what we want
    ; to hand back to userland in rax.

    ; ---- restore registers from struct -------------------------
    ; Skip rax slot (we keep the return value in rax)
    add     rsp, 8      ; skip saved rax (syscall number)
    pop     rdi
    pop     rsi
    pop     rdx
    pop     r10
    pop     r8
    pop     r9
    pop     rcx         ; user RIP  → rcx (needed by SYSRETQ)
    pop     r11         ; user RFLAGS → r11 (needed by SYSRETQ)
    pop     rbx
    pop     rbp
    pop     r12
    pop     r13
    pop     r14
    pop     r15

    ; Restore user rsp (was the last thing pushed)
    ; It's sitting at [rsp] right now — pop into the actual rsp.
    ; But pop rsp isn't encodable directly; use a mov.
    mov     rsp, [rsp]  ; restore user rsp

    ; ---- return to userland ------------------------------------
    sysretq