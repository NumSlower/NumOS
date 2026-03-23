# NumOS

A 64-bit operating system kernel written from scratch in C and Assembly, featuring custom memory management, hardware drivers, and an interactive scrollback system.

Current build support:
- AMD64 and Intel x86-64 PCs through GRUB Multiboot2
- Raspberry Pi 5 support is planned as a separate ARM64 port, see `docs/PORTING_RPI5_ARM64.md`

![NumOS Version](https://img.shields.io/badge/version-2.5-blue)
![Architecture](https://img.shields.io/badge/architecture-x86__64_current-green)
![License](https://img.shields.io/badge/license-MIT-orange)

## 🎯 Features

### Core Kernel
- **64-bit Long Mode**: Full x86-64 architecture support
- **Memory Management**: 
  - 4-level paging (PML4 → PDPT → PD → PT)
  - Virtual memory manager with region tracking
  - Physical memory manager with frame allocation
  - Dynamic heap allocator with memory guards
- **Interrupt Handling**: 
  - IDT with 256 entries
  - Exception handlers (divide by zero, page fault, GPF, etc.)
  - IRQ handling through PIC
- **GDT**: Proper segmentation for kernel code and data

### Drivers
- **VGA Text Mode**: 
  - 80x25 color text display with cursor support
  - **200-line scrollback buffer** for reviewing output
  - Interactive scroll mode with arrow key navigation
  - Real-time scroll position indicator
- **Keyboard**: 
  - PS/2 keyboard with scan code translation
  - Arrow key support for navigation
  - Buffered input system
- **Timer**: PIT-based timer with configurable frequency (18Hz - 1000Hz)

### User Interface
- **Interactive Scrollback System**: 
  - Browse through 200 lines of kernel output
  - Arrow key navigation (UP/DOWN or W/S)
  - Visual scroll position indicator
  - Smooth scrolling through boot messages and logs

## 🏗️ Architecture

```
┌─────────────────────────────────────┐
│         Kernel (Ring 0)             │
│  ┌──────────────────────────────┐   │
│  │   Scrollback System          │   │
│  │  - 200-line history buffer   │   │
│  │  - Arrow key navigation      │   │
│  │  - Position tracking         │   │
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │   Memory Management          │   │
│  │  - Paging (4-level)          │   │
│  │  - Heap allocator            │   │
│  │  - Physical memory manager   │   │
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │   Device Drivers             │   │
│  │  - VGA, Keyboard, Timer      │   │
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │   Hardware Abstraction       │   │
│  │  - GDT, IDT, PIC             │   │
│  │  - Port I/O                  │   │
│  └──────────────────────────────┘   │
└─────────────────────────────────────┘
            │ GRUB Multiboot2
┌─────────────────────────────────────┐
│         Hardware / QEMU             │
└─────────────────────────────────────┘
```

## 🛠️ Building

### Prerequisites

Current target:
- `NUMOS_ARCH=x86_64`
- `NUMOS_MACHINE=pc`
- `make arch-status` prints the active target and support state

**Cross-Compiler Toolchain:**
- `x86_64-elf-gcc` (cross-compiler)
- `x86_64-elf-ld` (linker)
- `nasm` (assembler)

**Build Tools:**
- `make`
- `grub-mkrescue` (for creating bootable ISO)
- `xorriso` (ISO creation dependency)

**Emulation:**
- `qemu-system-x86_64`

### Installation

#### Linux (Ubuntu/Debian)
```bash
# Install NASM
sudo apt install nasm

# Install QEMU
sudo apt install qemu-system-x86

# Install GRUB tools
sudo apt install grub-pc-bin grub-common xorriso mtools

# Build cross-compiler (or install pre-built from your distro)
# See: https://wiki.osdev.org/GCC_Cross-Compiler
```

#### macOS
```bash
# Install Homebrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install dependencies
brew install nasm qemu xorriso

# Build cross-compiler
brew install x86_64-elf-gcc x86_64-elf-binutils
```

### Compilation

```bash
# Build kernel
make all

# Create bootable ISO image
make iso

# Build and run in QEMU
make run

# Build and debug with GDB
make debug
```

### Partitioning a Drive or Image

```bash
# 1) List host drives
make partition-list

# 2) Dry run on a disk image (prints commands only)
make partition PART_TARGET=build/disk.img

# 3) Apply changes and format partition 1 as FAT32
make partition PART_TARGET=build/disk.img PART_APPLY=1 PART_FORMAT=1

# 4) Example for SSD or micro SD (replace with your device path)
make partition PART_TARGET=/dev/sdX PART_APPLY=1 PART_FORMAT=1

# 5) Boot NumOS from a partitioned image
make run-partition PART_TARGET=build/disk.img
```

Notes:
- Default layout uses one partition from `1MiB` to `100%`
- Default partition table is `gpt`
- Use `PART_TABLE=msdos` if you need MBR
- Use `PART_FS=ext4` for ext4 formatting
- `PART_POPULATE=1` writes NumOS files into FAT32 partition 1
- GRUB mode choice in `run-partition` persists in `/run/grubenv` on the disk image
- Keep `PART_APPLY=0` for a safe preview

### Project Structure

```
NumOS/
├── Include/              # Header files
│   ├── cpu/             # CPU-related (GDT, IDT, paging, heap)
│   ├── drivers/         # Device drivers
│   ├── kernel/          # Kernel core
│   └── lib/             # Standard library implementations
├── src/                 # Source files
│   ├── boot/            # Boot code (assembly)
│   ├── cpu/x86/         # x86-64 specific code
│   ├── drivers/         # Device drivers
│   └── kernel/          # Kernel main and utilities
├── preboot/             # GRUB configuration
├── build/               # Build output directory
├── Makefile             # Main build system
└── linker/kernel.ld     # Linker script
```

## 🚀 Running

### QEMU (Recommended)
```bash
# Standard run
make run

# With debugging
make debug
# In another terminal:
gdb build/kernel.bin
(gdb) target remote localhost:1234
(gdb) break kernel_main
(gdb) continue
```

### VirtualBox
1. Create a new VM with:
   - Type: Other/Unknown (64-bit)
   - Memory: 128MB minimum
   - Display: Use `VBoxVGA`
   - Storage: Attach `NumOS.iso` as optical drive only
   - Do not attach `disk.img` separately. The ISO already includes it as a multiboot ramdisk module.
2. Boot the VM

### Real Hardware (Advanced)
⚠️ **Use at your own risk!**

```bash
# Write ISO to USB drive (replace /dev/sdX with your device)
sudo dd if=NumOS.iso of=/dev/sdX bs=4M status=progress
sudo sync
```

## 🎮 Using the Scrollback System

When NumOS boots, it automatically displays system tests and then enters scroll mode.

### Navigation Controls
- **↑ (Up Arrow)** or **W**: Scroll backward in history (see older lines)
- **↓ (Down Arrow)** or **S**: Scroll forward toward present (see newer lines)
- **Q**: Exit scroll mode

### Features
- **200-line buffer**: Stores all kernel output for later review
- **Position indicator**: Top-right corner shows "SCROLL" with current line number
- **Smooth navigation**: Browse through boot messages, initialization logs, and test output
- **Help bar**: Bottom of screen shows navigation instructions

## 📊 Memory Map

```
0x0000000000000000 - 0x00000000000FFFFF  Low memory (1MB)
0x0000000000100000 - 0x00000000003FFFFF  Kernel code/data (identity mapped)
0xFFFFFFFF80000000 - 0xFFFFFFFF803FFFFF  Kernel virtual base
0xFFFFFFFF90000000 - 0xFFFFFFFF93FFFFFF  Kernel heap (64MB)
```

## 🧪 System Tests

NumOS includes built-in tests for:
- **Memory Allocation**: kmalloc, kzalloc, kcalloc
- **Paging System**: Virtual memory allocation and management
- **Timer**: Delay accuracy and uptime tracking
- **Keyboard**: Interactive input testing

## 🐛 Known Issues

- No networking support
- No sound support
- No USB stack
- No NumOS specific libc or toolchain target yet
- No `fork`, user thread API, or TLS yet
- No graphical window system yet
- Not self hosting yet

## 🗺️ Roadmap

See `docs/OSDEV_CHECKLIST.md` for a phase by phase audit against the OSDev operating system roadmap.

## 🔧 Development

### Adding a New Driver

1. Create header in `Include/drivers/mydriver.h`
2. Implement in `src/drivers/mydriver.c`
3. Initialize in `kernel_init()` in `src/kernel/kmain.c`
4. Add to `KERNEL_C_SOURCES` in Makefile

### Debugging Tips

- Use `vga_writestring()` for kernel debugging output
- Enable serial output in QEMU: `-serial stdio`
- Use GDB with `make debug` for step-through debugging
- Check memory with `print_memory()` utility
- Monitor heap with `heap_print_stats()`
- Use scrollback (↑/↓) to review earlier debug messages

### Configuring Scrollback Buffer

Edit `src/drivers/vga.c`:
```c
#define SCROLLBACK_LINES 200  // Adjust buffer size
```

Recommended values:
- `100` = ~16 KB buffer
- `200` = ~32 KB buffer (default)
- `500` = ~80 KB buffer
- `1000` = ~160 KB buffer

## 📖 Resources

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD64 Architecture Programmer's Manual](https://www.amd.com/en/support/tech-docs)
- [X86-64 Overview](https://wiki.osdev.org/X86-64)

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Make your changes with clear commit messages
4. Test thoroughly in QEMU
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see LICENSE file for details.

## 👏 Acknowledgments

- OSDev community for excellent documentation
- GRUB developers for the bootloader
- QEMU team for the emulator
- Everyone who contributed to the x86-64 specifications

## 📞 Contact

For questions or issues, please open an issue on the repository.

---

**Built with ❤️ for learning OS development**
