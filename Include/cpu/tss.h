/*
 * tss.h — NumOS minimal Task State Segment (TSS) for Ring 3 support
 *
 * WHY THIS IS NEEDED:
 *   When an interrupt or exception fires while the CPU is executing at Ring 3
 *   (user space), the CPU must switch to a kernel stack.  It loads the new RSP
 *   from TSS.RSP0.  If no TSS is installed (or RSP0 == 0), the CPU uses address
 *   0 as the kernel stack, which immediately page-faults, causing a double fault,
 *   then a triple fault → machine reset.
 *
 *   This is the reason the OS reboots as soon as the first interrupt fires after
 *   exec_user_elf() jumps to Ring 3.
 *
 * USAGE:
 *   Call tss_init() once, after gdt_init() but before exec_user_elf():
 *
 *     gdt_init();
 *     idt_init();
 *     tss_init();          // <-- required for Ring 3
 *     syscall_init();
 *     exec_user_elf(...);
 *
 *   tss_init() installs a TSS descriptor in GDT[5], loads it with ltr, and
 *   sets TSS.RSP0 to the top of the kernel interrupt stack.
 *
 * GDT SLOT:
 *   GDT[5] = TSS descriptor (selector 0x28).  The TSS descriptor is 16 bytes
 *   (two GDT slots) in 64-bit mode.  Your GDT must have room for at least 7
 *   entries (indices 0-6).
 */

#ifndef TSS_H
#define TSS_H

#include "lib/base.h"

/*
 * tss_init — install the TSS descriptor in the GDT and load it.
 *
 * Sets TSS.RSP0 so the CPU has a valid kernel stack when taking interrupts
 * from Ring 3.  Must be called after gdt_init().
 */
void tss_init(void);

/*
 * tss_set_rsp0 — update RSP0 (kernel stack pointer for Ring 0 entry).
 *
 * Call this if you ever switch tasks / processes.  For a single-process
 * kernel, tss_init() sets it once and you don't need this.
 */
void tss_set_rsp0(uint64_t rsp0);

#endif /* TSS_H */