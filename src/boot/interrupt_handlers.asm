; Interrupt Handlers Assembly Code
; ISRs (Interrupt Service Routines) for exceptions and IRQs

bits 64

extern exception_handler
extern irq_handler

; Exception handlers (ISRs 0-21)
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21

; IRQ handlers (IRQs 0-15)
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

section .text

; Macro for ISRs that don't push error codes
%macro ISR_NO_ERROR 1
isr%1:
    push 0          ; Push dummy error code
    push %1         ; Push interrupt number
    jmp isr_common
%endmacro

; Macro for ISRs that push error codes
%macro ISR_ERROR 1
isr%1:
    push %1         ; Push interrupt number
    jmp isr_common
%endmacro

; Macro for IRQ handlers
%macro IRQ 2
irq%1:
    push 0          ; Push dummy error code
    push %2         ; Push IRQ number
    jmp irq_common
%endmacro

; Exception handlers
ISR_NO_ERROR 0   ; Division by zero
ISR_NO_ERROR 1   ; Debug
ISR_NO_ERROR 2   ; NMI
ISR_NO_ERROR 3   ; Breakpoint
ISR_NO_ERROR 4   ; Overflow
ISR_NO_ERROR 5   ; Bound range exceeded
ISR_NO_ERROR 6   ; Invalid opcode
ISR_NO_ERROR 7   ; Device not available
ISR_ERROR    8   ; Double fault
ISR_NO_ERROR 9   ; Coprocessor segment overrun
ISR_ERROR    10  ; Invalid TSS
ISR_ERROR    11  ; Segment not present
ISR_ERROR    12  ; Stack segment fault
ISR_ERROR    13  ; General protection fault
ISR_ERROR    14  ; Page fault (has error code)
ISR_NO_ERROR 15  ; Reserved
ISR_NO_ERROR 16  ; x87 FPU error
ISR_ERROR    17  ; Alignment check
ISR_NO_ERROR 18  ; Machine check
ISR_NO_ERROR 19  ; SIMD floating point
ISR_NO_ERROR 20  ; Virtualization
ISR_ERROR    21  ; Control protection

; IRQ handlers
IRQ 0,  0   ; Timer
IRQ 1,  1   ; Keyboard
IRQ 2,  2   ; Cascade
IRQ 3,  3   ; COM2
IRQ 4,  4   ; COM1
IRQ 5,  5   ; LPT2
IRQ 6,  6   ; Floppy
IRQ 7,  7   ; LPT1
IRQ 8,  8   ; RTC
IRQ 9,  9   ; Free
IRQ 10, 10  ; Free
IRQ 11, 11  ; Free
IRQ 12, 12  ; Mouse
IRQ 13, 13  ; FPU
IRQ 14, 14  ; Primary ATA
IRQ 15, 15  ; Secondary ATA

; Common ISR handler
isr_common:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Save segment registers
    mov ax, ds
    push rax
    mov ax, es
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call exception handler
    ; rdi = exception number, rsi = error code
    mov rdi, [rsp + 160]  ; Exception number (adjusted for saved registers)
    mov rsi, [rsp + 168]  ; Error code
    call exception_handler
    
    ; Restore segment registers
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up stack (remove error code and interrupt number)
    add rsp, 16
    
    ; Return from interrupt
    iretq

; Common IRQ handler
irq_common:
    ; Save all registers
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15
    
    ; Save segment registers
    mov ax, ds
    push rax
    mov ax, es
    push rax
    mov ax, fs
    push rax
    mov ax, gs
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Call IRQ handler
    ; rdi = IRQ number
    mov rdi, [rsp + 160]  ; IRQ number (adjusted for saved registers)
    call irq_handler
    
    ; Restore segment registers
    pop rax
    mov gs, ax
    pop rax
    mov fs, ax
    pop rax
    mov es, ax
    pop rax
    mov ds, ax
    
    ; Restore all registers
    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax
    
    ; Clean up stack (remove error code and IRQ number)
    add rsp, 16
    
    ; Return from interrupt
    iretq