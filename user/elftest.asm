; =============================================================================
; user/elftest.asm  –  NumOS userland test program
;
; Exercises every syscall currently implemented by the kernel:
;   SYS_WRITE    (1)   – write bytes to stdout
;   SYS_GETPID   (39)  – return current PID
;   SYS_UPTIME_MS(96)  – return kernel uptime in milliseconds
;   SYS_SLEEP_MS (35)  – sleep N milliseconds
;   SYS_PUTS     (200) – write null-terminated string + newline
;   SYS_EXIT     (60)  – terminate process
;
; Syscall ABI (x86-64, same as Linux):
;   rax = number | rdi = arg1 | rsi = arg2 | rdx = arg3
;   syscall  →  rax = return value
; =============================================================================

bits 64

global _start

; ---------------------------------------------------------------------------
section .rodata

; ---- raw byte strings used with SYS_WRITE --------------------------------
msg_hello:     db  "=== NumOS Userland Test Program ===", 0x0A
msg_hello_len  equ $ - msg_hello

msg_write_ok:  db  "[PASS] sys_write: output works", 0x0A
msg_write_len  equ $ - msg_write_ok

msg_pid_prefix:db  "[INFO] sys_getpid returned: "
msg_pid_pfxlen equ $ - msg_pid_prefix

msg_newline:   db  0x0A
msg_nl_len     equ 1

msg_uptime_pre:db  "[INFO] sys_uptime_ms returned: "
msg_uptime_len equ $ - msg_uptime_pre

msg_ms:        db  " ms", 0x0A
msg_ms_len     equ $ - msg_ms

msg_sleep_pre: db  "[INFO] sys_sleep_ms(200): sleeping...", 0x0A
msg_sleep_prelen equ $ - msg_sleep_pre

msg_sleep_done:db  "[PASS] sys_sleep_ms: woke up", 0x0A
msg_sleep_len  equ $ - msg_sleep_done

; ---- null-terminated strings used with SYS_PUTS --------------------------
str_puts_test: db  "[PASS] sys_puts: null-terminated string write works", 0
str_exit_msg:  db  "[INFO] sys_exit: calling exit(0) now...", 0

; ---------------------------------------------------------------------------
section .bss

; Scratch buffer for formatting numbers as ASCII
num_buf: resb 24

; ---------------------------------------------------------------------------
section .text

; ---------------------------------------------------------------------------
; _start – entry point
; ---------------------------------------------------------------------------
_start:
    ; ====================================================================
    ; 1.  SYS_WRITE: print banner
    ; ====================================================================
    mov     rax, 1                  ; SYS_WRITE
    mov     rdi, 1                  ; fd = stdout
    lea     rsi, [rel msg_hello]
    mov     rdx, msg_hello_len
    syscall

    ; ====================================================================
    ; 2.  SYS_WRITE: write_ok message
    ; ====================================================================
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_write_ok]
    mov     rdx, msg_write_len
    syscall

    ; ====================================================================
    ; 3.  SYS_GETPID: retrieve PID and print it
    ; ====================================================================
    mov     rax, 1                  ; SYS_WRITE – prefix
    mov     rdi, 1
    lea     rsi, [rel msg_pid_prefix]
    mov     rdx, msg_pid_pfxlen
    syscall

    mov     rax, 39                 ; SYS_GETPID
    syscall
    ; rax now holds PID (1 in current kernel)

    mov     rdi, rax                ; value to format
    lea     rsi, [rel num_buf]      ; output buffer
    call    uint64_to_str           ; returns length in rax
    mov     rdx, rax                ; length
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel num_buf]
    syscall

    mov     rax, 1                  ; newline
    mov     rdi, 1
    lea     rsi, [rel msg_newline]
    mov     rdx, msg_nl_len
    syscall

    ; ====================================================================
    ; 4.  SYS_UPTIME_MS: get uptime and print it
    ; ====================================================================
    mov     rax, 1                  ; SYS_WRITE – prefix
    mov     rdi, 1
    lea     rsi, [rel msg_uptime_pre]
    mov     rdx, msg_uptime_len
    syscall

    mov     rax, 96                 ; SYS_UPTIME_MS
    syscall
    ; rax = uptime in ms

    mov     rdi, rax
    lea     rsi, [rel num_buf]
    call    uint64_to_str
    mov     rdx, rax
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel num_buf]
    syscall

    mov     rax, 1                  ; " ms\n"
    mov     rdi, 1
    lea     rsi, [rel msg_ms]
    mov     rdx, msg_ms_len
    syscall

    ; ====================================================================
    ; 5.  SYS_SLEEP_MS: sleep 200 ms
    ; ====================================================================
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_sleep_pre]
    mov     rdx, msg_sleep_prelen
    syscall

    mov     rax, 35                 ; SYS_SLEEP_MS
    mov     rdi, 200                ; 200 ms
    syscall

    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg_sleep_done]
    mov     rdx, msg_sleep_len
    syscall

    ; ====================================================================
    ; 6.  SYS_PUTS: write a null-terminated string (kernel adds newline)
    ; ====================================================================
    mov     rax, 200                ; SYS_PUTS
    lea     rdi, [rel str_puts_test]
    syscall

    ; ====================================================================
    ; 7.  SYS_PUTS: farewell message before exit
    ; ====================================================================
    mov     rax, 200
    lea     rdi, [rel str_exit_msg]
    syscall

    ; ====================================================================
    ; 8.  SYS_EXIT: terminate with status 0
    ; ====================================================================
    mov     rax, 60                 ; SYS_EXIT
    xor     rdi, rdi                ; status = 0
    syscall

.hang:
    ; Should never reach here – kernel's sys_exit spins forever
    jmp     .hang

; ---------------------------------------------------------------------------
; uint64_to_str
;   in:  rdi = unsigned 64-bit value
;        rsi = pointer to output buffer (at least 21 bytes)
;   out: rax = number of characters written (no null terminator)
;   clobbers: rcx, rdx, r8
; ---------------------------------------------------------------------------
uint64_to_str:
    push    rbx
    push    rbp
    mov     rbx, rsi                ; save base pointer
    mov     rbp, rsi
    add     rbp, 20                 ; point to end of scratch space
    mov     byte [rbp], 0           ; null terminator

    test    rdi, rdi
    jnz     .convert
    ; special case: value is 0
    mov     byte [rbx], '0'
    mov     rax, 1
    pop     rbp
    pop     rbx
    ret

.convert:
    ; Build digits in reverse at the end of the scratch area
    mov     rax, rdi                ; value
    mov     r8,  rbp                ; cursor (working backwards)
.digit_loop:
    test    rax, rax
    jz      .done_digits
    xor     rdx, rdx
    mov     rcx, 10
    div     rcx                     ; rax = quotient, rdx = remainder
    dec     r8
    add     dl, '0'
    mov     [r8], dl
    jmp     .digit_loop

.done_digits:
    ; Copy from r8 to rbx (forward into output buffer)
    mov     rdi, rbx                ; destination
    mov     rsi, r8                 ; source
    xor     rcx, rcx
.copy_loop:
    mov     dl, [rsi]
    test    dl, dl
    jz      .copy_done
    mov     [rdi], dl
    inc     rsi
    inc     rdi
    inc     rcx
    jmp     .copy_loop
.copy_done:
    mov     rax, rcx                ; return length
    pop     rbp
    pop     rbx
    ret