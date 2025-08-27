# NumOS Main Makefile
# 64-bit Operating System

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

.PHONY: all clean iso run debug

all: $(ISO)

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
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO)

# Run in QEMU
run: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M

# Debug in QEMU
debug: $(ISO)
	qemu-system-x86_64 -cdrom $(ISO) -boot d -m 512M -s -S
