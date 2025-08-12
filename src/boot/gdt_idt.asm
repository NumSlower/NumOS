; GDT and IDT Assembly Support for NumOS
; 64-bit assembly functions

bits 64

global gdt_flush
global tss_flush
global idt_flush
global paging_load_directory
global paging_read_cr3
global paging_invalidate_page
global paging_flush_tlb

; ISR handlers
global isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
global isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
global isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
global isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31

; IRQ handlers
global irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7
global irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15

extern interrupt_handler
extern irq_handler

section .text

; Load GDT
gdt_flush:
    lgdt [rdi]          ; Load GDT pointer
    mov ax, 0x10        ; Data segment selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    
    ; Far jump to reload CS
    push 0x08           ; Code segment selector
    push .reload_cs
    retfq
.reload_cs:
    ret

; Load TSS
tss_flush:
    mov ax, 0x2B        ; TSS segment selector
    ltr ax
    ret

; Load IDT
idt_flush:
    lidt [rdi]          ; Load IDT pointer
    ret

; Paging functions
paging_load_directory:
    mov cr3, rdi        ; Load page directory
    ret

paging_read_cr3:
    mov rax, cr3        ; Read current page directory
    ret

paging_invalidate_page:
    invlpg [rdi]        ; Invalidate single page
    ret

paging_flush_tlb:
    mov rax, cr3
    mov cr3, rax        ; Flush entire TLB
    ret

; Common ISR stub
isr_common_stub:
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
    
    ; Call interrupt handler
    mov rdi, rsp        ; Pass register state
    call interrupt_handler
    
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
    
    ; Remove interrupt number and error code
    add rsp, 16
    iretq

; Common IRQ stub
irq_common_stub:
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
    
    ; Call IRQ handler
    mov rdi, rsp        ; Pass register state
    call irq_handler
    
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
    
    ; Remove interrupt number and error code
    add rsp, 16
    iretq

; ISR handlers (0-31)
isr0:
    push 0              ; Dummy error code
    push 0              ; Interrupt number
    jmp isr_common_stub

isr1:
    push 0
    push 1
    jmp isr_common_stub

isr2:
    push 0
    push 2
    jmp isr_common_stub

isr3:
    push 0
    push 3
    jmp isr_common_stub

isr4:
    push 0
    push 4
    jmp isr_common_stub

isr5:
    push 0
    push 5
    jmp isr_common_stub

isr6:
    push 0
    push 6
    jmp isr_common_stub

isr7:
    push 0
    push 7
    jmp isr_common_stub

isr8:
    ; Double fault pushes error code automatically
    push 8
    jmp isr_common_stub

isr9:
    push 0
    push 9
    jmp isr_common_stub

isr10:
    ; Bad TSS pushes error code automatically
    push 10
    jmp isr_common_stub

isr11:
    ; Segment not present pushes error code automatically
    push 11
    jmp isr_common_stub

isr12:
    ; Stack fault pushes error code automatically
    push 12
    jmp isr_common_stub

isr13:
    ; General protection fault pushes error code automatically
    push 13
    jmp isr_common_stub

isr14:
    ; Page fault pushes error code automatically
    push 14
    jmp isr_common_stub

isr15:
    push 0
    push 15
    jmp isr_common_stub

isr16:
    push 0
    push 16
    jmp isr_common_stub

isr17:
    ; Alignment check pushes error code automatically
    push 17
    jmp isr_common_stub

isr18:
    push 0
    push 18
    jmp isr_common_stub

isr19:
    push 0
    push 19
    jmp isr_common_stub

isr20:
    push 0
    push 20
    jmp isr_common_stub

isr21:
    ; Control protection exception pushes error code automatically
    push 21
    jmp isr_common_stub

isr22:
    push 0
    push 22
    jmp isr_common_stub

isr23:
    push 0
    push 23
    jmp isr_common_stub

isr24:
    push 0
    push 24
    jmp isr_common_stub

isr25:
    push 0
    push 25
    jmp isr_common_stub

isr26:
    push 0
    push 26
    jmp isr_common_stub

isr27:
    push 0
    push 27
    jmp isr_common_stub

isr28:
    push 0
    push 28
    jmp isr_common_stub

isr29:
    push 0
    push 29
    jmp isr_common_stub

isr30:
    ; Security exception pushes error code automatically
    push 30
    jmp isr_common_stub

isr31:
    push 0
    push 31
    jmp isr_common_stub

; IRQ handlers (32-47)
irq0:
    push 0
    push 32
    jmp irq_common_stub

irq1:
    push 0
    push 33
    jmp irq_common_stub

irq2:
    push 0
    push 34
    jmp irq_common_stub

irq3:
    push 0
    push 35
    jmp irq_common_stub

irq4:
    push 0
    push 36
    jmp irq_common_stub

irq5:
    push 0
    push 37
    jmp irq_common_stub

irq6:
    push 0
    push 38
    jmp irq_common_stub

irq7:
    push 0
    push 39
    jmp irq_common_stub

irq8:
    push 0
    push 40
    jmp irq_common_stub

irq9:
    push 0
    push 41
    jmp irq_common_stub

irq10:
    push 0
    push 42
    jmp irq_common_stub

irq11:
    push 0
    push 43
    jmp irq_common_stub

irq12:
    push 0
    push 44
    jmp irq_common_stub

irq13:
    push 0
    push 45
    jmp irq_common_stub

irq14:
    push 0
    push 46
    jmp irq_common_stub

irq15:
    push 0
    push 47
    jmp irq_common_stub