# NumOS OSDev Checklist

This file tracks NumOS against the OSDev "Creating an Operating System" path.

Source pages:
- https://wiki.osdev.org/Expanded_Main_Page
- https://wiki.osdev.org/Creating_an_Operating_System

Status key:
- `[x]` complete
- `[~]` partial
- `[ ]` missing
- `N/A` not a kernel feature

## Phase 0

- `N/A` Welcome to Operating Systems Development
- `[~]` Building the latest GCC
  - `tools/build-cross-compiler.sh` builds an `x86_64-elf` cross toolchain.
  - NumOS does not yet ship its own OS target in GCC.

## Phase I

- `[x]` Setting up a Cross-Toolchain
  - `tools/build-cross-compiler.sh`
- `[x]` Creating a Hello World kernel
  - Bootable Multiboot2 kernel with text output
- `[x]` Setting up a Project
  - `Makefile`, split `src`, `Include`, `user`, `tools`
- `[x]` Calling Global Constructors
  - Kernel and user runtimes now walk `.init_array`
  - `src/kernel/runtime.c`
  - `user/runtime/runtime.c`
- `[x]` Terminal Support
  - VGA text console
  - Framebuffer console
  - Scrollback
- `[x]` Stack Smash Protector
  - Kernel and user builds use stack protector with freestanding guard/fail hooks
  - `Makefile`
  - `user/Makefile`
- `[x]` Multiboot
  - Multiboot2 header and GRUB boot path
  - `src/boot/multiboot_header.asm`
  - `Include/kernel/multiboot2.h`
- `[x]` Global Descriptor Table
  - `src/cpu/x86/gdt.c`
- `[x]` Memory Management
  - Physical frame tracking
  - 4 level paging
  - Kernel heap
  - `src/cpu/x86/paging.c`
  - `src/cpu/x86/heap.c`
- `[x]` Interrupts
  - IDT
  - PIC IRQ routing
  - exception handlers
  - `src/cpu/x86/idt.c`
  - `src/drivers/pic.c`
- `[~]` Multithreaded Kernel
  - Scheduler and context switching exist
  - Idle task, user processes, and basic kernel thread creation exist
  - No full synchronization layer or broader locking model
  - `src/kernel/scheduler.c`
  - `src/boot/context_switch.asm`
- `[x]` Keyboard
  - PS/2 keyboard driver
  - buffered input
  - `src/drivers/keyboard.c`
- `[~]` Internal Kernel Debugger
  - Basic diagnostics and interactive kernel menu exist
  - GDB workflow is documented in `README.md`
  - No real in kernel debugger shell, symbol aware backtrace, or breakpoint control
- `[x]` Filesystem Support
  - FAT32 driver
  - multiboot ramdisk
  - `src/fs/fat32.c`
  - `src/drivers/ramdisk.c`

## Phase II

- `[x]` User-Space
  - Ring 3 user processes via `sysret`
  - `src/kernel/syscall.c`
  - `user/runtime/entry.asm`
- `[~]` Program Loading
  - Static ELF64 loading works
  - Dynamic linking and relocation support are not complete
  - `src/kernel/elf_loader.c`
- `[x]` System Calls
  - File, process, time, framebuffer, input, and info syscalls
  - `Include/kernel/syscall.h`
  - `src/kernel/syscall.c`
- `[ ]` OS Specific Toolchain
  - Cross compiler target is still `x86_64-elf`
  - No `x86_64-numos` target or ABI integration
- `[~]` Creating a C Library
  - User space now ships a small NumOS runtime and libc starter layer
  - No hosted ABI, allocator, stdio, or broader POSIX surface yet
  - `user/runtime/libc.c`
  - `user/include/libc.h`
- `[~]` Fork and Execute
  - `exec` exists
  - `fork` does not exist
  - `Include/kernel/syscall.h`
- `[x]` Shell
  - `user/programs/shell.c`

## Phase III

- `[x]` Time
  - uptime and sleep exist
  - RTC backed calendar time syscall exists
  - timer object API exists
  - `src/drivers/timer.c`
  - `src/drivers/rtc.c`
  - `src/kernel/syscall.c`
- `[~]` Threads
  - Kernel thread creation API exists and is exercised during boot
  - No user thread creation API yet
  - `src/kernel/scheduler.c`
  - `src/kernel/kmain.c`
- `[ ]` Thread Local Storage
  - No TLS runtime or ABI support
- `[~]` Symmetric Multiprocessing
  - Local APIC init and AP bring up exist
  - Scheduler is not fully SMP ready
  - No multiprocessor locking model or load balancing
  - `src/kernel/process.c`
  - `src/cpu/x86/apic.c`
- `[x]` Secondary Storage
  - ATA PIO support
  - RAM disk support
  - `src/drivers/ata.c`
  - `src/drivers/ramdisk.c`
- `[~]` Real Filesystems
  - FAT32 support exists
  - VFS layer now routes kernel file operations through mount points
  - No ext2 or other broader filesystem set
  - No mature persistence model yet
- `[x]` Graphics
  - VGA
  - VESA
  - BGA
  - generic framebuffer console
  - `src/drivers/graphices`
  - `src/drivers/framebuffer.c`
- `[ ]` User Interface
  - Text shell exists
  - No graphical desktop, windows, compositor, or widget stack
- `[ ]` Networking
  - Excluded by request
- `[ ]` Sound
  - Excluded by request
- `[ ]` Universal Serial Bus
  - No USB host controller stack or device support

## Phase IV

- `[~]` Porting Software
  - NumOS runs small custom user programs
  - No broader third party software port set
- `[ ]` Porting GCC
  - No native NumOS GCC target
- `[ ]` Compiling your OS under your OS
  - Build still depends on host tools
- `[ ]` Fully Self-hosting
  - No native compiler, make, editor, package flow, or full development environment

## Phase V

- `[ ]` Profit

## Summary

NumOS already covers much of Phase I, most of the core of Phase II, and parts of Phase III.

The biggest missing blocks are:

1. OS specific toolchain target
2. fuller C library and hosted ABI
3. `fork`
4. user threads and TLS
5. full SMP safety
6. broader filesystem coverage and more mature persistence work
7. graphical UI
8. USB
9. self hosting toolchain and build environment
