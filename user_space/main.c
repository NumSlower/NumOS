/*
 * main.c — NumOS first user-space process ("shell")
 *
 * This is the program the kernel loads from /init/SHELL on the FAT32
 * volume.  It is intentionally minimal: it exercises the two syscalls
 * the kernel currently implements (SYS_WRITE and SYS_EXIT) and
 * demonstrates the full user-space lifecycle.
 *
 * Design constraints:
 *   - No libc.  We link with -nostdlib.  All I/O goes through our
 *     thin syscall wrappers.
 *   - No heap allocation.  All data is either on the stack or in
 *     .rodata (string literals).
 *   - No argc/argv.  The kernel does not set up a process argument
 *     vector.  main() takes no parameters.
 *   - Return value of main() becomes the exit status passed to
 *     SYS_EXIT by crt0.S.  We return 0 on success.
 *
 * Compilation:
 *   gcc -c main.c -o main.o -fno-pie -nostdlib -O2 \
 *       -fno-asynchronous-unwind-tables -fno-unwind-tables
 *
 * The flags are documented in the Makefile.
 */

#include "user_syscall.h"

/* ─── Helper: write a NUL-terminated string to stdout ────────────
 * Computes length by scanning for NUL, then issues one SYS_WRITE.
 * Returns the kernel's return value (bytes written or -1).
 *
 * This is deliberately not called "puts" or "printf" to avoid any
 * accidental symbol conflict with a future libc stub layer.
 * ─────────────────────────────────────────────────────────────── */
static long write_str(const char *s)
{
    /* Compute length without strlen — we have no libc. */
    long len = 0;
    const char *p = s;
    while (*p) {
        len++;
        p++;
    }
    return sys_write(1, s, len);
}

/* ─── main ───────────────────────────────────────────────────────
 * No argc/argv — the kernel does not populate them.
 * Returns 0; crt0.S passes this to SYS_EXIT.
 * ─────────────────────────────────────────────────────────────── */
int main(void)
{
    write_str("NumOS user space: shell started\n");
    write_str("Hello from ring 3!\n");
    write_str("Exiting cleanly.\n");
    return 0;
}