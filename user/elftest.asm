; =============================================================================
; elftest.asm - NumOS Userland Entry Point (stub)
;
; A minimal x86-64 ELF executable that demonstrates the syscall ABI.
; This proves the ELF loader can find, parse, and map the binary.
; Actual execution requires the kernel's ring-3 / SYSCALL dispatch.
;
; Syscall ABI (matches Linux x86-64 for future compatibility):
;   rax = syscall number
;   rdi = arg1,  rsi = arg2,  rdx = arg3
;   syscall instruction; return value in rax
;
; NumOS planned syscall numbers:
;   1  = sys_write  (fd, buf, count)
;   60 = sys_exit   (status)
; =============================================================================

bits 64

global _start

section .data
    msg     db  "Hello from NumOS userland!", 0x0A
    msg_len equ $ - msg

    msg2     db  "ELF stub running - waiting for process manager.", 0x0A
    msg2_len equ $ - msg2

section .text

_start:
    ; sys_write(fd=1, buf=msg, count=msg_len)
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg]
    mov     rdx, msg_len
    syscall

    ; sys_write(fd=1, buf=msg2, count=msg2_len)
    mov     rax, 1
    mov     rdi, 1
    lea     rsi, [rel msg2]
    mov     rdx, msg2_len
    syscall

    ; sys_exit(0)
    mov     rax, 60
    xor     rdi, rdi
    syscall

.hang:
    jmp     .hang