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
    resb 16384          ; 16 KB fallback kernel stack for syscall handling
syscall_kernel_stack_space_top:

align 8
syscall_kernel_stack_top:
    resq 1              ; uint8_t* set by scheduler before entering userland

align 8
syscall_user_rsp:
    resq 1              ; temporary storage for user RSP while switching stacks

section .text

syscall_entry:
    ; ---- switch to the kernel stack ----------------------------
    ; SYSCALL does not switch stacks. Save user RSP to memory
    ; before overwriting RSP with the kernel stack pointer.
    mov     [rel syscall_user_rsp], rsp

    ; If the scheduler hasn't set syscall_kernel_stack_top yet, fall back
    ; to the static kernel stack in this file.
    mov     rsp, [rel syscall_kernel_stack_top]
    test    rsp, rsp
    jnz     .have_kstack
    lea     rsp, [rel syscall_kernel_stack_space_top]
.have_kstack:

    ; Align stack to 16 bytes (System V ABI requirement for calls)
    and     rsp, -16

    ; ---- build struct syscall_regs on the kernel stack ----------
    ; struct syscall_regs (see Include/kernel/syscall.h):
    ;   rax, rdi, rsi, rdx, r10, r8, r9, rcx, r11,
    ;   rbx, rbp, r12, r13, r14, r15, rsp
    ;
    ; Push in reverse order so [rsp] points at regs->rax.
    push    qword [rel syscall_user_rsp]   ; rsp (user stack pointer)
    push    r15
    push    r14
    push    r13
    push    r12
    push    rbp
    push    rbx
    push    r11                            ; user RFLAGS (saved by SYSCALL)
    push    rcx                            ; user RIP    (saved by SYSCALL)
    push    r9
    push    r8
    push    r10
    push    rdx
    push    rsi
    push    rdi
    push    rax                            ; syscall number

    ; ---- call C dispatcher -------------------------------------
    ; First argument (rdi) = pointer to struct syscall_regs
    mov     rdi, rsp
    call    syscall_dispatch

    ; Return value from C is in rax — that's exactly what we want
    ; to hand back to userland in rax.

    ; ---- restore registers from struct -------------------------
    ; Skip regs->rax slot (keep return value in rax)
    add     rsp, 8
    pop     rdi
    pop     rsi
    pop     rdx
    pop     r10
    pop     r8
    pop     r9
    pop     rcx         ; user RIP     (needed by SYSRETQ)
    pop     r11         ; user RFLAGS  (needed by SYSRETQ)
    pop     rbx
    pop     rbp
    pop     r12
    pop     r13
    pop     r14
    pop     r15
    pop     rsp         ; user RSP

    ; ---- return to userland ------------------------------------
    ; NASM's plain `sysret` encodes SYSRETL (compat mode). Force SYSRETQ.
    o64 sysret
