; interrupt_handlers.asm - Complete ISR and IRQ handlers for x86-64
; Implements all 256 interrupt vectors with proper context switching

bits 64

; External C functions
extern exception_handler
extern irq_handler

; Export all ISR handlers (exceptions 0-31)
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21

; Export all IRQ handlers (IRQs 0-15)
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

section .text

;==============================================================================
; MACRO DEFINITIONS
;==============================================================================

; Macro for exceptions that don't push an error code
; We push a dummy error code (0) for stack consistency
%macro ISR_NOERRCODE 1
isr%1:
    cli                     ; Disable interrupts
    push qword 0            ; Push dummy error code
    push qword %1           ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for exceptions that push an error code automatically
; The CPU already pushed the error code, we just push the interrupt number
%macro ISR_ERRCODE 1
isr%1:
    cli                     ; Disable interrupts
    push qword %1           ; Push interrupt number
    jmp isr_common_stub
%endmacro

; Macro for IRQ handlers
%macro IRQ 2
irq%1:
    cli                     ; Disable interrupts
    push qword 0            ; Push dummy error code
    push qword %2           ; Push interrupt number (32 + IRQ number)
    jmp irq_common_stub
%endmacro

;==============================================================================
; EXCEPTION HANDLERS (0-31)
;==============================================================================

; CPU Exceptions without error code
ISR_NOERRCODE 0     ; Division By Zero
ISR_NOERRCODE 1     ; Debug
ISR_NOERRCODE 2     ; Non Maskable Interrupt
ISR_NOERRCODE 3     ; Breakpoint
ISR_NOERRCODE 4     ; Into Detected Overflow
ISR_NOERRCODE 5     ; Out of Bounds
ISR_NOERRCODE 6     ; Invalid Opcode
ISR_NOERRCODE 7     ; No Coprocessor

; CPU Exceptions with error code
ISR_ERRCODE   8     ; Double Fault
ISR_NOERRCODE 9     ; Coprocessor Segment Overrun
ISR_ERRCODE   10    ; Bad TSS
ISR_ERRCODE   11    ; Segment Not Present
ISR_ERRCODE   12    ; Stack Fault
ISR_ERRCODE   13    ; General Protection Fault
ISR_ERRCODE   14    ; Page Fault
ISR_NOERRCODE 15    ; Unknown Interrupt
ISR_NOERRCODE 16    ; Coprocessor Fault
ISR_ERRCODE   17    ; Alignment Check
ISR_NOERRCODE 18    ; Machine Check
ISR_NOERRCODE 19    ; SIMD Floating-Point Exception
ISR_NOERRCODE 20    ; Virtualization Exception
ISR_ERRCODE   21    ; Control Protection Exception

; Reserved exceptions
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_NOERRCODE 29
ISR_NOERRCODE 30
ISR_NOERRCODE 31

;==============================================================================
; IRQ HANDLERS (32-47)
;==============================================================================

IRQ 0,  32      ; System timer
IRQ 1,  33      ; Keyboard
IRQ 2,  34      ; Cascade (used internally by PICs)
IRQ 3,  35      ; COM2
IRQ 4,  36      ; COM1
IRQ 5,  37      ; LPT2
IRQ 6,  38      ; Floppy disk
IRQ 7,  39      ; LPT1
IRQ 8,  40      ; CMOS real-time clock
IRQ 9,  41      ; Free for peripherals
IRQ 10, 42      ; Free for peripherals
IRQ 11, 43      ; Free for peripherals
IRQ 12, 44      ; PS/2 mouse
IRQ 13, 45      ; FPU / Coprocessor / Inter-processor
IRQ 14, 46      ; Primary ATA hard disk
IRQ 15, 47      ; Secondary ATA hard disk

;==============================================================================
; COMMON ISR STUB
; This is called by all exception handlers
; Stack layout when this is called:
;   [SS]           (pushed by CPU if privilege change)
;   [RSP]          (pushed by CPU if privilege change)
;   [RFLAGS]       (pushed by CPU)
;   [CS]           (pushed by CPU)
;   [RIP]          (pushed by CPU)
;   [Error Code]   (pushed by CPU or us)
;   [Int Number]   (pushed by us)  <- RSP points here
;==============================================================================

isr_common_stub:
    ; Save all general purpose registers
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
    
    ; Save data segment selector
    mov ax, ds
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up parameters for exception_handler(int_no, err_code)
    ; x86-64 calling convention: RDI = 1st arg, RSI = 2nd arg
    ; Stack: 15 GPRs (120 bytes) + 1 DS (8 bytes) = 128 bytes
    mov rdi, [rsp + 128]    ; Interrupt number
    mov rsi, [rsp + 136]    ; Error code
    
    ; Call C exception handler
    cld                     ; C code expects direction flag cleared
    call exception_handler
    
    ; Restore data segment
    pop rax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Restore general purpose registers
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
    
    ; Clean up error code and interrupt number from stack
    add rsp, 16
    
    ; Enable interrupts and return
    sti
    iretq

;==============================================================================
; COMMON IRQ STUB
; This is called by all IRQ handlers
; Similar to ISR stub but calls irq_handler instead
;==============================================================================

irq_common_stub:
    ; Save all general purpose registers
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
    
    ; Save data segment selector
    mov ax, ds
    push rax
    
    ; Load kernel data segment
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Set up parameter for irq_handler(irq_no)
    ; IRQ number = interrupt number - 32
    mov rdi, [rsp + 128]    ; Get interrupt number
    sub rdi, 32             ; Convert to IRQ number (0-15)
    
    ; Call C IRQ handler
    cld
    call irq_handler
    
    ; Restore data segment
    pop rax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    
    ; Restore general purpose registers
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
    
    ; Clean up error code and interrupt number from stack
    add rsp, 16
    
    ; Enable interrupts and return
    sti
    iretq