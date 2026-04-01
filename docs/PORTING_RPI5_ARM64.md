# Raspberry Pi 5 ARM64 Port Plan

NumOS currently boots on x86_64 PCs through GRUB Multiboot2. Raspberry Pi 5 support requires a separate ARM64 platform bring-up.

## Why The Current Tree Does Not Boot On Pi 5

The current codebase is tightly coupled to x86_64 PC hardware in several places:

- `src/boot/*.asm` contains NASM entry code for the current PC boot path
- `src/cpu/x86/*` owns GDT, IDT, paging, APIC, FPU, and TSS setup
- `src/drivers/pic.c`, `src/drivers/ata.c`, and PS/2 keyboard paths assume PC devices
- `src/kernel/syscall.c`, `src/kernel/elf_loader.c`, and `user/runtime/entry.asm` assume the x86_64 ABI
- the build and boot flow expect GRUB Multiboot2 on a PC-style machine

## Recommended Port Order

### 1. Bring up a serial-only ARM64 kernel

Start with the smallest path that proves execution:

- add `src/cpu/arm64/`
- add an ARM64 linker script
- add a board boot path, usually U-Boot, EDK2, or a board-specific loader
- print early logs on serial before touching framebuffer code

Target result:

- one core reaches C code
- early panic and status logs work over serial

### 2. Replace x86_64 CPU setup with ARM64 EL1 setup

Build the minimum platform layer needed for a scheduler and syscall path:

- exception vectors
- EL1 startup code
- MMU and page table setup
- timer support
- interrupt controller support

Target result:

- memory management works
- timer interrupts fire
- exceptions land in a known handler

### 3. Rebuild the hardware layer around Pi 5 devices

The current driver set assumes PC parts. Replace those assumptions with Pi 5 or generic ARM-friendly drivers:

- serial console first
- timer path
- storage path
- framebuffer path after serial is stable
- interrupt controller integration

Target result:

- stable console
- stable timer
- one storage path for loading files

### 4. Port user space

User mode depends on ABI, syscall, and ELF details. Port them together:

- add ARM64 `_start` in `user/runtime/entry.asm`
- add ARM64 syscall entry and return handling
- extend ELF loading for `EM_AARCH64`
- add ARM64 user linker settings

Target result:

- one static user ELF loads and runs
- the shell works over serial

### 5. Add repeatable run targets

Add a fast development loop before moving to board-only testing:

- add `qemu-system-aarch64` support where useful
- add a board image path for Raspberry Pi 5 media
- document the exact boot files and layout

Target result:

- one command for emulator bring-up
- one documented path for real hardware tests

## Suggested Milestones

First milestone:

- boot to serial
- initialize memory
- handle timer interrupts
- enter the scheduler
- run one kernel thread

Second milestone:

- load one ARM64 user ELF
- run the shell over serial
- add framebuffer support after the serial path is stable

## Porting Rule Of Thumb

Do not try to carry the full x86_64 feature set over at once. Get a serial-only kernel working first, then add interrupts, then storage, then user space.
