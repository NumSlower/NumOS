################################################################################
#  NUMOS ROOT MAKEFILE
################################################################################

UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)
ifeq ($(findstring Linux,$(UNAME_S)), Linux)
    OS_TYPE := linux
else ifeq ($(findstring Darwin,$(UNAME_S)), Darwin)
    OS_TYPE := mac
else
    OS_TYPE := windows
endif

AS := nasm
CC := x86_64-elf-gcc
LD := x86_64-elf-ld

SRC_DIR      := src
BUILD_DIR    := build
BUILD_KERNEL := $(BUILD_DIR)/kernel
BUILD_USER   := $(BUILD_DIR)/user
ISO_DIR      := $(BUILD_DIR)/iso
GRUB_DIR     := $(ISO_DIR)/boot/grub
TOOLS_DIR    := tools
USER_DIR     := user

ASFLAGS := -f elf64

KERNEL_CFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-pic \
                 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
                 -Wall -Wextra -c -IInclude -O2

LDFLAGS := -T linker.ld -nostdlib --nmagic

ASM_SOURCES := $(wildcard $(SRC_DIR)/boot/*.asm)

KERNEL_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/*.c) \
                    $(wildcard $(SRC_DIR)/drivers/*.c) \
                    $(wildcard $(SRC_DIR)/cpu/x86/*.c) \
                    $(wildcard $(SRC_DIR)/fs/*.c)

ASM_OBJECTS     := $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_KERNEL)/boot/%.o,$(ASM_SOURCES))
KERNEL_C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL)/%.o,$(KERNEL_C_SOURCES))
ALL_KERNEL_OBJECTS := $(ASM_OBJECTS) $(KERNEL_C_OBJECTS)

KERNEL     := $(BUILD_DIR)/kernel.bin
ISO_FILE   := NumOS.iso
DISK_IMAGE := $(BUILD_DIR)/disk.img
SHELL_ELF  := $(BUILD_USER)/elftest.elf

.PHONY: all
all: kernel user_space disk

# ---- Kernel ----------------------------------------------------------------
.PHONY: kernel
kernel: $(KERNEL)

$(BUILD_KERNEL)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@echo "[AS]  $<"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_KERNEL)/%.o: $(SRC_DIR)/%.c
	@echo "[CC]  $<"
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) $< -o $@

$(KERNEL): $(ALL_KERNEL_OBJECTS)
	@echo "[LD]  kernel"
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo "[OK]  $(KERNEL)"

# ---- User space ------------------------------------------------------------
.PHONY: user_space
user_space:
	@$(MAKE) -C $(USER_DIR) install

# ---- Disk image ------------------------------------------------------------
.PHONY: disk
disk: user_space
	@mkdir -p $(BUILD_DIR)
	@python3 $(TOOLS_DIR)/create_disk.py $(DISK_IMAGE) $(SHELL_ELF)
	@echo "[OK]  $(DISK_IMAGE)"

# ---- ISO -------------------------------------------------------------------
.PHONY: iso
iso: $(ISO_FILE)

$(ISO_FILE): $(KERNEL)
	@mkdir -p $(GRUB_DIR)
	@cp $(KERNEL) $(ISO_DIR)/boot/kernel.bin
	@echo "set default=0"        > $(GRUB_DIR)/grub.cfg
	@echo "set timeout=0"       >> $(GRUB_DIR)/grub.cfg
	@echo "menuentry \"NumOS\" {" >> $(GRUB_DIR)/grub.cfg
	@echo "    multiboot2 /boot/kernel.bin" >> $(GRUB_DIR)/grub.cfg
	@echo "    boot"             >> $(GRUB_DIR)/grub.cfg
	@echo "}"                   >> $(GRUB_DIR)/grub.cfg
	@grub-mkrescue -o $(ISO_FILE) $(ISO_DIR) 2>/dev/null || \
		(echo "[ERROR] Install grub-pc-bin and xorriso" && false)
	@echo "[OK]  $(ISO_FILE)"

# ---- QEMU ------------------------------------------------------------------
#
# -vga std  exposes the Bochs VGA device (PCI 0x1234:0x1111) with BGA I/O
#           ports 0x01CE/0x01CF.  Required for the framebuffer driver.
#           Do NOT combine with -device virtio-gpu-pci.
#
.PHONY: run
run: iso disk
	@echo "[QEMU] Starting NumOS..."
	@qemu-system-x86_64 \
		-m 4096 \
		-boot order=dc \
		-smp 2 \
		-vga std \
		-display gtk \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-serial stdio

.PHONY: run-nographic
run-nographic: iso disk
	qemu-system-x86_64 -m 128M -boot order=dc \
		-vga std \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-nographic

# ---- GDB -------------------------------------------------------------------
.PHONY: debug
debug: iso disk
	@qemu-system-x86_64 -m 128M -boot order=dc \
		-vga std \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-serial stdio -s -S

# ---- VirtualBox (manual) ---------------------------------------------------
# Import NumOS.iso as a CD-ROM in VirtualBox.
# Attach disk.img (build/disk.img) to an IDE controller (PIIX3 or PIIX4).
# Set display adapter to VBoxVGA (System → Display → Graphics Controller).
# Boot order: CD-ROM first.
# The kernel ATA driver uses ports 0x1F0-0x1F7 (IDE); SATA/AHCI ports differ.

# ---- Clean -----------------------------------------------------------------
.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR) NumOS.iso
	@$(MAKE) -C $(USER_DIR) clean

.PHONY: distclean
distclean: clean
	@rm -f $(ISO_FILE)

# ---- Help ------------------------------------------------------------------
.PHONY: help
help:
	@echo "NumOS Build System"
	@echo "  make run          - build + QEMU  (-vga std, BGA framebuffer)"
	@echo "  make debug        - build + QEMU + GDB stub on :1234"
	@echo "  make clean        - remove build artefacts"
	@echo ""
	@echo "VirtualBox setup:"
	@echo "  1. New VM, 64-bit Other, 4 GB RAM"
	@echo "  2. Storage: attach NumOS.iso as CD, build/disk.img as IDE disk"
	@echo "  3. Display: Graphics Controller = VBoxVGA"
	@echo "  4. Boot order: Optical first"
	@echo ""
	@echo "OS: $(OS_TYPE)"