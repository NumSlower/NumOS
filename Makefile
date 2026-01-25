################################################################################
#  KERNEL-ONLY MAKEFILE FOR NumOS (No Filesystem/Disk Support)
################################################################################

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(findstring Linux,$(UNAME_S)), Linux)
    OS_TYPE := linux
else ifeq ($(findstring Darwin,$(UNAME_S)), Darwin)
    OS_TYPE := mac
else
    OS_TYPE := windows
endif

# ==============================
#  TOOLCHAIN
# ==============================
AS := nasm
CC := x86_64-elf-gcc
LD := x86_64-elf-ld

# ==============================
#  DIRECTORIES
# ==============================
SRC_DIR := src
BUILD_DIR := build
BUILD_KERNEL := $(BUILD_DIR)/kernel
ISO_DIR := $(BUILD_DIR)/iso
GRUB_DIR := $(ISO_DIR)/boot/grub

# ==============================
#  FLAGS
# ==============================
ASFLAGS := -f elf64

KERNEL_CFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-pic \
                 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
                 -Wall -Wextra -c -IInclude -O2

LDFLAGS := -T linker.ld -nostdlib --nmagic

# ==============================
#  SOURCE FILES (Excluding FS/Disk)
# ==============================
ASM_SOURCES := $(wildcard $(SRC_DIR)/boot/*.asm)

KERNEL_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/*.c) \
                    $(wildcard $(SRC_DIR)/drivers/*.c) \
                    $(wildcard $(SRC_DIR)/cpu/x86/*.c)

ASM_OBJECTS := $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_KERNEL)/boot/%.o,$(ASM_SOURCES))

KERNEL_C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL)/%.o,$(KERNEL_C_SOURCES))

ALL_KERNEL_OBJECTS := $(ASM_OBJECTS) $(KERNEL_C_OBJECTS)

# ==============================
#  ARTIFACTS
# ==============================
KERNEL := $(BUILD_DIR)/kernel.bin
ISO_FILE := NumOS.iso

# ==============================
#  DEFAULT TARGET
# ==============================
.PHONY: all
all: kernel

# ==============================
#  KERNEL BUILD
# ==============================
.PHONY: kernel
kernel: $(KERNEL)

$(BUILD_KERNEL)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@echo "[AS] $<"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_KERNEL)/%.o: $(SRC_DIR)/%.c
	@echo "[CC] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) $< -o $@

$(KERNEL): $(ALL_KERNEL_OBJECTS)
	@echo "[LD] Linking kernel..."
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo "[OK] Kernel built: $(KERNEL)"

# ==============================
#  ISO IMAGE (GRUB)
# ==============================
.PHONY: iso
iso: $(ISO_FILE)

$(ISO_FILE): $(KERNEL)
	@echo "[ISO] Creating bootable ISO image..."
	@mkdir -p $(GRUB_DIR)
	@cp $(KERNEL) $(ISO_DIR)/boot/kernel.bin
	@if [ ! -f $(GRUB_DIR)/grub.cfg ]; then \
		echo "menuentry \"NumOS\" {" > $(GRUB_DIR)/grub.cfg; \
		echo "    multiboot2 /boot/kernel.bin" >> $(GRUB_DIR)/grub.cfg; \
		echo "    boot" >> $(GRUB_DIR)/grub.cfg; \
		echo "}" >> $(GRUB_DIR)/grub.cfg; \
	fi
	@grub-mkrescue -o $(ISO_FILE) $(ISO_DIR) 2>/dev/null || \
		(echo "[ERROR] grub-mkrescue not found. Install grub-pc-bin and xorriso" && false)
	@echo "[OK] ISO created: $(ISO_FILE)"

# ==============================
#  RUN IN QEMU
# ==============================
.PHONY: run
run: iso
	@echo "[QEMU] Starting NumOS..."
	@qemu-system-x86_64 -m 128M \
		-cdrom $(ISO_FILE) \
		-serial stdio

# ==============================
#  DEBUG WITH GDB
# ==============================
.PHONY: debug
debug: iso
	@echo "[QEMU] Starting NumOS with GDB support..."
	@echo "In another terminal run: gdb build/kernel.bin"
	@echo "Then: (gdb) target remote localhost:1234"
	@echo "      (gdb) break kernel_main"
	@echo "      (gdb) continue"
	@qemu-system-x86_64 -m 128M \
		-cdrom $(ISO_FILE) \
		-serial stdio \
		-s -S

.PHONY: run-nographic
run-nographic: iso
	@echo "=== Starting NumOS in QEMU (no graphics) ==="
	qemu-system-x86_64 -m 128M \
		-cdrom $(ISO_FILE) \
		-nographic

# ==============================
#  CLEAN
# ==============================
.PHONY: clean
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
	@echo "[OK] Clean complete"

.PHONY: distclean
distclean: clean
	@echo "[DISTCLEAN] Removing ISO image..."
	@rm -f $(ISO_FILE)
	@echo "[OK] Distribution clean complete"

# ==============================
#  HELP
# ==============================
.PHONY: help
help:
	@echo "NumOS Kernel Build System (No Filesystem/Disk)"
	@echo "==============================================="
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build kernel (default)"
	@echo "  kernel      - Build kernel only"
	@echo "  iso         - Create bootable ISO image"
	@echo "  run         - Build ISO and run in QEMU"
	@echo "  debug       - Build and run with GDB support"
	@echo "  clean       - Remove build artifacts"
	@echo "  distclean   - Remove everything including images"
	@echo ""
	@echo "Current OS: $(OS_TYPE)"