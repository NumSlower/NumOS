# NumOS

A 64-bit operating system kernel written from scratch in C and Assembly, featuring custom memory management, FAT32 filesystem, and hardware drivers.

![NumOS Version](https://img.shields.io/badge/version-2.5-blue)
![Architecture](https://img.shields.io/badge/architecture-x86__64-green)
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
- **VGA Text Mode**: 80x25 color text display with cursor support
- **Keyboard**: PS/2 keyboard with scan code translation and buffer
- **Timer**: PIT-based timer with configurable frequency (18Hz - 1000Hz)
- **Disk**: ATA/IDE PIO driver with sector caching
  - Read/write support
  - Multi-sector operations
  - Cache management

### Filesystem
- **FAT32**: Full FAT32 implementation
  - Mount/unmount operations
  - File creation, reading, writing
  - Directory listing
  - Cluster chain management
  - Automatic filesystem creation if needed

### Kernel Shell
- **Interactive Command Line**: Built-in kernel shell
  - File operations (cat, write, ls)
  - System information (sysinfo, heap, disk)
  - System control (shutdown, reboot)

## 🏗️ Architecture

```
┌─────────────────────────────────────┐
│         Kernel (Ring 0)             │
│  ┌──────────────────────────────┐   │
│  │   Command Shell              │   │
│  │  - Built-in kernel commands  │   │
│  │  - File operations           │   │
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │   Memory Management          │   │
│  │  - Paging (4-level)          │   │
│  │  - Heap allocator            │   │
│  │  - Physical memory manager   │   │
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │   Filesystem (FAT32)         │   │
│  │  - File I/O operations       │   │
│  │  - Directory management      │   │
│  └──────────────────────────────┘   │
│  ┌──────────────────────────────┐   │
│  │   Device Drivers             │   │
│  │  - VGA, Keyboard, Timer      │   │
│  │  - ATA/IDE Disk              │   │
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

# Create bootable disk image (Linux only)
make disk

# Build and run in QEMU
make run

# Build and debug with GDB
make debug
```

### Project Structure

```
NumOS/
├── Include/              # Header files
│   ├── cpu/             # CPU-related (GDT, IDT, paging, heap)
│   ├── drivers/         # Device drivers
│   ├── fs/              # Filesystem headers
│   ├── kernel/          # Kernel core
│   └── lib/             # Standard library implementations
├── src/                 # Source files
│   ├── boot/            # Boot code (assembly)
│   ├── cpu/x86/         # x86-64 specific code
│   ├── drivers/         # Device drivers
│   ├── fs/              # FAT32 implementation
│   └── kernel/          # Kernel main and utilities
├── preboot/             # GRUB configuration
├── build/               # Build output directory
├── Makefile             # Main build system
└── linker.ld            # Linker script
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
   - Storage: Attach `NumOS.img` as hard disk
2. Boot the VM

### Real Hardware (Advanced)
⚠️ **Use at your own risk!**

```bash
# Write to USB drive (replace /dev/sdX with your device)
sudo dd if=NumOS.img of=/dev/sdX bs=4M status=progress
sudo sync
```

## 🎮 Kernel Shell Commands

```
help        - Show available commands
clear       - Clear the screen
ls          - List files in current directory
cat <file>  - Display file contents
write <file> <text> - Write text to file
sysinfo     - Show system information
heap        - Display heap statistics
disk        - Show disk information
reboot      - Reboot the system
shutdown    - Shutdown the system
```

## 🔧 Development

### Adding a New Driver

1. Create header in `Include/drivers/mydriver.h`
2. Implement in `src/drivers/mydriver.c`
3. Initialize in `kernel_init()` in `src/kernel/kmain.c`
4. Makefile will automatically pick it up

### Debugging Tips

- Use `vga_writestring()` for kernel debugging
- Enable serial output in QEMU: `-serial stdio`
- Use GDB with `make debug` for step-through debugging
- Check memory with `print_memory()` utility
- Monitor heap with `heap_print_stats()`

## 📊 Memory Map

```
0x0000000000000000 - 0x00000000000FFFFF  Low memory (1MB)
0x0000000000100000 - 0x00000000003FFFFF  Kernel code/data (identity mapped)
0xFFFFFFFF80000000 - 0xFFFFFFFF803FFFFF  Kernel virtual base
0xFFFFFFFF90000000 - 0xFFFFFFFF93FFFFFF  Kernel heap (64MB)
```

## 🐛 Known Issues

- Limited to 128MB RAM in current configuration
- No networking support
- Basic single-core only (no SMP)
- Limited error recovery in some drivers
- FAT32 long filename support incomplete

## 🗺️ Roadmap

- [ ] Multi-tasking and scheduling
- [ ] User space support
- [ ] System calls and IPC
- [ ] Virtual filesystem layer (VFS)
- [ ] Additional filesystems (ext2)
- [ ] Network stack (TCP/IP)
- [ ] USB support
- [ ] Graphics mode (VESA/GOP)
- [ ] SMP support
- [ ] ACPI implementation

## 📖 Resources

- [OSDev Wiki](https://wiki.osdev.org/)
- [Intel 64 and IA-32 Architectures Software Developer Manuals](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)
- [AMD64 Architecture Programmer's Manual](https://www.amd.com/en/support/tech-docs)
- [FAT32 Specification](https://www.win.tue.nl/~aeb/linux/fs/fat/fat-1.html)

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