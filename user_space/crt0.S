/*
 * crt0.S — NumOS user-space test program
 *
 * Entry point of the user-space executable.
 * Writes directly to VGA buffer to display "TEST FROM USER SPACE"
 * No syscalls needed - just direct memory writes.
 *
 * VGA text mode buffer is at physical 0xB8000, which should be
 * identity-mapped by the kernel's boot paging.
 */

    .section .rodata
msg:
    .asciz "TEST FROM USER SPACE"
msg_len = . - msg - 1

    .section .text
    .global _start

_start:
    /* Clear frame pointer */
    xorq    %rbp, %rbp

    /* 
     * Write message directly to VGA buffer at 0xB8000
     * Each character takes 2 bytes: one for ASCII, one for attribute
     * We'll write to columns 40-60 on line 12 (offset 0xB8000 + 24*160 + 80)
     */
    
    /* RBX = VGA buffer address + offset for row 12, col 0 */
    movq    $0xB8000,   %rbx
    
    /* RBX = 0xB8000 + (12 * 160) = 0xB8000 + 0xBE0 = 0xB8BE0 */
    addq    $0xBE0,     %rbx
    
    /* RSI = pointer to message string */
    leaq    msg(%rip),  %rsi
    
    /* RCX = message length */
    movq    $msg_len,   %rcx
    
    /* RAX = attribute: 0x0F = white on black */
    movq    $0x0F,      %rax

write_loop:
    /* Check if we've written all characters */
    cmpq    $0,         %rcx
    je      write_done
    
    /* Load character from message */
    movzbl  (%rsi),     %edx    /* Load byte, zero extend to 32-bit */
    
    /* Write character to VGA buffer: char in low byte, attribute in high byte */
    movb    %dl,        (%rbx)  /* Write ASCII character */
    movb    %al,        1(%rbx) /* Write attribute (white on black) */
    
    /* Move to next character position (2 bytes per char in VGA) */
    addq    $2,         %rbx
    addq    $1,         %rsi
    subq    $1,         %rcx
    
    jmp     write_loop

write_done:
    /* 
     * Message written. Now just halt.
     * Execute an infinite loop.
     */
    cli                         /* Disable interrupts */
    
halt_loop:
    hlt                         /* Halt CPU */
    jmp     halt_loop           /* If we somehow resume, loop */