# NumOS

NumOS is a from-scratch 64-bit x86_64 operating system: a Multiboot2/GRUB2
booted kernel with its own memory management, interrupt handling, drivers,
and eventually its own filesystem, userspace, shell, system monitor, and
native graphical desktop. NumOS is not based on Linux, BSD, or any existing
kernel, shell, or desktop environment.

## Current status

| Subsystem | Status |
|---|---|
| Multiboot2 boot, long-mode entry, GDT/TSS | Implemented, built |
| IDT, CPU exceptions, IRQs, PIC, NMI | Implemented, built |
| ACPI (RSDP/RSDT/XSDT/MADT), Local APIC, I/O APIC | Implemented, built |
| PMM, VMM (4-level paging), kernel heap | Implemented, built |
| CPUID, FPU/SSE enable | Implemented, built |
| Serial, VGA text, PS/2 keyboard, PIT, BGA probe | Implemented, built |
| PCI configuration-space discovery | Implemented, built, tested |
| Multiboot2 framebuffer tag parsing | Implemented, built, tested |
| Block-device layer (bounds checks, counters, errors) | Implemented, built, tested |
| ATA PIO storage driver (LBA28 read/write/flush) | Implemented, built, tested |
| VFS layer (mounts, open/read/write/stat/list/unlink) | Implemented, built, tested |
| FAT32 filesystem (8.3+LFN read, create/write, unlink) | Implemented, built, tested |
| Persistence across reboots (boot counter) | Implemented, built, tested |
| Processes, kernel threads, PID allocation | Implemented, built, tested |
| Context switching, round-robin scheduler, exit/reap | Implemented, built, tested |
| Ring 3 user processes (address-space isolation, TSS stacks) | Implemented, built, tested |
| System-call table (int 0x80, validation, per-process fds) | Implemented, built, tested |
| Timer-driven preemption of Ring 3 (CS-checked) | Implemented, built, tested |
| Userspace memory allocation (sbrk) | Implemented, built, tested |
| ELF64 loader (validated, W^X, embedded + VFS disk exec) | Implemented, built, tested |
| Terminal/TTY subsystem (line discipline, ANSI, scrollback) | Implemented, built, tested |
| stdin/stdout/stderr as terminal fds, Ctrl-C fg-process stop | Implemented, built, tested |
| Multi-session terminals (Ctrl-N switch, per-session shell/input) | Implemented, built, tested |
| Shell env variables, \$? status, history, lexer/conditionals | Implemented, built, tested |
| Shell builtins (echo ls cat stat write rm ps env set, > / >>) | Implemented, built, tested |
| exec/wait of /BIN ELF programs with argv/envp and exit status | Implemented, built, tested |
| Shell kernel pipes (12.4), 2>, scripting if/while (12.5), job control | Deferred |
| System monitoring | Not implemented |
| Framebuffer graphics, mouse, windows, desktop | Not implemented |

Status labels follow the master specification: implemented / built / tested /
partially tested / blocked / not implemented.

## Prerequisites

NumOS requires a freestanding cross toolchain and QEMU. On Windows, build and
run inside WSL2 Ubuntu — full setup instructions (including building
`x86_64-elf-gcc` from source) are in [BUILDING.md](BUILDING.md).

Required tools: `x86_64-elf-gcc`, `x86_64-elf-ld`, `nasm`, `grub-mkrescue`,
`xorriso`, `qemu-system-x86_64`, `make`, `mtools` (for `make disk`).

## Build and run

```bash
make            # build the kernel and bootable ISO (build/numos.iso)
make kernel     # build the kernel ELF (build/numos.bin)
make iso        # build the GRUB2 bootable ISO
make disk       # create a blank writable FAT32 disk image (build/disk.img)
make run        # boot the ISO in QEMU with a display and serial console
make run-text   # boot headless, serial console only (text-mode path)
make run-gui    # boot with the display and the writable disk attached
make run-debug  # boot headless with a GDB stub on port 1234
make test       # automated boot test: boots headless and checks serial output
make verify     # clean rebuild, then the full boot test
make clean      # remove everything under build/
```

All generated objects, ELF files, images, and ISO staging output are kept
under `build/`.

## Debugging

```bash
make run-debug                # terminal 1: QEMU waits on port 1234
gdb build/numos.bin -ex "target remote :1234" -ex "break kernel_main" -ex continue
```

See [BUILDING.md](BUILDING.md) for toolchain troubleshooting, VirtualBox
usage, and QEMU/GDB details.
