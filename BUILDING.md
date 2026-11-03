# Building NumOS

## Prerequisites

NumOS is a 64-bit x86_64 operating system kernel that requires a cross-compilation toolchain.

### Required Tools

| Tool | Purpose |
|------|---------|
| `x86_64-elf-gcc` | Cross-compiler for freestanding x86_64 code |
| `x86_64-elf-ld` | Cross-linker |
| `x86_64-elf-binutils` | Binary utilities (objdump, objcopy, etc.) |
| `nasm` | Netwide Assembler for x86_64 assembly |
| `grub-mkrescue` | Creates bootable ISO images |
| `xorriso` | Required by grub-mkrescue |
| `qemu-system-x86_64` | Emulator for testing |
| `make` | Build system |

---

## WSL2 Setup (Windows)

Since NumOS requires Linux-native tools, **WSL2 with Ubuntu** is the recommended build environment on Windows.

### Step 1: Install WSL2

Open PowerShell **as Administrator** and run:

```powershell
wsl --install -d Ubuntu
```

Restart your computer when prompted. On reboot, Ubuntu will finish installing and ask you to create a username and password.

### Step 2: Update System Packages

```bash
sudo apt update && sudo apt upgrade -y
```

### Step 3: Install Build Dependencies

```bash
sudo apt install -y build-essential nasm xorriso grub-pc-bin grub-common \
    mtools qemu-system-x86 git curl wget texinfo bison flex libgmp-dev \
    libmpfr-dev libmpc-dev
```

### Step 4: Build the x86_64-elf Cross-Compiler

The cross-compiler must be built from source. This takes 15–30 minutes.

```bash
# Set install prefix
export PREFIX="$HOME/opt/cross"
export TARGET=x86_64-elf
export PATH="$PREFIX/bin:$PATH"

mkdir -p "$PREFIX"
mkdir -p ~/src && cd ~/src

# Download sources
BINUTILS_VER=2.42
GCC_VER=14.1.0

wget https://ftp.gnu.org/gnu/binutils/binutils-$BINUTILS_VER.tar.xz
wget https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VER/gcc-$GCC_VER.tar.xz

# Build binutils
tar xf binutils-$BINUTILS_VER.tar.xz
mkdir build-binutils && cd build-binutils
../binutils-$BINUTILS_VER/configure --target=$TARGET --prefix="$PREFIX" \
    --with-sysroot --disable-nls --disable-werror
make -j$(nproc)
make install
cd ..

# Build GCC
tar xf gcc-$GCC_VER.tar.xz
mkdir build-gcc && cd build-gcc
../gcc-$GCC_VER/configure --target=$TARGET --prefix="$PREFIX" \
    --disable-nls --enable-languages=c --without-headers
make -j$(nproc) all-gcc all-target-libgcc
make install-gcc install-target-libgcc
cd ..
```

### Step 5: Add Cross-Compiler to PATH

Add this to your `~/.bashrc`:

```bash
export PATH="$HOME/opt/cross/bin:$PATH"
```

Then reload:

```bash
source ~/.bashrc
```

### Step 6: Verify Installation

```bash
x86_64-elf-gcc --version
x86_64-elf-ld --version
nasm --version
grub-mkrescue --version
qemu-system-x86_64 --version
```

All commands should print version info without errors.

---

## Building NumOS

### Navigate to the Project

If your project is on the Windows filesystem:

```bash
cd /mnt/c/Users/numso/OneDrive/Desktop/Project/NumOS
```

> **Tip**: For better build performance, copy the project into the WSL filesystem:
> ```bash
> cp -r /mnt/c/Users/numso/OneDrive/Desktop/Project/NumOS ~/NumOS
> cd ~/NumOS
> ```

### Build Commands

```bash
# Clean previous build
make clean

# Compile kernel binary
make

# Create bootable ISO
make iso

# Run in QEMU (graphical + serial output to terminal)
make run

# Run in QEMU with GDB debug stub (attach with gdb on port 1234)
make run-debug
```

### Expected Output

When running `make run`, QEMU will open a window showing:

```
NumOS v0.1 - 64-bit Kernel
===========================
Initializing Multiboot2 Info..... [OK]
Initializing Serial.............. [OK]
Initializing GDT................. [OK]
Initializing TSS................. [OK]
Initializing Paging.............. [OK]
...
Hello World!
```

Serial output is simultaneously printed to your terminal via `-serial stdio`.

---

## Running in VirtualBox

The generated ISO (`build/numos.iso`) is also bootable in VirtualBox:

1. Create a new VM:
   - **Type**: Other
   - **Version**: Other/Unknown (64-bit)
   - **Memory**: 256 MB
   - **Hard disk**: Do not add a virtual hard disk
2. Go to **Settings → System → Processor**: Enable PAE/NX
3. Go to **Settings → Storage**: Add `build/numos.iso` as an IDE optical disc
4. Go to **Settings → Serial Ports → Port 1**: Enable, set to **Raw File** mode, point to a log file
5. Start the VM

> **Note**: BGA (Bochs Graphics Adapter) is not available in VirtualBox and will show `[SKIP]`.

---

## Debugging with GDB

```bash
# Terminal 1: Start QEMU with debug stub
make run-debug

# Terminal 2: Attach GDB
gdb build/numos.bin -ex "target remote :1234" -ex "break kernel_main" -ex "continue"
```

Useful GDB commands:
- `info registers` — Show all registers
- `x/20i $rip` — Disassemble at current instruction
- `x/10gx $rsp` — Show stack contents
- `p/x *(registers_t*)$rdi` — Print interrupt frame

---

## Troubleshooting

### `x86_64-elf-gcc: command not found`
Ensure `$HOME/opt/cross/bin` is in your `$PATH`. Run `source ~/.bashrc`.

### `grub-mkrescue` hangs or fails
Install the BIOS GRUB modules: `sudo apt install grub-pc-bin`

### Triple fault on boot
Run with `-no-reboot` flag (already in Makefile). Check serial output for exception details. Attach GDB with `make run-debug`.

### `find: not found` error in Makefile
Ensure you're running in a Linux/WSL environment, not Windows CMD/PowerShell.
