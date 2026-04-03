# ARM64 QEMU Virt Bring Up

NumOS now carries an ARM64 bring up path for the QEMU `virt` machine.

Current scope:

- boots on ARM64 through a native AArch64 entry path
- clears `.bss`
- sets `VBAR_EL1`
- initializes PL011 serial on `0x09000000`
- prints early boot status over serial

What is still missing:

- MMU and page tables
- storage and filesystem bring up
- user mode and syscall entry
- Raspberry Pi specific drivers

Build command:

```bash
make NUMOS_ARCH=arm64 kernel
```

Run command:

```bash
make NUMOS_ARCH=arm64 run
```

Expected serial output includes:

- `NumOS ARM64 bring up`
- current core ID
- current exception level
- generic timer frequency

This path is meant for early ARM64 development. The full Raspberry Pi port plan still lives in `docs/PORTING_RPI5_ARM64.md`.
