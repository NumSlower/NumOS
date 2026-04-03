# ARM64 QEMU Virt Bring Up

NumOS now carries an ARM64 bring up path for the QEMU `virt` machine.

Current scope:

- boots on ARM64 through a native AArch64 entry path
- clears `.bss`
- sets `VBAR_EL1`
- initializes PL011 serial on `0x09000000`
- enables an identity mapped EL1 MMU setup for the QEMU `virt` RAM window
- finds the initrd from the device tree and mounts the FAT32 ramdisk
- validates and launches the ARM64 init ELF at `/bin/empty.elf` by default
- handles a small EL0 syscall set for file I/O, console I/O, uptime, sleep,
  framebuffer info, and exit
- supports an optional simple framebuffer path through QEMU `ramfb`

What is still missing:

- full per process address spaces
- process scheduler integration on ARM64
- most NumOS syscalls beyond the bootstrap set
- Raspberry Pi specific drivers

Build command:

```bash
make NUMOS_ARCH=arm64 kernel
```

Run command:

```bash
make NUMOS_ARCH=arm64 run
```

Framebuffer run command:

```bash
make NUMOS_ARCH=arm64 run-framebuffer
```

Expected serial output includes:

- `NumOS ARM64 bring up`
- `Storage: ramdisk FAT32 mounted`
- `User init launch: /bin/empty.elf`
- `Init exit code: ...`

This path is meant for early ARM64 development. The full Raspberry Pi port plan still lives in `docs/PORTING_RPI5_ARM64.md`.
