################################################################################
#  KERNEL-ONLY MAKEFILE FOR NumOS WITH INTEGRATED DISK CREATION
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
CC := $(shell command -v x86_64-elf-gcc || echo /usr/local/cross/bin/x86_64-elf-gcc)
LD := $(shell command -v x86_64-elf-ld || echo /usr/local/cross/bin/x86_64-elf-ld)

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
ISO_FILE := NumOS.iso
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
#  DISK IMAGE (RAW)
# ==============================
.PHONY: disk
disk: $(DISK_IMG)

$(DISK_IMG): $(KERNEL)
	@echo "[DISK] Creating bootable disk image..."
	@if [ "$(OS_TYPE)" = "linux" ]; then \
		echo "  Creating 64MB disk image..."; \
		dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 status=none 2>/dev/null; \
		echo "  Setting up partition table..."; \
		parted -s $(DISK_IMG) mklabel msdos 2>/dev/null; \
		parted -s $(DISK_IMG) mkpart primary fat32 1MiB 100% 2>/dev/null; \
		parted -s $(DISK_IMG) set 1 boot on 2>/dev/null; \
		echo "  Creating loop device..."; \
		LOOP_DEV=$$(sudo losetup -f --show -P $(DISK_IMG)); \
		echo "  Formatting partition as FAT32..."; \
		sudo mkfs.vfat -F 32 $${LOOP_DEV}p1 2>/dev/null; \
		echo "  Mounting partition..."; \
		mkdir -p /tmp/numos_mount; \
		sudo mount $${LOOP_DEV}p1 /tmp/numos_mount; \
		echo "  Installing GRUB..."; \
		sudo grub-install --target=i386-pc --boot-directory=/tmp/numos_mount/boot $${LOOP_DEV} 2>/dev/null; \
		sudo mkdir -p /tmp/numos_mount/boot/grub; \
		echo "menuentry \"NumOS\" {" | sudo tee /tmp/numos_mount/boot/grub/grub.cfg > /dev/null; \
		echo "    multiboot2 /boot/kernel.bin" | sudo tee -a /tmp/numos_mount/boot/grub/grub.cfg > /dev/null; \
		echo "    boot" | sudo tee -a /tmp/numos_mount/boot/grub/grub.cfg > /dev/null; \
		echo "}" | sudo tee -a /tmp/numos_mount/boot/grub/grub.cfg > /dev/null; \
		echo "  Copying kernel..."; \
		sudo cp $(KERNEL) /tmp/numos_mount/boot/kernel.bin; \
		echo "  Cleaning up..."; \
		sudo umount /tmp/numos_mount; \
		sudo losetup -d $${LOOP_DEV}; \
		rmdir /tmp/numos_mount 2>/dev/null || true; \
		echo "[OK] Disk image created: $(DISK_IMG)"; \
	else \
		echo "[ERROR] Disk image creation only supported on Linux"; \
		echo "        Use 'make iso' instead, or run on Linux"; \
		false; \
	fi

# ==============================
#  SIMPLE DISK (NO GRUB) - Works on any OS
# ==============================
.PHONY: simpledisk
simpledisk: $(KERNEL)
	@echo "[DISK] Creating simple disk image (no bootloader)..."
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 2>/dev/null
	@echo "[OK] Simple disk created: $(DISK_IMG)"
	@echo "     Note: This disk has no bootloader, use 'make iso' for bootable image"

# ==============================
#  RUN IN QEMU
# ==============================
.PHONY: run
run: iso
	@echo "[QEMU] Starting NumOS..."
	@qemu-system-x86_64 -m 128M \
		-cdrom $(ISO_FILE) \
		-serial stdio

.PHONY: run-disk
run-disk: disk
	@echo "[QEMU] Starting NumOS from disk..."
	@qemu-system-x86_64 -m 128M \
		-drive file=$(DISK_IMG),format=raw \
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

.PHONY: debug-disk
debug-disk: disk
	@echo "[QEMU] Starting NumOS from disk with GDB..."
	@qemu-system-x86_64 -m 128M \
		-drive file=$(DISK_IMG),format=raw \
		-serial stdio \
		-s -S

.PHONY: run-nographic
run-nographic: disk
	@echo "=== Starting NumOS in QEMU (no graphics) ==="
	qemu-system-x86_64 -m 128M \
		-drive file=$(DISK_IMG),format=raw \
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
	@echo "[DISTCLEAN] Removing disk and ISO images..."
	@rm -f $(DISK_IMG) $(ISO_FILE)
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
	@echo "  all         - Build kernel (default)"
	@echo "  kernel      - Build kernel only"
	@echo "  iso         - Create bootable ISO image (works on any OS)"
	@echo "  disk        - Create bootable disk image (Linux only, requires sudo)"
	@echo "  simpledisk  - Create simple disk without bootloader"
	@echo "  run         - Build ISO and run in QEMU"
	@echo "  run-disk    - Build disk and run in QEMU (Linux only)"
	@echo "  debug       - Build and run with GDB support (ISO)"
	@echo "  debug-disk  - Build and run with GDB support (disk)"
	@echo "  clean       - Remove build artifacts"
	@echo "  distclean   - Remove everything including images"
	@echo ""
	@echo "Recommended:"
	@echo "  make iso && make run     - Works on any OS"
	@echo ""
	@echo "Linux only:"
	@echo "  make disk && make run-disk"
	@echo ""
	@echo "Current OS: $(OS_TYPE)"