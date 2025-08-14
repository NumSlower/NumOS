# NumOS Enhanced Makefile
# 64-bit Operating System with FAT32 Boot Support

# Compiler and tools
AS = nasm
CC = x86_64-elf-gcc
LD = ld
PYTHON = python3

# Directories
SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso
BOOT_DIR = iso/boot
GRUB_DIR = iso/boot/grub
DISK_DIR = disk

# Flags
ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-pic -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -Wall -Wextra -c -IInclude -Os
LDFLAGS = -T link.ld -nostdlib --nmagic

# Source files
ASM_SOURCES = $(wildcard $(SRC_DIR)/boot/*.asm)
C_SOURCES = $(wildcard $(SRC_DIR)/kernel/*.c) $(wildcard $(SRC_DIR)/drivers/*.c) $(wildcard $(SRC_DIR)/cpu/x86/*.c $(wildcard $(SRC_DIR)/fs/*.c))

# Object files
ASM_OBJECTS = $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_DIR)/boot/%.o,$(ASM_SOURCES))
C_KERNEL_OBJECTS = $(patsubst $(SRC_DIR)/kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(wildcard $(SRC_DIR)/kernel/*.c))
C_CPU_OBJECTS    = $(patsubst $(SRC_DIR)/cpu/x86/%.c,$(BUILD_DIR)/cpu/x86/%.o,$(wildcard $(SRC_DIR)/cpu/x86/*.c))
C_DRIVER_OBJECTS = $(patsubst $(SRC_DIR)/drivers/%.c,$(BUILD_DIR)/drivers/%.o,$(wildcard $(SRC_DIR)/drivers/*.c))
C_FS_OBJECTS     = $(patsubst $(SRC_DIR)/fs/%.c,$(BUILD_DIR)/fs/%.o,$(wildcard $(SRC_DIR)/fs/*.c))

OBJECTS = $(ASM_OBJECTS) $(C_KERNEL_OBJECTS) $(C_CPU_OBJECTS) $(C_DRIVER_OBJECTS) $(C_FS_OBJECTS)

# Target files
KERNEL = $(BUILD_DIR)/kernel.bin
ISO = NumOS.iso
DISK_IMAGE = NumOS.img
BOOTLOADER = $(BUILD_DIR)/boot.bin

.PHONY: all clean iso disk run run-disk debug debug-disk install-tools

# Default target - build disk image
all: $(DISK_IMAGE)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel $(BUILD_DIR)/cpu/x86 $(BUILD_DIR)/drivers $(BUILD_DIR)/fs

$(ISO_DIR):
	mkdir -p $(BOOT_DIR) $(GRUB_DIR)

$(DISK_DIR):
	mkdir -p $(DISK_DIR)

# Compile assembly files
$(BUILD_DIR)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@mkdir -p $(BUILD_DIR)/boot
	$(AS) $(ASFLAGS) $< -o $@

$(BUILD_DIR)/kernel/%.o: $(SRC_DIR)/kernel/%.c
	@mkdir -p $(BUILD_DIR)/kernel
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/cpu/x86/%.o: $(SRC_DIR)/cpu/x86/%.c
	@mkdir -p $(BUILD_DIR)/cpu/x86
	$(CC) $(CFLAGS) $< -o $@

$(BUILD_DIR)/drivers/%.o: $(SRC_DIR)/drivers/%.c
	@mkdir -p $(BUILD_DIR)/drivers
	$(CC) $(CFLAGS) $< -o $@
	
$(BUILD_DIR)/fs/%.o: $(SRC_DIR)/fs/%.c
	@mkdir -p $(BUILD_DIR)/fs
	$(CC) $(CFLAGS) $< -o $@

# Build bootloader (if using custom boot sector)
$(BOOTLOADER): boot/boot.asm
	@mkdir -p $(BUILD_DIR)
	$(AS) -f bin boot/boot.asm -o $@

# Link kernel
$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

# Create ISO (GRUB-based boot)
$(ISO): $(KERNEL) | $(ISO_DIR)
	cp $(KERNEL) $(BOOT_DIR)/
	cp boot/grub/grub.cfg $(GRUB_DIR)/
	grub-mkrescue -o $@ $(ISO_DIR)

# Create disk image with FAT32 filesystem
$(DISK_IMAGE): $(KERNEL) tools/build_disk.py | $(DISK_DIR)
	@echo "Building FAT32 disk image..."
	$(PYTHON) tools/build_disk.py $@ $(KERNEL)
	@echo "Disk image ready: $@"

# Install required tools
install-tools:
	@echo "Installing required tools..."
	@echo "Please ensure you have:"
	@echo "  - x86_64-elf-gcc (cross-compiler)"
	@echo "  - nasm (assembler)"
	@echo "  - qemu-system-x86_64 (emulator)"
	@echo "  - grub-mkrescue (for ISO creation)"
	@echo "  - python3 (for disk image builder)"
	@echo ""
	@echo "On Ubuntu/Debian:"
	@echo "  sudo apt install build-essential nasm qemu-system-x86 grub-pc-bin grub-common xorriso python3"
	@echo ""
	@echo "For cross-compiler, you may need to build it from source or use a package manager like:"
	@echo "  sudo apt install gcc-multilib-x86-64-linux-gnu"

# Copy build disk tool to tools directory
tools/build_disk.py: | tools/
	@echo "Copying disk builder tool..."
	@cp build_disk.py tools/build_disk.py 2>/dev/null || echo "Please copy the disk builder script to tools/build_disk.py"

tools/:
	mkdir -p tools

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(DISK_DIR) $(ISO) $(DISK_IMAGE)
	@echo "Build directories cleaned"

# Run with ISO in QEMU
run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M

# Run with disk image in QEMU
run-disk: $(DISK_IMAGE)
	qemu-system-x86_64 -drive file=$(DISK_IMAGE),format=raw -m 512M

# Run with more memory and debugging options
run-debug: $(DISK_IMAGE)
	qemu-system-x86_64 -drive file=$(DISK_IMAGE),format=raw -m 1G -serial stdio -d cpu_reset

# Debug in QEMU with GDB support
debug: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M -s -S

debug-disk: $(DISK_IMAGE)
	qemu-system-x86_64 -drive file=$(DISK_IMAGE),format=raw -m 512M -s -S

# Create a minimal test environment
test-env: $(DISK_IMAGE)
	@echo "Testing disk image..."
	@echo "Running quick boot test..."
	timeout 10 qemu-system-x86_64 -drive file=$(DISK_IMAGE),format=raw -m 256M -display none -serial stdio || true
	@echo "Test complete. If kernel loaded successfully, you should see boot messages above."

# Show disk image information
disk-info: $(DISK_IMAGE)
	@echo "Disk Image Information:"
	@echo "======================"
	@ls -lh $(DISK_IMAGE)
	@echo ""
	@echo "First sector (boot sector):"
	@hexdump -C $(DISK_IMAGE) | head -20
	@echo ""
	@echo "To examine the filesystem:"
	@echo "  sudo mount -o loop,offset=16384 $(DISK_IMAGE) /mnt"
	@echo "  ls -la /mnt"
	@echo "  sudo umount /mnt"

# Extract files from disk image (requires sudo for loop mount)
extract-files: $(DISK_IMAGE)
	@echo "Extracting files from disk image..."
	mkdir -p extracted
	@echo "Note: This requires sudo for loop mounting"
	sudo mount -o loop,offset=16384 $(DISK_IMAGE) extracted/
	ls -la extracted/
	@echo "Files extracted to extracted/ directory"
	@echo "Remember to unmount with: sudo umount extracted/"

# Help target
help:
	@echo "NumOS Build System"
	@echo "=================="
	@echo ""
	@echo "Targets:"
	@echo "  all          - Build disk image (default)"
	@echo "  $(KERNEL)    - Build kernel binary only"
	@echo "  $(ISO)       - Build bootable ISO"
	@echo "  $(DISK_IMAGE)- Build FAT32 disk image"
	@echo "  clean        - Clean all build files"
	@echo ""
	@echo "Running:"
	@echo "  run          - Run ISO in QEMU"
	@echo "  run-disk     - Run disk image in QEMU"
	@echo "  run-debug    - Run with debug output"
	@echo "  debug        - Run with GDB debugging"
	@echo "  debug-disk   - Debug disk image with GDB"
	@echo ""
	@echo "Testing:"
	@echo "  test-env     - Quick boot test"
	@echo "  disk-info    - Show disk image information"
	@echo "  extract-files- Extract files from disk image"
	@echo ""
	@echo "Utilities:"
	@echo "  install-tools- Show tool installation instructions"
	@echo "  help         - Show this help message"
	@echo ""
	@echo "Files created:"
	@echo "  $(KERNEL)    - Kernel binary"
	@echo "  $(DISK_IMAGE)- Bootable disk image with FAT32"
	@echo "  $(ISO)       - Bootable ISO image (GRUB)"