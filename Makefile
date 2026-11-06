# NumOS — 64-bit Operating System build system
# Master spec Phase 1: toolchain checks, build/ output layout, dependency
# generation, and all required targets. Runs inside the build environment
# documented in BUILDING.md (WSL2 Ubuntu).

CC      := x86_64-elf-gcc
LD      := x86_64-elf-ld
ASM     := nasm
QEMU    := qemu-system-x86_64
GRUB    := grub-mkrescue

BUILD   := build
OBJDIR  := $(BUILD)/obj
ISODIR  := $(BUILD)/iso
KERNEL  := $(BUILD)/numos.bin
ISO     := $(BUILD)/numos.iso
DISK    := $(BUILD)/disk.img

CFLAGS  := -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
           -nostartfiles -nodefaultlibs -Wall -Wextra -Werror \
           -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
           -MMD -MP -g
LDFLAGS := -T linker.ld -nostdlib
ASMFLAGS := -f elf64

C_SOURCES  := $(shell find src -type f -name '*.c' ! -path 'src/user/*' | sort)
ASM_SOURCES := $(shell find src -type f -name '*.asm' | sort)
USER_SRC   := src/user/init.c
USER_ELF   := $(BUILD)/user_init.elf
SHELL_SRC  := src/user/shell.c
SHELL_ELF  := $(BUILD)/shell.elf
SHELL_BLOB := $(OBJDIR)/user/shell_blob.o
USER_BLOB  := $(OBJDIR)/user/user_init_blob.o
C_OBJS   := $(patsubst src/%.c,$(OBJDIR)/%.o,$(C_SOURCES))
ASM_OBJS := $(patsubst src/%.asm,$(OBJDIR)/%.o,$(ASM_SOURCES))
ALL_OBJS := $(C_OBJS) $(ASM_OBJS) $(SHELL_BLOB) $(USER_BLOB)
DEPS     := $(C_OBJS:.o=.d)

REQUIRED_TOOLS := $(CC) $(LD) $(ASM) $(GRUB) xorriso $(QEMU)

.PHONY: all kernel userspace iso disk run run-text run-gui run-debug test verify clean check-tools

all: iso

check-tools:
	@ok=1; \
	for t in $(REQUIRED_TOOLS); do \
		command -v $$t >/dev/null 2>&1 || { echo "ERROR: required tool '$$t' not found in PATH"; ok=0; }; \
	done; \
	if [ $$ok -ne 1 ]; then \
		echo "NumOS requires the tools above. x86_64-elf-gcc/x86_64-elf-ld are built from"; \
		echo "source — see BUILDING.md for the full provisioning procedure (WSL2 Ubuntu)."; \
		exit 1; \
	fi

kernel: check-tools $(KERNEL)

userspace:
	@if [ ! -d userspace ]; then \
		echo "USERSPACE: not implemented (master spec Phase 11). Nothing to build."; \
		exit 1; \
	fi

iso: check-tools $(ISO)

disk: check-tools
	@mkdir -p $(BUILD)
	truncate -s 64M $(DISK)
	mformat -i $(DISK) -C -T 131072 -F ::
	@echo "Created FAT32 disk image: $(DISK)"

$(KERNEL): $(ALL_OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(ALL_OBJS)

$(SHELL_ELF): $(SHELL_SRC) user.ld
	@mkdir -p $(dir $@)
	$(CC) -m64 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
		-nostdlib -nostartfiles -mno-red-zone -mno-sse -mno-mmx \
		-Wall -Wextra -Werror -Iinclude -T user.ld -o $@ $<

$(SHELL_BLOB): $(SHELL_ELF)
	@mkdir -p $(dir $@)
	cd $(BUILD) && ld -r -b binary -o obj/user/shell_blob.o shell.elf

$(USER_ELF) $(USER_BLOB): $(USER_SRC) user.ld
	@mkdir -p $(dir $@)
	$(CC) -m64 -ffreestanding -fno-pic -fno-stack-protector -fno-builtin \
		-nostdlib -nostartfiles -mno-red-zone -mno-sse -mno-mmx \
		-Wall -Wextra -Werror -T user.ld -o $(USER_ELF) $<
	cd $(BUILD) && ld -r -b binary -o obj/user/user_init_blob.o user_init.elf

$(OBJDIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -Iinclude -c -o $@ $<

$(OBJDIR)/%.o: src/%.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

$(ISO): $(KERNEL) src/boot/grub/grub.cfg
	@mkdir -p $(ISODIR)/boot/grub
	cp $(KERNEL) $(ISODIR)/boot/numos.bin
	cp src/boot/grub/grub.cfg $(ISODIR)/boot/grub/grub.cfg
	$(GRUB) -o $@ $(ISODIR)

run: iso
	$(QEMU) -cdrom $(ISO) -m 512M -serial stdio

run-text: iso
	$(QEMU) -cdrom $(ISO) -m 512M -display none -serial stdio -no-reboot

run-gui: iso disk
	$(QEMU) -cdrom $(ISO) -drive file=$(DISK),format=raw,if=ide -m 512M -boot d -serial stdio

run-debug: iso
	$(QEMU) -cdrom $(ISO) -m 512M -display none -serial stdio -no-reboot -s -S

test: iso disk
	-mmd -i $(DISK) ::/BIN 2>/dev/null
	-mcopy -i $(DISK) $(USER_ELF) ::/BIN/INIT.ELF 2>/dev/null
	@rm -f $(BUILD)/boot-test.log
	timeout 45 $(QEMU) -cdrom $(ISO) -drive file=$(DISK),format=raw,if=ide -m 512M -boot d \
		-display none -no-reboot -serial stdio > $(BUILD)/boot-test.log 2>/dev/null || true
	@echo "--- Serial output (tail) ---"
	@tail -n 30 $(BUILD)/boot-test.log || { echo "TEST FAILED: no serial output"; exit 1; }
	@if grep -q "CPU EXCEPTION" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: CPU exception during boot"; exit 1; fi
	@if grep -q "\[FAIL\]" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: a required subsystem failed to initialize"; exit 1; fi
	@if ! grep -q "hello from session 0" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: session-0 shell echo/env failed"; exit 1; fi
	@if ! grep -q 'hi shell and $$literal' $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: variable expansion or literal escape failed"; exit 1; fi
	@if ! grep -q "and-chain-ok" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: && conditional failed"; exit 1; fi
	@if ! grep -q "or-chain-ok" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: || conditional failed"; exit 1; fi
	@if ! grep -q 's0 $$ data' $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: builtin > redirection or cat failed"; exit 1; fi
	@if ! grep -q "OUT.TXT FILE 5" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: stat/ls on redirected file failed"; exit 1; fi
	@if ! grep -q "1 0 running sh" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: ps failed"; exit 1; fi
	@if ! grep -q "exited with code 42" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: exec/wait of /BIN/init.ELF failed"; exit 1; fi
	@if ! grep -q "exit=42" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: shell did not report child exit status"; exit 1; fi
	@if ! grep -q "badcmd: not found" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: unknown command handling failed"; exit 1; fi
	@if ! grep -q "[0-9]  badcmd" $(BUILD)/boot-test.log; then \
		echo "BOOT TEST FAILED: history failed"; exit 1; fi
	@echo "BOOT TEST PASSED"

verify: clean
	$(MAKE) kernel
	$(MAKE) iso
	$(MAKE) test

clean:
	rm -rf $(BUILD)

-include $(DEPS)
