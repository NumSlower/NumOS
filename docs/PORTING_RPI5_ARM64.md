# Raspberry Pi 5 ARM64 Port Plan

NumOS already supports AMD64 and Intel x86-64.

Raspberry Pi 5 support is a separate ARM64 port.

The current tree does not boot on Pi 5 because these pieces are x86-64 only:

- `src/boot/*.asm` uses NASM and x86-64 entry code.
- `src/cpu/x86/*` owns GDT, IDT, paging, APIC, FPU, and TSS setup.
- `src/drivers/pic.c`, `src/drivers/ata.c`, and PS/2 keyboard paths assume PC hardware.
- `user/runtime/entry.asm`, `src/kernel/syscall.c`, and the ELF loader assume the x86-64 ABI.
- The build boots through GRUB Multiboot2 on a PC style machine.

Recommended port order:

1. Bring up a serial only ARM64 kernel.
   - Add a new `src/cpu/arm64/` tree.
   - Add an ARM64 linker script and entry path.
   - Pick one boot path for early bring up. U-Boot, EDK2, or a direct board specific loader.
   - Print early boot logs to a serial console before framebuffer work.

2. Replace x86-64 CPU setup with ARM64 setup.
   - Add exception vectors.
   - Add EL1 startup code.
   - Add MMU and page table setup.
   - Add timer support.
   - Add interrupt controller support.

3. Rebuild the platform layer around Pi 5 hardware.
   - Replace PIC and APIC logic with the ARM interrupt path.
   - Replace ATA and PS/2 assumptions.
   - Start with serial, timer, framebuffer, and storage.

4. Port user space.
   - Add an ARM64 `_start` in `user/runtime/entry.asm`.
   - Add ARM64 syscall entry and return handling.
   - Extend ELF loading for `EM_AARCH64`.
   - Add ARM64 linker settings for user ELFs.

5. Add an ARM64 run target.
   - Add `qemu-system-aarch64` support for development.
   - Add a real hardware image path for Raspberry Pi 5 testing.

Suggested first milestone:

- Boot to serial.
- Initialize memory.
- Handle timer interrupts.
- Enter the scheduler.
- Run one kernel thread.

Suggested second milestone:

- Load one static ARM64 user ELF.
- Run the shell over serial.
- Add framebuffer after the serial path is stable.
