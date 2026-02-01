/*
 * crt0.S — NumOS user-space C runtime startup
 *
 * This is the true entry point of every user-space executable.  The
 * kernel's ELF loader sets RIP to _start, not to main.  Our job here is
 * to call main() and pass its return value to SYS_EXIT.
 *
 * Execution flow:
 *   1. Kernel loads the ELF, sets up user stack, and iretq to _start.
 *   2. _start (below) calls main().
 *   3. main() returns an int (the exit status).
 *   4. We move that status into RDI and invoke SYS_EXIT via syscall.
 *   5. The kernel never returns from SYS_EXIT; it halts.
 *
 * No argc/argv: this kernel does not populate process arguments.  main()
 * is declared as int main(void) in main.c, so we do not push anything
 * onto the stack before calling it.
 *
 * Assembly syntax: AT&T (the default for GNU as).  Operands are
 * source, destination: movq %rax, %rbx means "move RAX into RBX".
 */

    .section .text
    .global _start

_start:
    /*
     * The kernel has already set RSP to the top of our user stack.
     * We do NOT need to initialize it again.  Just align to 16 bytes
     * before the call (AMD64 ABI requires RSP ≡ 8 mod 16 at a CALL).
     *
     * At entry, RSP is 16-byte aligned from the kernel.  The `call`
     * instruction will push an 8-byte return address, leaving RSP
     * at 8 mod 16 inside main, which is correct.
     */

    /* Clear frame pointer for stack unwinding hygiene */
    xorq    %rbp, %rbp

    /* Call main().  It takes no arguments (void). */
    call    main

    /*
     * main() has returned.  Its return value (the exit status) is in
     * RAX.  We move it to RDI (first syscall argument) and invoke
     * SYS_EXIT.
     */
    movq    %rax, %rdi        /* RDI = exit status */
    movq    $60,  %rax        /* RAX = SYS_EXIT */
    syscall

    /*
     * Unreachable.  SYS_EXIT does not return.  If we somehow get here,
     * execute an undefined instruction to trigger a fault.
     */
    ud2