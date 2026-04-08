# NumOS

NumOS is a small 64 bit operating system project written in C, assembly, and a small amount of Python for build tooling.

Current repo version:

- `v0.0.0`

Current focus:

- x86_64 PC boot through GRUB Multiboot2
- arm64 QEMU `virt` serial bring up
- a freestanding kernel with paging, interrupts, scheduling, ELF loading, and syscalls
- a small userland with native tools such as `shell`, `edit`, `mk`, `pkg`, `connect`, and `proc`
- FAT32 based image creation and partition population flows

ARM64 planning for Raspberry Pi still lives in `docs/PORTING_RPI5_ARM64.md`. The current QEMU bring up notes live in `docs/ARM64_QEMU_VIRT.md`.

## Status

What works today:

- kernel boots on x86_64 through GRUB
- kernel and user runtimes use stack protection
- ELF64 `ET_EXEC` and `ET_DYN` loading works
- the kernel can unpack `numloss` compressed ELF payloads before loading them
- user processes run in ring 3
- basic threads and TLS work in user space
- FAT32 and ramdisk paths are present
- VGA, VESA, and framebuffer console paths exist
- e1000 networking path includes DHCP, ARP, IPv4, and ICMP echo
- build creates a bootable ISO and a FAT32 disk image

What is still incomplete:

- no full hosted libc
- no `fork`
- SMP path is not production safe yet
- networking does not include TCP or sockets
- USB support is limited to controller and port inspection
- ARM64 is early bring up only. The current path is serial first on QEMU `virt`

## Host Requirements

Required for a full x86_64 build:

- `make`
- `nasm` or `yasm`
- `x86_64-elf-gcc`, `gcc`, or `clang`
- `x86_64-elf-ld`, `ld`, or `ld.lld`
- `grub-mkrescue`
- `xorriso`
- `mtools`
- `qemu-system-x86_64` for local boot tests
- `python3`
- `gdb` or `gdb-multiarch` for source-level debug work

Required for the ARM64 bring up path:

- `make`
- `aarch64-none-elf-gcc` or another usable AArch64 freestanding compiler
- `aarch64-none-elf-ld` or another usable AArch64 linker
- `qemu-system-aarch64` for local boot tests
- `gdb-multiarch` recommended for ARM64 debug sessions

Check your host from the repo:

```bash
python3 tools/check_host_deps.py --arch x86_64
python3 tools/check_host_deps.py --arch arm64
```

Platform notes:

- Linux and other Unix hosts use the Makefiles directly.
- macOS needs GNU Make, usually as `gmake`, plus the other host tools from Homebrew.
- Windows builds are supported through WSL2. Run the Linux dependency commands inside WSL and build from the Linux path.

Ubuntu or Debian example:

```bash
sudo apt update
sudo apt install nasm qemu-system-x86 qemu-system-arm grub-common grub-pc-bin xorriso mtools parted dosfstools python3 gcc-aarch64-linux-gnu binutils-aarch64-linux-gnu gdb gdb-multiarch texinfo curl
```

You still need a usable cross or freestanding compiler setup. The helper script in `tools/build-cross-compiler.sh` exists for that path.

## Quick Start

Show the active architecture and support state:

```bash
make arch-status
```

Build the ARM64 preview kernel:

```bash
make NUMOS_ARCH=arm64 kernel
```

Run the ARM64 preview in QEMU `virt`:

```bash
make NUMOS_ARCH=arm64 run
```

Build the userland, kernel, disk image, and ISO:

```bash
make all
```

Run in QEMU:

```bash
make run
```

Run the unit tests that exist in this repo:

```bash
python3 -m unittest discover -s tests -v
```

Fetch the pinned GCC and binutils source archives used by the cross compiler helper:

```bash
make toolchain-source
```

## Common Build Targets

Use these targets most often:

- `make all`, full build
- `make iso`, build the bootable ISO
- `make run`, boot in QEMU
- `make debug`, boot QEMU with a GDB stub
- `make gdb-script`, generate `build/numos.gdb`
- `make gdb`, start GDB with the generated script
- `make clean`, remove build output
- `make arch-status`, print current target settings
- `make deps-check`, audit host dependencies for the active architecture

The default supported target is:

- `NUMOS_ARCH=x86_64`
- `NUMOS_MACHINE=pc`

The tree now carries an ARM64 serial bring up path for QEMU `virt`. Full storage, MMU, and user mode support are still in progress.

## Debug Flow

Start the debug target:

```bash
make debug
```

In another terminal:

```bash
make gdb-script
gdb -x build/numos.gdb
```

Or let the Makefile open GDB for you:

```bash
make gdb
```

The default debug port is `1234`. Override it with `NUMOS_DEBUG_PORT=<port>`.

## Disk Image And Partition Flow

The build creates `build/disk.img` with a fixed FAT32 layout and staged payloads from:

