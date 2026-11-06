; NumOS — syscall_entry.asm — int 0x80 entry and first Ring 3 entry
;
; The CPU switches to TSS.RSP0 on entry from Ring 3 and pushes
; SS,RSP,RFLAGS,CS,RIP. The stub completes a registers_t frame
; (int_no, err, then rax..r15 pushed in isr_common_stub order), calls the
; C dispatcher, and returns via iretq. The dispatcher may switch tasks;
; resumption returns here through the saved context, so the frame survives
; on the process kernel stack.

[bits 64]
section .text

extern syscall_handler
extern process_exit
extern kernel_exec_depth

global syscall_entry
global user_first_entry

syscall_entry:
    inc qword [rel kernel_exec_depth]

    push qword 0x80
    push qword 0

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

    mov rdi, rsp
    cld
    call syscall_handler

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

    dec qword [rel kernel_exec_depth]
    add rsp, 16
    iretq

; First entry into Ring 3 for a freshly created user process. The crafted
; kernel stack (see userprog.c) holds, ascending: six context words
; consumed by proc_switch, a ret slot pointing here, then the register
; block and the iret frame. On entry rsp points at r15.
user_first_entry:
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

    add rsp, 16
    iretq
