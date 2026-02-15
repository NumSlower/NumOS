################################################################################
#  NUMOS MAKEFILE WITH FAT32 FILESYSTEM SUPPORT + USER SPACE
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
TOOLS_DIR := tools
USER_SPACE_DIR := user_space

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

KERNEL_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/*.c) \
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
DISK_IMAGE := $(BUILD_DIR)/disk.img

# ==============================
#  DEFAULT TARGET
# ==============================
.PHONY: all
all: kernel disk

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
#  DISK IMAGE CREATION
#   Depends on user_space so main.elf is guaranteed to exist.
#   Always rebuilds the disk image (PHONY) because create_disk_fixed.py
#   embeds the current main.elf.
# ==============================
.PHONY: disk
disk: 
	@echo "[DISK] Creating FAT32 disk image..."
	@mkdir -p $(BUILD_DIR)
	@python3 $(TOOLS_DIR)/create_disk_fixed.py $(DISK_IMAGE) 
	@echo "[OK] Disk image created: $(DISK_IMAGE)"

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
#  RUN IN QEMU WITH DISK
# ==============================
.PHONY: run
run: iso disk
	@echo "[QEMU] Starting NumOS with disk..."
	@qemu-system-x86_64 -m 128M \
		-boot order=dc \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-serial stdio

# ==============================
#  RUN WITHOUT GRAPHICS
# ==============================
.PHONY: run-nographic
run-nographic: iso disk
	@echo "=== Starting NumOS in QEMU (no graphics) with disk ==="
	qemu-system-x86_64 -m 128M \
		-boot order=dc \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-nographic

# ==============================
#  DEBUG WITH GDB
# ==============================
.PHONY: debug
debug: iso disk
	@echo "[QEMU] Starting NumOS with GDB support and disk..."
	@echo "In another terminal run: gdb build/kernel.bin"
	@echo "Then: (gdb) target remote localhost:1234"
	@echo "      (gdb) break kernel_main"
	@echo "      (gdb) continue"
	@qemu-system-x86_64 -m 128M \
		-boot order=dc \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-serial stdio \
		-s -S

# ==============================
#  RECREATE DISK IMAGE
# ==============================
.PHONY: newdisk
newdisk:
	@echo "[DISK] Recreating disk image..."
	@rm -f $(DISK_IMAGE)
	@$(MAKE) disk

# ==============================
#  CLEAN
# ==============================
.PHONY: clean
clean:
	@echo "[CLEAN] Removing build artifacts..."
	@rm -rf $(BUILD_DIR)
	@rm -rf NumOS.iso
	@echo "[OK] Clean complete"

.PHONY: distclean
distclean: clean
	@echo "[DISTCLEAN] Removing ISO and disk images..."
	@rm -f $(ISO_FILE)
	@echo "[OK] Distribution clean complete"

# ==============================
#  HELP
# ==============================
.PHONY: help
help:
	@echo "NumOS Kernel Build System with FAT32 + User Space"
	@echo "=================================================="
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build kernel, user space, and disk image (default)"
	@echo "  kernel      - Build kernel only"
	@echo "  user_space  - Build user_space/shell.elf"
	@echo "  disk        - Create FAT32 disk image (embeds shell.elf into /init)"
	@echo "  newdisk     - Recreate disk image from scratch"
	@echo "  iso         - Create bootable ISO image"
	@echo "  run         - Build and run in QEMU with disk"
	@echo "  debug       - Build and run with GDB support"
	@echo "  clean       - Remove build artifacts"
	@echo "  distclean   - Remove everything including images"
	@echo ""
	@echo "Current OS: $(OS_TYPE)"
	@echo ""
	@echo "The disk image contains:"
	@echo "  /init/SHELL - User-space Hello World ELF"
	@echo "  /bin        - Empty directory"
	@echo "  /run        - Empty directory"