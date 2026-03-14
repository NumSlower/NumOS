; =============================================================================
; user/elftest.asm  –  NumOS userland shell prompt
; All buffers on the stack to avoid BSS page-fault issues.
; =============================================================================

bits 64

global _start

; ---------------------------------------------------------------------------
section .rodata

; 50 newlines guarantees the screen is visually clear regardless of
; how many lines the kernel boot sequence printed before us.
msg_cls:       times 50 db 0x0A
msg_cls_len    equ 50

msg_banner:    db  "NumOS Userspace v1.0.0-beta", 0x0A, 0x0A
msg_banner_len equ $ - msg_banner

msg_prompt:    db  "> "
msg_prompt_len equ 2

msg_newline:   db  0x0A
msg_nl_len     equ 1

msg_bs:        db  0x08, 0x20, 0x08
msg_bs_len     equ 3

msg_cls2:      times 50 db 0x0A
msg_cls2_len   equ 50

msg_unknown:   db  "Unknown command.", 0x0A
msg_unknown_len equ $ - msg_unknown

cmd_clear:     db  "clear"
cmd_clear_len  equ 5

; ---------------------------------------------------------------------------
section .text

; ---------------------------------------------------------------------------
; _start
; ---------------------------------------------------------------------------
_start:
    ; Allocate 256-byte line buffer on the stack
    sub     rsp, 256

    ; Clear screen (50 newlines scrolls all kernel boot text off screen)
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_cls]
    mov     rdx, msg_cls_len
    syscall

    ; Print banner
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_banner]
    mov     rdx, msg_banner_len
    syscall

.prompt_loop:
    ; Print "> "
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_prompt]
    mov     rdx, msg_prompt_len
    syscall

    ; Read line into stack buffer, length returned in rbx
    mov     rdi, rsp
    call    read_line

    ; Execute command
    mov     rdi, rsp
    mov     rsi, rbx
    call    execute_cmd

    jmp     .prompt_loop

    ; Never reached
    add     rsp, 256
    mov     rax, 60
    xor     rdi, rdi
    syscall

; ---------------------------------------------------------------------------
; read_line(rdi = buffer ptr)
;   Returns length in rbx.
; ---------------------------------------------------------------------------
read_line:
    push    rbp
    mov     rbp, rsp
    sub     rsp, 16

    mov     r12, rdi
    xor     rbx, rbx

.rl_loop:
    mov     rax, 0
    mov     rdi, 0
    lea     rsi, [rbp-8]
    mov     rdx, 1
    syscall

    movzx   eax, byte [rbp-8]

    cmp     al, 0x0A
    je      .rl_done
    cmp     al, 0x0D
    je      .rl_done

    cmp     al, 0x08
    je      .rl_bs
    cmp     al, 0x7F
    je      .rl_bs

    cmp     rbx, 254
    jge     .rl_loop

    mov     [r12 + rbx], al
    inc     rbx

    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rbp-8]
    mov     rdx, 1
    syscall

    jmp     .rl_loop

.rl_bs:
    test    rbx, rbx
    jz      .rl_loop
    dec     rbx
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_bs]
    mov     rdx, msg_bs_len
    syscall
    jmp     .rl_loop

.rl_done:
    mov     byte [r12 + rbx], 0
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_newline]
    mov     rdx, msg_nl_len
    syscall

    add     rsp, 16
    pop     rbp
    ret

; ---------------------------------------------------------------------------
; execute_cmd(rdi = buffer, rsi = length)
; ---------------------------------------------------------------------------
execute_cmd:
    push    rbp
    mov     rbp, rsp

    test    rsi, rsi
    jz      .done

    cmp     rsi, cmd_clear_len
    jne     .unknown

    lea     r8,  [rel cmd_clear]
    mov     r9,  rdi
    mov     rcx, cmd_clear_len

.cmp_loop:
    movzx   eax,  byte [r9]
    movzx   r10d, byte [r8]
    cmp     eax,  r10d
    jne     .unknown
    inc     r9
    inc     r8
    dec     rcx
    jnz     .cmp_loop

    ; Clear screen then reprint banner
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_cls2]
    mov     rdx, msg_cls2_len
    syscall

    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_banner]
    mov     rdx, msg_banner_len
    syscall
    jmp     .done

.unknown:
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_unknown]
    mov     rdx, msg_unknown_len
    syscall

.done:
    pop     rbp
    ret
