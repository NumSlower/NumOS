################################################################################
#  KERNEL-ONLY MAKEFILE FOR NumOS
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

# ==============================
#  FLAGS
# ==============================
ASFLAGS := -f elf64

KERNEL_CFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-pic \
                 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
                 -Wall -Wextra -c -IInclude -O2

LDFLAGS := -T linker.ld -nostdlib --nmagic

# ==============================
#  SOURCE FILES
# ==============================
ASM_SOURCES := $(wildcard $(SRC_DIR)/boot/*.asm)

KERNEL_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/kmain.c) \
                    $(wildcard $(SRC_DIR)/kernel/kernel.c) \
                    $(wildcard $(SRC_DIR)/drivers/*.c) \
                    $(wildcard $(SRC_DIR)/cpu/x86/*.c) \
                    $(wildcard $(SRC_DIR)/fs/*.c)

ASM_OBJECTS := $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_KERNEL)/boot/%.o,$(ASM_SOURCES))

KERNEL_C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL)/%.o,$(KERNEL_C_SOURCES))

ALL_KERNEL_OBJECTS := $(ASM_OBJECTS) $(KERNEL_C_OBJECTS)

# ==============================
#  ARTIFACTS
# ==============================
KERNEL := $(BUILD_DIR)/kernel.bin
DISK_IMG := NumOS.img

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
#  DISK IMAGE (Linux only)
# ==============================
.PHONY: disk
disk: kernel
	@echo "=== Creating bootable disk image ==="
	@if [ "$(OS_TYPE)" != "linux" ]; then \
		echo "Disk image creation only supported on Linux"; \
		echo "Use build_and_deploy.sh script instead"; \
		exit 1; \
	fi
	@./build_and_deploy.sh

# ==============================
#  RUN IN QEMU
# ==============================
.PHONY: run
run: disk
	@echo "=== Starting NumOS in QEMU ==="
	qemu-system-x86_64 -m 128M \
		-drive file=$(DISK_IMG),format=raw \
		-serial stdio

# ==============================
#  DEBUG WITH GDB
# ==============================
.PHONY: debug
debug: disk
	@echo "=== Starting NumOS with GDB ==="
	qemu-system-x86_64 -m 128M \
		-drive file=$(DISK_IMG),format=raw \
		-serial stdio \
		-s -S

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
	@echo "[DISTCLEAN] Removing disk image..."
	@rm -f $(DISK_IMG)
	@echo "[OK] Distribution clean complete"

# ==============================
#  HELP
# ==============================
.PHONY: help
help:
	@echo "NumOS Kernel Build System"
	@echo "========================="
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build kernel (default)"
	@echo "  kernel    - Build kernel only"
	@echo "  disk      - Create bootable disk image (Linux only)"
	@echo "  run       - Build and run in QEMU"
	@echo "  debug     - Build and run with GDB support"
	@echo "  clean     - Remove build artifacts"
	@echo "  distclean - Remove everything including disk image"
	@echo ""
	@echo "On Linux, use: make disk && make run"
	@echo "On other systems: Build with 'make all', then use provided scripts"