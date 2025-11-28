################################################################################
#  UNIVERSAL CROSS-PLATFORM MAKEFILE FOR NumOS
################################################################################

# ==============================
#  OS DETECTION
# ==============================
UNAME_S := $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(findstring Linux,$(UNAME_S)), Linux)
    OS_TYPE := linux
else ifeq ($(findstring Darwin,$(UNAME_S)), Darwin)
    OS_TYPE := mac
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    OS_TYPE := windows
    ENV_TYPE := msys
else ifneq (,$(findstring CYGWIN,$(UNAME_S)))
    OS_TYPE := windows
    ENV_TYPE := cygwin
else
    OS_TYPE := windows
    ENV_TYPE := powershell
endif

# ==============================
#  TOOLCHAIN
# ==============================
AS := nasm

ifeq ($(OS_TYPE), linux)
    CC := $(shell command -v x86_64-elf-gcc || echo /usr/local/cross/bin/x86_64-elf-gcc)
    LD := $(shell command -v x86_64-elf-ld || echo /usr/local/cross/bin/x86_64-elf-ld)
endif

ifeq ($(OS_TYPE), mac)
    CC := $(shell command -v x86_64-elf-gcc)
    LD := $(shell command -v x86_64-elf-ld)
endif

ifeq ($(OS_TYPE), windows)
    ifeq ($(ENV_TYPE), msys)
        CC := x86_64-elf-gcc
        LD := x86_64-elf-ld
    else
        CC := x86_64-elf-gcc.exe
        LD := x86_64-elf-ld.exe
    endif
endif

# Fallback if nothing found
CC := $(CC)
LD := $(LD)

# Diagnostics
$(info Detected OS: $(OS_TYPE))
$(info Environment: $(ENV_TYPE))
$(info CC: $(CC))
$(info LD: $(LD))

# ==============================
#  DIRECTORIES
# ==============================
SRC_DIR := src
BUILD_DIR := build
BUILD_KERNEL := $(BUILD_DIR)/kernel
MOUNT_DIR := /tmp/numos_mount

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
ASM_SOURCES := $(wildcard $(SRC_DIR)/boot/*.asm) \
               $(SRC_DIR)/kernel/process_switch.asm

KERNEL_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/*.c) \
                    $(wildcard $(SRC_DIR)/drivers/*.c) \
                    $(wildcard $(SRC_DIR)/cpu/x86/*.c) \
                    $(wildcard $(SRC_DIR)/fs/*.c)

# Convert paths → object files
ASM_OBJECTS := $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_KERNEL)/boot/%.o,$(filter $(SRC_DIR)/boot/%,$(ASM_SOURCES))) \
               $(patsubst $(SRC_DIR)/kernel/%.asm,$(BUILD_KERNEL)/kernel/%.o,$(filter $(SRC_DIR)/kernel/%,$(ASM_SOURCES)))

KERNEL_C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL)/%.o,$(KERNEL_C_SOURCES))

ALL_KERNEL_OBJECTS := $(ASM_OBJECTS) $(KERNEL_C_OBJECTS)

# ==============================
#  FINAL ARTIFACTS
# ==============================
KERNEL := $(BUILD_DIR)/kernel.bin
DISK_IMG := NumOS.img
USERSPACE_BIN := $(BUILD_DIR)/shell.bin

# ==============================
#  DEFAULT TARGET
# ==============================
ifeq ($(OS_TYPE), windows)
all: kernel userspace
	@echo "Windows build complete (no disk image)"
else
all: $(DISK_IMG)
endif

################################################################################
#  COMPILATION RULES
################################################################################

# ------------------------------
#  Assembly compilation
# ------------------------------
$(BUILD_KERNEL)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@echo "[AS] $<"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_KERNEL)/kernel/%.o: $(SRC_DIR)/kernel/%.asm
	@echo "[AS] $<"
	@mkdir -p $(BUILD_KERNEL)/kernel
	@$(AS) $(ASFLAGS) $< -o $@

# ------------------------------
#  C compilation
# ------------------------------
$(BUILD_KERNEL)/%.o: $(SRC_DIR)/%.c
	@echo "[CC] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) $< -o $@

# ------------------------------
#  Linking
# ------------------------------
$(KERNEL): $(ALL_KERNEL_OBJECTS)
	@echo "[LD] Linking kernel..."
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo "[OK] Kernel built"

################################################################################
#  USERSPACE
################################################################################

userspace:
	@$(MAKE) -C userspace

################################################################################
#  DISK IMAGE (Linux/macOS only)
################################################################################

ifeq ($(OS_TYPE), linux)
$(DISK_IMG): $(KERNEL) userspace
	@echo "=== Creating Disk Image ==="
	@dd if=/dev/zero of=$(DISK_IMG) bs=1M count=64 status=none
	@parted -s $(DISK_IMG) mklabel msdos
	@parted -s $(DISK_IMG) mkpart primary fat32 1MiB 63MiB
	@parted -s $(DISK_IMG) set 1 boot on
	@sudo losetup -fP $(DISK_IMG)
	@sleep 1
	@LOOP=$$(sudo losetup -j $(DISK_IMG) | cut -d: -f1); \
	sudo mkfs.fat -F 32 $${LOOP}p1; \
	sudo mkdir -p $(MOUNT_DIR); \
	sudo mount $${LOOP}p1 $(MOUNT_DIR); \
	sudo mkdir -p $(MOUNT_DIR)/boot/grub; \
	sudo cp $(KERNEL) $(MOUNT_DIR)/boot/kernel.bin; \
	sudo cp preboot/grub/grub.cfg $(MOUNT_DIR)/boot/grub/; \
	sudo grub-install --target=i386-pc --boot-directory=$(MOUNT_DIR)/boot $$LOOP; \
	sudo cp $(USERSPACE_BIN) $(MOUNT_DIR)/shell; \
	sudo umount $(MOUNT_DIR); \
	sudo losetup -d $$LOOP

	@echo "[OK] Disk image created"

endif

################################################################################
#  CLEAN
################################################################################

clean:
	@rm -rf $(BUILD_DIR) $(DISK_IMG)
	@$(MAKE) -C userspace clean
	@echo "[OK] Cleaned"

