# NumOS

NumOS is a small 64 bit operating system project written in C, assembly, and a small amount of Python for build tooling.

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

Required for the ARM64 bring up path:

- `make`
- `aarch64-none-elf-gcc` or another usable AArch64 freestanding compiler
- `aarch64-none-elf-ld` or another usable AArch64 linker
- `qemu-system-aarch64` for local boot tests

Ubuntu or Debian example:

```bash
sudo apt update
sudo apt install nasm qemu-system-x86 grub-common grub-pc-bin xorriso mtools python3
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

## Common Build Targets

Use these targets most often:

- `make all`, full build
- `make iso`, build the bootable ISO
- `make run`, boot in QEMU
- `make debug`, boot QEMU with a GDB stub
- `make clean`, remove build output
- `make arch-status`, print current target settings

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
gdb build/kernel.bin
target remote localhost:1234
break kernel_main
continue
```

## Disk Image And Partition Flow

The build creates `build/disk.img` with a fixed FAT32 layout and staged payloads from:

- `build/user`
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
- `install`, native install helper
- staged non-init `.elf` files are packed with `numloss` by default when this makes them smaller

Example flow after boot:

- `pkg list`, inspect staged packages in `/run`
- `pkg install OCLDEV`, install the sample OCL development package
- `pkg http://203.0.113.10/packages/OCLDEV.PKG`, download a remote package into `/run` and install it
- `mk`, run the default target from `/home/BUILD.MK`

Disable host-side ELF packing for a disk build:

```bash
make disk NUMOS_PACK_USER_ELF=0
```

Remote URL installs run inside NumOS. Start networking first with `connect --dhcp`. URL hosts currently need an IPv4 literal address.

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

## Verification

Current local verification path:

- `make all`
- `python3 -m unittest discover -s tests -v`

There is no `make test` target in the current tree.

## Known Limits

NumOS is an experimental OS project. Treat the disk and partition helpers with care when targeting real devices. Dry run is the default for the partition helper for this reason.
