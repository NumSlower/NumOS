################################################################################
#  NUMOS ROOT MAKEFILE
#
#  Kernel and disk image build.
#  User-space programs live in user/ and are built by user/Makefile.
#  This file never compiles user code directly.
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
#  TOOLCHAIN  (kernel only)
# ==============================
AS := nasm
CC := x86_64-elf-gcc
LD := x86_64-elf-ld

# ==============================
#  DIRECTORIES
# ==============================
SRC_DIR      := src
BUILD_DIR    := build
BUILD_KERNEL := $(BUILD_DIR)/kernel
BUILD_USER   := $(BUILD_DIR)/user
ISO_DIR      := $(BUILD_DIR)/iso
GRUB_DIR     := $(ISO_DIR)/boot/grub
TOOLS_DIR    := tools
USER_DIR     := user

# ==============================
#  FLAGS  (kernel only)
# ==============================
ASFLAGS := -f elf64

KERNEL_CFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-pic \
                 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel \
                 -Wall -Wextra -c -IInclude -O2

LDFLAGS := -T linker.ld -nostdlib --nmagic

# ==============================
#  KERNEL SOURCE FILES
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
KERNEL     := $(BUILD_DIR)/kernel.bin
ISO_FILE   := NumOS.iso
DISK_IMAGE := $(BUILD_DIR)/disk.img

# The ELF that ends up as /init/SHELL on the disk.
# Built by user/Makefile, installed to build/user/.
SHELL_ELF  := $(BUILD_USER)/elftest.elf

# ==============================
#  DEFAULT TARGET
# ==============================
.PHONY: all
all: kernel user_space disk

# ==============================
#  KERNEL BUILD
# ==============================
.PHONY: kernel
kernel: $(KERNEL)

$(BUILD_KERNEL)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@echo "[AS kernel] $<"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(AS) $(ASFLAGS) $< -o $@

$(BUILD_KERNEL)/%.o: $(SRC_DIR)/%.c
	@echo "[CC kernel] $<"
	@mkdir -p $(dir $@)
	@$(CC) $(KERNEL_CFLAGS) $< -o $@

$(KERNEL): $(ALL_KERNEL_OBJECTS)
	@echo "[LD kernel] Linking kernel..."
	@$(LD) $(LDFLAGS) -o $@ $^
	@echo "[OK] Kernel: $(KERNEL)"

# ==============================
#  USER-SPACE BUILD
#  Delegates entirely to user/Makefile.
#  No user .asm/.c/.o files are ever touched by this Makefile.
# ==============================
.PHONY: user_space
user_space:
	@echo "[USER] Building user-space programs..."
	@$(MAKE) -C $(USER_DIR) install
	@echo "[USER] Done."

# ==============================
#  DISK IMAGE
#  Depends on user_space so elftest.elf is guaranteed to exist first.
# ==============================
.PHONY: disk
disk: user_space
	@echo "[DISK] Creating FAT32 disk image..."
	@mkdir -p $(BUILD_DIR)
	@python3 $(TOOLS_DIR)/create_disk_fixed.py $(DISK_IMAGE) $(SHELL_ELF)
	@echo "[OK] Disk image: $(DISK_IMAGE)"

# ==============================
#  ISO IMAGE (GRUB)
# ==============================
.PHONY: iso
iso: $(ISO_FILE)

$(ISO_FILE): $(KERNEL)
	@echo "[ISO] Creating bootable ISO..."
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
	@echo "[OK] ISO: $(ISO_FILE)"

# ==============================
#  RUN IN QEMU
# ==============================
.PHONY: run
run: iso disk
	@echo "[QEMU] Starting NumOS..."
	@qemu-system-x86_64 -m 128M \
		-boot order=dc \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-serial stdio

.PHONY: run-nographic
run-nographic: iso disk
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
	@echo "[QEMU] GDB mode..."
	@qemu-system-x86_64 -m 128M \
		-boot order=dc \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=1 \
		-serial stdio -s -S

# ==============================
#  RECREATE DISK
# ==============================
.PHONY: newdisk
newdisk:
	@rm -f $(DISK_IMAGE)
	@$(MAKE) disk

# ==============================
#  CLEAN
# ==============================
.PHONY: clean
clean:
	@echo "[CLEAN] Kernel build artefacts..."
	@rm -rf $(BUILD_DIR) NumOS.iso
	@echo "[CLEAN] User build artefacts..."
	@$(MAKE) -C $(USER_DIR) clean
	@echo "[OK] Clean complete."

.PHONY: distclean
distclean: clean
	@rm -f $(ISO_FILE)
	@echo "[OK] Dist-clean complete."

# ==============================
#  HELP
# ==============================
.PHONY: help
help:
	@echo "NumOS Build System"
	@echo "=================="
	@echo ""
	@echo "Kernel targets (this Makefile):"
	@echo "  all          - kernel + user_space + disk (default)"
	@echo "  kernel       - kernel binary only"
	@echo "  user_space   - delegates to user/Makefile"
	@echo "  disk         - FAT32 disk image (embeds user/elftest.elf as /init/SHELL)"
	@echo "  iso          - bootable ISO via GRUB"
	@echo "  run          - build + boot in QEMU"
	@echo "  debug        - build + QEMU with GDB stub on :1234"
	@echo "  clean        - remove all build artefacts (kernel + user)"
	@echo ""
	@echo "User targets (user/Makefile, also callable directly):"
	@echo "  make -C user         - build all user programs"
	@echo "  make -C user install - build + copy to build/user/"
	@echo "  make -C user clean   - remove user build artefacts"
	@echo ""
	@echo "Project layout:"
	@echo "  src/          Kernel C and boot ASM sources"
	@echo "  Include/      Kernel headers"
	@echo "  user/         User-space programs (elftest.asm, linker.ld, Makefile)"
	@echo "  build/kernel/ Kernel object files"
	@echo "  build/user/   User ELF binaries (produced by user/Makefile)"
	@echo "  tools/        Disk image creation scripts"
	@echo ""
	@echo "Syscall numbers:"
	@echo "  0   SYS_READ       – read from fd"
	@echo "  1   SYS_WRITE      – write to fd (stdout/stderr → VGA)"
	@echo "  2   SYS_OPEN       – open file on FAT32 volume"
	@echo "  3   SYS_CLOSE      – close file descriptor"
	@echo "  35  SYS_SLEEP_MS   – sleep N milliseconds"
	@echo "  39  SYS_GETPID     – return process ID (always 1)"
	@echo "  60  SYS_EXIT       – terminate process"
	@echo "  96  SYS_UPTIME_MS  – return kernel uptime in ms"
	@echo "  200 SYS_PUTS       – write null-terminated string + newline"
	@echo ""
	@echo "OS: $(OS_TYPE)"