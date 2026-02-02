/*
 * crt0.S — NumOS user-space C runtime startup
 *
 * Entry point of the user-space executable.
 * Displays "Hello World From User space" via syscall and then halts.
 *
 * Execution flow:
 *   1. Kernel loads the ELF, sets up user stack, and iretq to _start.
 *   2. _start (below) writes the message to stdout via SYS_WRITE.
 *   3. We invoke SYS_EXIT to halt.
 *
 * Assembly syntax: AT&T (the default for GNU as).  Operands are
 * source, destination: movq %rax, %rbx means "move RAX into RBX".
 */

    .section .rodata
msg:
    .asciz "Hello World From User space\n"
msg_len = . - msg - 1   /* Exclude null terminator */

    .section .text
    .global _start

_start:
    /*
     * The kernel has already set RSP to the top of our user stack.
     * Clear frame pointer for stack unwinding hygiene.
     */
    xorq    %rbp, %rbp

    /* 
     * Issue SYS_WRITE(1, msg, len) via syscall:
     *   RAX = 1  (SYS_WRITE)
     *   RDI = 1  (stdout)
     *   RSI = pointer to message
     *   RDX = message length
     */
    movq    $1,         %rax    /* RAX = SYS_WRITE */
    movq    $1,         %rdi    /* RDI = stdout fd */
    leaq    msg(%rip),  %rsi    /* RSI = &msg (RIP-relative) */
    movq    $msg_len,   %rdx    /* RDX = message length */
    syscall

    /*
     * Write completed.  Now exit via SYS_EXIT(0).
     *   RAX = 60 (SYS_EXIT)
     *   RDI = 0  (exit status)
     */
    movq    $60,  %rax          /* RAX = SYS_EXIT */
    movq    $0,   %rdi          /* RDI = exit status */
    syscall

    /*
     * SYS_EXIT does not return.  If we somehow get here,
     * execute an undefined instruction to trigger a fault.
     */
    ud2