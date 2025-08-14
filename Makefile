# NumOS Main Makefile
# 64-bit Operating System

# Compiler and tools
AS = nasm
CC = x86_64-elf-gcc
LD = ld
GRUB_MKRESCUE = grub-mkrescue

# Directories
SRC_DIR = src
BUILD_DIR = build
ISO_DIR = iso
BOOT_DIR = iso/boot
GRUB_DIR = iso/boot/grub

# Flags
ASFLAGS = -f elf64
CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-pic -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel -Wall -Wextra -c -IInclude -Os
LDFLAGS = -T link.ld -nostdlib --nmagic

# Source files
ASM_SOURCES = $(wildcard $(SRC_DIR)/boot/*.asm)
C_SOURCES = $(wildcard $(SRC_DIR)/kernel/*.c) $(wildcard $(SRC_DIR)/drivers/*.c) $(wildcard $(SRC_DIR)/cpu/x86/*.c)

# Object files
ASM_OBJECTS = $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_DIR)/boot/%.o,$(ASM_SOURCES))
C_KERNEL_OBJECTS = $(patsubst $(SRC_DIR)/kernel/%.c,$(BUILD_DIR)/kernel/%.o,$(wildcard $(SRC_DIR)/kernel/*.c))
C_CPU_OBJECTS    = $(patsubst $(SRC_DIR)/cpu/x86/%.c,$(BUILD_DIR)/cpu/x86/%.o,$(wildcard $(SRC_DIR)/cpu/x86/*.c))
C_DRIVER_OBJECTS = $(patsubst $(SRC_DIR)/drivers/%.c,$(BUILD_DIR)/drivers/%.o,$(wildcard $(SRC_DIR)/drivers/*.c))

OBJECTS = $(ASM_OBJECTS) $(C_KERNEL_OBJECTS) $(C_CPU_OBJECTS) $(C_DRIVER_OBJECTS)

# Target
KERNEL = $(BUILD_DIR)/kernel.bin
ISO = NumOS.iso

.PHONY: all clean iso run

all: $(ISO)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)/boot $(BUILD_DIR)/kernel $(BUILD_DIR)/cpu/x86 $(BUILD_DIR)/drivers

$(ISO_DIR):
	mkdir -p $(BOOT_DIR) $(GRUB_DIR)

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

# Link kernel
$(KERNEL): $(OBJECTS)
	$(LD) $(LDFLAGS) -o $@ $^

# Create ISO
$(ISO): $(KERNEL) | $(ISO_DIR)
	cp $(KERNEL) $(BOOT_DIR)/
	cp boot/grub/grub.cfg $(GRUB_DIR)/
	$(GRUB_MKRESCUE) -o $@ $(ISO_DIR)

# Clean build files
clean:
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)

# Run in QEMU
run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M

# Debug in QEMU
debug: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M -s -S