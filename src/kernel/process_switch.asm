; process_switch.asm - Context switching to user mode
section .text
bits 64

global _process_switch_to_user

; void _process_switch_to_user(uint64_t entry, uint64_t stack, uint64_t user_ds, uint64_t user_cs)
; Arguments: RDI = entry point, RSI = stack pointer, RDX = user DS, RCX = user CS
_process_switch_to_user:
    ; Disable interrupts during switch
    cli
    
    ; Save parameters
    mov r8, rdi     ; entry point
    mov r9, rsi     ; stack pointer
    mov r10, rdx    ; user DS
    mov r11, rcx    ; user CS
    
    ; Set up user data segments
    mov ax, r10w
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Build IRET stack frame
    ; Stack layout (from bottom to top):
    ; SS (user data segment)
    ; RSP (user stack pointer)
    ; RFLAGS (with interrupts enabled)
    ; CS (user code segment)
    ; RIP (entry point)
    
    push r10        ; SS (user data segment)
    push r9         ; RSP (user stack pointer)
    
    ; Push RFLAGS with IF=1 (interrupts enabled) and reserved bit 1 set
    pushfq
    pop rax
    or rax, 0x200   ; Set IF (interrupt enable flag)
    push rax        ; RFLAGS
    
    push r11        ; CS (user code segment)
    push r8         ; RIP (entry point)
    
    ; Clear all general purpose registers for security
    xor rax, rax
    xor rbx, rbx
    xor rcx, rcx
    xor rdx, rdx
    xor rsi, rsi
    xor rdi, rdi
    xor r8, r8
    xor r9, r9
    xor r10, r10
    xor r11, r11
    xor r12, r12
    xor r13, r13
    xor r14, r14
    xor r15, r15
    xor rbp, rbp
    
    ; IRET to user mode
    ; This will pop: RIP, CS, RFLAGS, RSP, SS
    ; And switch to ring 3
    iretq
    
    ; Should never reach here
    hlt