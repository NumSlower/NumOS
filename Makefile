# NumOS Main Makefile
# 64-bit Operating System with Disk Driver

# Compiler and tools
AS = nasm
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
GRUB_MKRESCUE = grub-mkrescue

# Directories
SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso
BOOT_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_DIR)/grub

# Flags
ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-pic \
         -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
         -Wall -Wextra -c -IInclude -Os
LDFLAGS = -T link.ld -nostdlib --nmagic

# Source files
ASM_SOURCES = $(wildcard $(SRC_DIR)/boot/*.asm)
C_SOURCES = $(wildcard $(SRC_DIR)/kernel/*.c) \
            $(wildcard $(SRC_DIR)/drivers/*.c) \
            $(wildcard $(SRC_DIR)/cpu/x86/*.c) \
            $(wildcard $(SRC_DIR)/fs/*.c) \
			$(wildcard $(SRC_DIR)/usr/*.c)

# Object files
ASM_OBJECTS = $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_DIR)/boot/%.o,$(ASM_SOURCES))
C_OBJECTS   = $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(C_SOURCES))

OBJECTS = $(ASM_OBJECTS) $(C_OBJECTS)

# Targets
KERNEL = $(BUILD_DIR)/kernel.bin
ISO = NumOS.iso

.PHONY: all clean iso run debug disk-image

all: $(ISO)

# Create disk image for persistence
disk-image:
	@echo "Creating persistent disk image..."
	@mkdir -p images
	@if [ ! -f images/numos_disk.img ]; then \
		dd if=/dev/zero of=images/numos_disk.img bs=1M count=64 2>/dev/null; \
		echo "Created 64MB disk image at images/numos_disk.img"; \
	else \
		echo "Disk image already exists at images/numos_disk.img"; \
	fi

# Compile assembly
$(BUILD_DIR)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@mkdir -p $(BUILD_DIR)/boot
	$(AS) $(ASFLAGS) $< -o $@

# Compile C
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

# Link kernel
$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

# Create ISO
$(ISO): $(KERNEL) | $(ISO_DIR)
	@mkdir -p $(BOOT_DIR) $(GRUB_DIR)
	cp $(KERNEL) $(BOOT_DIR)/
	cp boot/grub/grub.cfg $(GRUB_DIR)/
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

# Clean
clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO) images bin

# Clean everything including disk image
clean-all: clean
	rm -rf images/

# Run in QEMU with persistent disk
run: $(ISO) disk-image
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M \
		-drive file=images/numos_disk.img,format=raw,if=ide

# Debug in QEMU with persistent disk
debug: $(ISO) disk-image
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M \
		-drive file=images/numos_disk.img,format=raw,if=ide -s -S

# Run without disk (RAM only mode)
run-ram: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M

# Test with different disk sizes
run-large: $(ISO)
	@mkdir -p images
	@if [ ! -f images/numos_large_disk.img ]; then \
		dd if=/dev/zero of=images/numos_large_disk.img bs=1M count=256 2>/dev/null; \
		echo "Created 256MB large disk image"; \
	fi
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M \
		-drive file=images/numos_large_disk.img,format=raw,if=ide

# Format disk image with FAT32
format-disk: disk-image
	@echo "Formatting disk image with FAT32..."
	@if command -v mkfs.fat >/dev/null 2>&1; then \
		mkfs.fat -F 32 -n "NUMOS_DISK" images/numos_disk.img; \
		echo "Disk image formatted with FAT32"; \
	else \
		echo "mkfs.fat not available, disk will be formatted by NumOS"; \
	fi

# Mount disk image (Linux/macOS)
mount-disk: disk-image
	@echo "Mounting disk image..."
	@mkdir -p mnt
	@if [ -f images/numos_disk.img ]; then \
		sudo mount -o loop images/numos_disk.img mnt/; \
		echo "Disk mounted at mnt/"; \
		echo "Use 'make unmount-disk' to unmount"; \
	fi

# Unmount disk image
unmount-disk:
	@if mountpoint -q mnt/ 2>/dev/null; then \
		sudo umount mnt/; \
		echo "Disk unmounted"; \
	fi
	@rmdir mnt/ 2>/dev/null || true