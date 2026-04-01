# NumOS OSDev Checklist

This checklist tracks NumOS against the broad OSDev learning path. It is a project status snapshot, not a claim of feature completeness or production maturity.

Status key:

- `[x]` implemented
- `[~]` partial
- `[ ]` missing
- `N/A` not a kernel feature

Reference pages:

- https://wiki.osdev.org/Expanded_Main_Page
- https://wiki.osdev.org/Creating_an_Operating_System

## Phase 0

- `N/A` Welcome to Operating Systems Development
- `[~]` Building the latest GCC
  - `tools/build-cross-compiler.sh` builds an `x86_64-elf` toolchain
  - no NumOS-specific GCC target exists yet

## Phase I

- `[x]` Setting up a Cross-Toolchain
  - `tools/build-cross-compiler.sh`
- `[x]` Creating a Hello World kernel
  - bootable Multiboot2 kernel with console output
- `[x]` Setting up a Project
  - split tree for `src`, `Include`, `user`, `tools`, and `docs`
- `[x]` Calling Global Constructors
  - kernel and user runtimes walk `.init_array`
  - `src/kernel/runtime.c`
  - `user/runtime/runtime.c`
- `[x]` Terminal Support
  - VGA text output
  - framebuffer console paths
  - scrollback support
- `[x]` Stack Smash Protector
  - kernel and user builds use stack protector hooks
  - `Makefile`
  - `user/Makefile`
- `[x]` Multiboot
  - GRUB Multiboot2 boot path
  - `src/boot/multiboot_header.asm`
  - `Include/kernel/multiboot2.h`
- `[x]` Global Descriptor Table
  - `src/cpu/x86/gdt.c`
- `[x]` Memory Management
  - physical frame tracking
  - 4 level paging
  - kernel heap
  - `src/cpu/x86/paging.c`
  - `src/cpu/x86/heap.c`
- `[x]` Interrupts
  - IDT
  - exception handlers
  - PIC IRQ routing
  - `src/cpu/x86/idt.c`
  - `src/drivers/pic.c`
- `[~]` Multithreaded Kernel
  - scheduler and context switching exist
  - idle task and basic kernel threads exist
  - wider locking and SMP safety are still incomplete
  - `src/kernel/scheduler.c`
  - `src/boot/context_switch.asm`
- `[x]` Keyboard
  - PS/2 keyboard driver with buffered input
  - `src/drivers/keyboard.c`
- `[~]` Internal Kernel Debugger
  - boot diagnostics and GDB flow exist
  - no in-kernel debugger shell or symbol-aware backtrace tool
- `[x]` Filesystem Support
  - FAT32 support
  - ramdisk module path
  - `src/fs/fat32.c`
  - `src/drivers/ramdisk.c`

## Phase II

- `[x]` User-Space
  - ring 3 processes through the syscall path
  - `src/kernel/syscall.c`
  - `user/runtime/entry.asm`
- `[~]` Program Loading
  - ELF64 `ET_EXEC` and `ET_DYN` loading works
  - static PIE-style relocations are handled
  - PT_TLS data is copied into thread-local storage
  - shared libraries and cross-image symbol resolution are missing
  - `src/kernel/elf_loader.c`
- `[x]` System Calls
  - file, process, input, time, framebuffer, and info syscalls exist
  - `Include/kernel/syscall.h`
  - `src/kernel/syscall.c`
- `[ ]` OS Specific Toolchain
  - the toolchain target is still `x86_64-elf`
- `[~]` Creating a C Library
  - a small freestanding user runtime and libc layer exist
  - allocator, stdio, and a larger hosted ABI are still missing
  - `user/runtime/libc.c`
  - `user/include/libc.h`
- `[~]` Fork and Execute
  - `exec` exists
  - `fork` is not implemented
- `[x]` Shell
  - `user/programs/shell.c`

## Phase III

- `[x]` Time
  - uptime and sleep syscalls
  - RTC-backed date and time syscall
  - `src/drivers/timer.c`
  - `src/drivers/rtc.c`
  - `src/kernel/syscall.c`
- `[x]` Threads
  - kernel thread creation path exists
  - user thread create, join, self, and exit exist
  - `user/programs/thread.c` exercises the user path
  - `src/kernel/scheduler.c`
  - `src/kernel/kmain.c`
- `[x]` Thread Local Storage
  - ELF PT_TLS support exists
  - x86_64 FS base is used for per-thread TLS
- `[~]` Symmetric Multiprocessing
  - APIC and AP bring-up work exists
  - scheduler and locking model are not fully SMP safe
- `[x]` Secondary Storage
  - ATA PIO support
  - ramdisk support
  - `src/drivers/ata.c`
  - `src/drivers/ramdisk.c`
- `[~]` Real Filesystems
  - FAT32 support exists
  - VFS mount routing exists
  - no ext2 or broader persistent filesystem support
- `[x]` Graphics
  - VGA
  - VESA
  - generic framebuffer console
  - `src/drivers/graphices`
  - `src/drivers/framebuffer.c`
- `[ ]` User Interface
  - text shell exists
  - no desktop UI or window system exists
- `[~]` Networking
  - e1000 path exists for QEMU
  - DHCP, ARP, IPv4, and ICMP echo exist
  - `net` user tool exposes the current kernel path
  - no TCP stack or sockets yet
- `[ ]` Sound
  - intentionally not implemented
- `[~]` Universal Serial Bus
  - controller and root port inspection exists
  - transfer engine, enumeration, and class drivers are missing

## Phase IV

- `[~]` Porting Software
  - custom NumOS user programs run
  - third-party software porting is still limited
- `[ ]` Porting GCC
  - no native NumOS GCC target
- `[ ]` Compiling your OS under your OS
  - full host build still depends on external tools
- `[~]` Fully Self-hosting
  - shell, editor, package tool, and small build helper exist
  - full native compiler and wider package ecosystem do not

## Phase V

- `[ ]` Profit

## Biggest Remaining Gaps

The largest missing blocks for a stronger OSDev milestone are:

1. a NumOS-specific toolchain target
2. a larger hosted libc surface
3. `fork`
4. stronger SMP safety and locking
5. broader filesystem and persistence support
6. sockets and a fuller network stack
7. deeper USB support
8. native self-hosted build tooling