- `build/user`
- `build/stage/boot`
- `build/stage/run`
- `user/files/bin`
- `user/files/home`
- `user/include/syscalls.h`

Preview partitioning commands for an image or device:

```bash
make partition PART_TARGET=build/disk.img
```

Apply the partition table and format:

```bash
make partition PART_TARGET=build/disk.img PART_APPLY=1 PART_FORMAT=1
```

Populate a FAT32 partition with NumOS files:

```bash
make partition PART_TARGET=build/disk.img PART_APPLY=1 PART_FORMAT=1 PART_POPULATE=1
```

List host block devices:

```bash
make partition-list
```

## Native Userland

The image currently stages these user tools:

- `shell`, command runner and script launcher
- `edit`, simple text editor
- `mk`, small build helper
- `pkg`, staged package installer
- `proc`, process inspection
- `connect`, DHCP, ping, TCP, HTTP, TLS, and HTTPS checks
- `tcp`, legacy IPv4 TCP connect checks and plain HTTP GET
- `see`, file viewer
- `usb`, USB controller and port inspection
- `install`, native ATA disk installer
- staged non-init `.elf` files are packed with `numloss` by default when this makes them smaller

Example flow after boot:

- `pkg list`, inspect staged packages in `/run`
- `pkg install OCLDEV`, install the sample OCL development package
- `pkg http://203.0.113.10/packages/OCLDEV.PKG`, download a remote package into `/run` and install it
- `pkg kernel /run/KERN0002.BIN`, stage a new kernel with fallback boot metadata
- `mk`, run the default target from `/home/BUILD.MK`

Disable host-side ELF packing for a disk build:

```bash
make disk NUMOS_PACK_USER_ELF=0
```

Remote URL installs run inside NumOS. Start networking first with `connect --dhcp`. URL hosts currently need an IPv4 literal address.

## Kernel Update Flow

Installed NumOS disks now stage immutable kernel artifacts in `/boot`:

- `/boot/kern0001.bin`, known-good kernel
- `/boot/kern0002.bin`, newly staged kernel
- `/boot/boot.cfg`, default and fallback selection
- `/boot/status.cfg`, boot state marker
- `/boot/grub/grubenv`, one-shot pending state for GRUB fallback

Inside NumOS you can stage a new kernel from a local file or a direct IPv4 HTTP or HTTPS URL:

```bash
pkg kernel /run/KERN0002.BIN
pkg kernel http://203.0.113.10/kern0002.bin reboot
```

The kernel updater never overwrites the running kernel in place. It copies the new kernel to a fresh `/boot/kernNNNN.bin`, updates `/boot/boot.cfg`, and marks `/boot/status.cfg` as `pending`. Early kernel init rewrites `/boot/status.cfg` to `success` after the filesystem mounts. If the pending kernel fails before then, the installed GRUB path retries once and then falls back to the previous kernel on the next boot.

For remote kernel updates, bring networking up first:

```bash
connect --dhcp
pkg kernel http://203.0.113.10/kern0002.bin reboot
```

Remote kernel URLs currently need an IPv4 literal host, like the package downloader.

## USB And VirtualBox

NumOS now includes a native USB inspection tool:

```bash
usb
usb controllers
usb ports 0
```

This reports detected USB controllers, PCI location, and per-port state exposed by the current USB driver path.

VirtualBox USB passthrough and VirtualBox shared folders are different features:

- USB passthrough appears as a USB controller and device inside the guest. Enable a USB controller in the VM, add a USB filter for your device, then use `usb` inside NumOS to see what the guest can detect.
- Shared folders do not appear as USB devices. They need a dedicated VirtualBox guest driver and filesystem implementation. NumOS does not ship a `vboxsf` style guest driver yet, so adding a shared folder in VirtualBox will not make a new disk or USB device appear inside NumOS.

Fetch a package from your website into the host staging area before `make disk` or `make iso`:

```bash
python3 tools/download_pkg.py https://example.com/packages/OCLDEV.PKG
```

Or use the Make helper:

```bash
make pkg-download PKG_URL=https://example.com/packages/OCLDEV.PKG
```

The downloader stores the `.PKG` manifest and every `copy` source file in `build/stage/run`, which becomes `/run` inside the image.

## Repository Layout

Key directories:

- `Include`, kernel and driver headers
- `src`, kernel, driver, filesystem, and boot sources
- `user`, user runtime, linker scripts, and native programs
- `tools`, host-side build and disk helpers
- `tests`, Python regression tests for tooling
- `docs`, status notes and porting plans
- `preboot`, GRUB configuration
- `third_party`, vendored source bundles for the C library reference tree and GCC toolchain sources

## Verification

Current local verification path:

- `make all`
- `python3 -m unittest discover -s tests -v`

There is no `make test` target in the current tree.

## Known Limits

NumOS is an experimental OS project. Treat the disk and partition helpers with care when targeting real devices. Dry run is the default for the partition helper for this reason.
