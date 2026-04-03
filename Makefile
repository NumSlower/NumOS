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

HOST_CPU_COUNT := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
NUMOS_BUILD_JOBS ?= $(HOST_CPU_COUNT)
NUMOS_OPT_LEVEL ?= 3
NUMOS_COMMON_OPT_FLAGS ?= -O$(NUMOS_OPT_LEVEL) -pipe

ifneq ($(filter -j% --jobs=%,$(MAKEFLAGS)),)
    NUMOS_PARALLEL_DEFAULT :=
else
    MAKEFLAGS += -j$(NUMOS_BUILD_JOBS)
endif

HOST_ARCH := $(shell uname -m 2>/dev/null || echo unknown)
ifeq ($(HOST_ARCH),aarch64)
    NUMOS_ARCH ?= arm64
else ifeq ($(HOST_ARCH),arm64)
    NUMOS_ARCH ?= arm64
else
    NUMOS_ARCH ?= x86_64
endif
NUMOS_MACHINE ?= pc
ARCH_DOC := docs/PORTING_RPI5_ARM64.md
ARM64_DOC := docs/ARM64_QEMU_VIRT.md

ifeq ($(NUMOS_ARCH),arm64)
ifeq ($(NUMOS_MACHINE),pc)
    NUMOS_MACHINE := virt
endif
endif

ARCH_BUILD_READY := 0
ARCH_STATUS := Unsupported NUMOS_ARCH=$(NUMOS_ARCH). Supported values: x86_64, arm64.
NUMOS_TARGET_TRIPLE :=
NUMOS_QEMU :=
NUMOS_ARCH_NAME := $(NUMOS_ARCH)
NUMOS_CPU_MODE_NAME := unknown
NUMOS_BOOT_PROTOCOL_NAME := unknown
NUMOS_CPU_DIR := $(NUMOS_ARCH)
ASFLAGS :=
KERNEL_ARCH_CFLAGS :=
LDFLAGS :=

ifeq ($(NUMOS_ARCH),x86_64)
    ARCH_BUILD_READY := 1
    ARCH_STATUS := Ready. NumOS builds for AMD64 and Intel x86-64 PCs through GRUB Multiboot2.
    NUMOS_TARGET_TRIPLE := x86_64-elf
    NUMOS_QEMU := qemu-system-x86_64
    NUMOS_ARCH_NAME := x86-64
    NUMOS_CPU_MODE_NAME := Long
    NUMOS_BOOT_PROTOCOL_NAME := Multiboot2
    NUMOS_CPU_DIR := x86
    ASFLAGS := -f elf64
    KERNEL_ARCH_CFLAGS := -m64 -mno-mmx -mno-sse -mno-sse2 -mno-red-zone -mcmodel=kernel
    LDFLAGS := -T linker/kernel.ld -nostdlib --nmagic -z noexecstack
else ifeq ($(NUMOS_ARCH),arm64)
    ARCH_BUILD_READY := 1
    ARCH_STATUS := Preview. NumOS builds a serial first ARM64 kernel for QEMU virt. See $(ARM64_DOC) and $(ARCH_DOC).
    NUMOS_TARGET_TRIPLE := aarch64-none-elf
    NUMOS_QEMU := qemu-system-aarch64
    NUMOS_ARCH_NAME := ARM64
    NUMOS_CPU_MODE_NAME := EL1
    NUMOS_BOOT_PROTOCOL_NAME := QEMU-virt-direct-boot
    NUMOS_CPU_DIR := arm64
    LDFLAGS := -T linker/kernel-arm64.ld -nostdlib -z noexecstack
else
    NUMOS_TARGET_TRIPLE := $(NUMOS_ARCH)-elf
    NUMOS_QEMU := qemu-system-$(NUMOS_ARCH)
endif

NUMOS_TARGET ?= $(NUMOS_TARGET_TRIPLE)
NUMOS_AS ?= $(or $(shell command -v nasm 2>/dev/null),$(shell command -v yasm 2>/dev/null),nasm)
ifeq ($(NUMOS_ARCH),arm64)
NUMOS_CC ?= $(or $(shell command -v $(NUMOS_TARGET)-gcc 2>/dev/null),$(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),$(NUMOS_TARGET)-gcc)
NUMOS_LD ?= $(or $(shell command -v $(NUMOS_TARGET)-ld 2>/dev/null),$(shell command -v aarch64-linux-gnu-ld 2>/dev/null),$(NUMOS_TARGET)-ld)
else
NUMOS_CC ?= $(or $(shell command -v $(NUMOS_TARGET)-gcc 2>/dev/null),$(shell command -v gcc 2>/dev/null),$(shell command -v clang 2>/dev/null),$(NUMOS_TARGET)-gcc)
NUMOS_LD ?= $(or $(shell command -v $(NUMOS_TARGET)-ld 2>/dev/null),$(shell command -v ld 2>/dev/null),$(shell command -v ld.lld 2>/dev/null),$(NUMOS_TARGET)-ld)
endif

SRC_DIR      := src
BUILD_DIR    := build
BUILD_KERNEL := $(BUILD_DIR)/kernel
BUILD_KERNEL_ATA := $(BUILD_DIR)/kernel-ata
BUILD_USER   := $(BUILD_DIR)/user
BOOT_STAGE_DIR := $(BUILD_DIR)/stage/run
ISO_DIR      := $(BUILD_DIR)/iso
GRUB_DIR     := $(ISO_DIR)/boot/grub
TOOLS_DIR    := tools
USER_DIR     := user
MULTIBOOT_FLAGS := $(BUILD_KERNEL)/boot/multiboot.flags

OS_NAME       ?= NumOS
KERNEL_NAME   ?= kernel.bin
KERNEL_VESA_NAME ?= kernel-vesa.bin
KERNEL_ATA_NAME ?= kernel-ata.bin
ARM64_KERNEL_NAME ?= kernel-arm64.elf
DISK_NAME     ?= disk.img
ISO_NAME      ?= $(OS_NAME).iso
ISO_KERNEL_ONLY_NAME ?= $(OS_NAME)-kernel-only.iso
INIT_ELF_NAME ?= shell.elf
INIT_PATH     ?= /bin/$(INIT_ELF_NAME)
PART_TARGET   ?= $(DISK_IMAGE)
PART_TABLE    ?= gpt
PART_FS       ?= fat32
PART_START    ?= 1MiB
PART_END      ?= 100%
PART_FORMAT   ?= 0
PART_APPLY    ?= 0
PART_POPULATE ?= 1
NUMOS_PACK_USER_ELF ?= 1

# Read NUMOS_ENABLE_FRAMEBUFFER and NUMOS_FB_ENABLE_VBE from config.h.
# The default kernel follows NUMOS_FB_ENABLE_VBE. A second kernel image is
# always built with VBE forced on so GRUB menu entries can offer VESA boot.
FB_ENABLED := $(shell grep -E '^\s*#\s*define\s+NUMOS_ENABLE_FRAMEBUFFER\s' \
                  Include/kernel/config.h | awk '{print $$3}' | tr -d '\r\n')
VBE_TAG := $(shell grep -E '^\s*#\s*define\s+NUMOS_FB_ENABLE_VBE\s' \
                  Include/kernel/config.h | awk '{print $$3}' | tr -d '\r\n')
FB_WIDTH := $(shell grep -E '^\s*#\s*define\s+NUMOS_FB_WIDTH\s' \
                  Include/kernel/config.h | awk '{print $$3}' | tr -d '\r\n')
FB_HEIGHT := $(shell grep -E '^\s*#\s*define\s+NUMOS_FB_HEIGHT\s' \
                   Include/kernel/config.h | awk '{print $$3}' | tr -d '\r\n')
FB_BPP ?= 32
GRUB_VESA_MODE := $(FB_WIDTH)x$(FB_HEIGHT)x$(FB_BPP)
FORCE_VBE ?= 0
ifeq ($(FORCE_VBE),1)
    VBE_TAG := 1
endif
ifeq ($(FB_ENABLED),1)
    GRUB_GFX_ENABLED := $(VBE_TAG)
else
    GRUB_GFX_ENABLED := 0
endif
ifeq ($(VBE_TAG),1)
    ASFLAGS_MULTIBOOT := -f elf64 -D ENABLE_FRAMEBUFFER=1 -D FB_WIDTH=$(FB_WIDTH) -D FB_HEIGHT=$(FB_HEIGHT) -D FB_BPP=$(FB_BPP)
else
    ASFLAGS_MULTIBOOT := -f elf64 -D ENABLE_FRAMEBUFFER=0 -D FB_WIDTH=$(FB_WIDTH) -D FB_HEIGHT=$(FB_HEIGHT) -D FB_BPP=$(FB_BPP)
endif
ASFLAGS_MULTIBOOT_VESA := -f elf64 -D ENABLE_FRAMEBUFFER=1 -D FB_WIDTH=$(FB_WIDTH) -D FB_HEIGHT=$(FB_HEIGHT) -D FB_BPP=$(FB_BPP)

KERNEL_CFLAGS := $(KERNEL_ARCH_CFLAGS) -ffreestanding -fstack-protector-strong \
                 -mstack-protector-guard=global -fno-pic \
                 -Wall -Wextra -c -IInclude $(NUMOS_COMMON_OPT_FLAGS) \
                 -DNUMOS_ARCH_NAME=\"$(NUMOS_ARCH_NAME)\" \
                 -DNUMOS_CPU_MODE_NAME=\"$(NUMOS_CPU_MODE_NAME)\" \
                 -DNUMOS_BOOT_PROTOCOL_NAME=\"$(NUMOS_BOOT_PROTOCOL_NAME)\" \
                 -DNUMOS_INIT_PATH=\"$(INIT_PATH)\"

ifeq ($(NUMOS_ARCH),x86_64)
ASM_SOURCES := $(wildcard $(SRC_DIR)/boot/*.asm)
ASM_COMMON_SOURCES := $(filter-out $(SRC_DIR)/boot/multiboot_header.asm,$(ASM_SOURCES))

KERNEL_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/*.c) \
                    $(wildcard $(SRC_DIR)/drivers/*.c) \
                    $(wildcard $(SRC_DIR)/drivers/graphices/*.c) \
                    $(wildcard $(SRC_DIR)/cpu/$(NUMOS_CPU_DIR)/*.c) \
                    $(wildcard $(SRC_DIR)/fs/*.c)

TRAMPOLINE_SRC := $(SRC_DIR)/boot/ap_trampoline.bin.nasm
TRAMPOLINE_BIN := $(BUILD_KERNEL)/boot/ap_trampoline.bin
TRAMPOLINE_OBJ := $(BUILD_KERNEL)/boot/ap_trampoline.o

ASM_OBJECTS      := $(patsubst $(SRC_DIR)/boot/%.asm,$(BUILD_KERNEL)/boot/%.o,$(ASM_COMMON_SOURCES))
MULTIBOOT_OBJECT := $(BUILD_KERNEL)/boot/multiboot_header.o
MULTIBOOT_OBJECT_VESA := $(BUILD_KERNEL)/boot/multiboot_header.vesa.o
KERNEL_C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL)/%.o,$(KERNEL_C_SOURCES))
KERNEL_C_OBJECTS_ATA := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL_ATA)/%.o,$(KERNEL_C_SOURCES))
COMMON_KERNEL_OBJECTS := $(ASM_OBJECTS) $(KERNEL_C_OBJECTS) $(TRAMPOLINE_OBJ)
else ifeq ($(NUMOS_ARCH),arm64)
ARM64_BOOT_SOURCES := $(wildcard $(SRC_DIR)/boot/arm64/*.S)
ARM64_C_SOURCES := $(wildcard $(SRC_DIR)/kernel/arm64/*.c) \
                   $(wildcard $(SRC_DIR)/cpu/arm64/*.c) \
                   $(wildcard $(SRC_DIR)/drivers/arm64/*.c)
ARM64_BOOT_OBJECTS := $(patsubst $(SRC_DIR)/boot/arm64/%.S,$(BUILD_KERNEL)/boot/arm64/%.o,$(ARM64_BOOT_SOURCES))
ARM64_C_OBJECTS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_KERNEL)/%.o,$(ARM64_C_SOURCES))
ARM64_OBJECTS := $(ARM64_BOOT_OBJECTS) $(ARM64_C_OBJECTS)
endif

KERNEL     := $(BUILD_DIR)/$(KERNEL_NAME)
KERNEL_VESA := $(BUILD_DIR)/$(KERNEL_VESA_NAME)
KERNEL_ATA := $(BUILD_DIR)/$(KERNEL_ATA_NAME)
ARM64_KERNEL := $(BUILD_DIR)/$(ARM64_KERNEL_NAME)
ISO_FILE   := $(BUILD_DIR)/$(ISO_NAME)
ISO_KERNEL_ONLY_FILE := $(BUILD_DIR)/$(ISO_KERNEL_ONLY_NAME)
DISK_IMAGE := $(BUILD_DIR)/$(DISK_NAME)
INIT_ELF   := $(BUILD_USER)/$(INIT_ELF_NAME)
ISO_KERNEL_DIR := $(BUILD_DIR)/iso-kernel-only
GRUB_KERNEL_DIR := $(ISO_KERNEL_DIR)/boot/grub
BOOT_SUPPORT_STAMP := $(BUILD_DIR)/stage/.boot-support.stamp

.PHONY: all
ifeq ($(NUMOS_ARCH),arm64)
all: kernel
else
all: iso
endif

.PHONY: check-arch
check-arch:
ifeq ($(ARCH_BUILD_READY),1)
	@true
else
	@echo "[ERROR] $(ARCH_STATUS)"
	@false
endif

.PHONY: arch-status
arch-status:
	@echo "NUMOS_ARCH=$(NUMOS_ARCH)"
	@echo "NUMOS_MACHINE=$(NUMOS_MACHINE)"
	@echo "NUMOS_TARGET=$(NUMOS_TARGET)"
	@echo "NUMOS_QEMU=$(NUMOS_QEMU)"
	@echo "Boot protocol: $(NUMOS_BOOT_PROTOCOL_NAME)"
	@echo "Status: $(ARCH_STATUS)"

.PHONY: check-host-tools
check-host-tools:
ifeq ($(NUMOS_ARCH),x86_64)
	@if ! command -v $(NUMOS_AS) >/dev/null 2>&1; then \
		echo "[ERROR] Missing assembler: $(NUMOS_AS)"; \
		echo "Install nasm or yasm, or set NUMOS_AS=/path/to/nasm"; \
		false; \
	fi
endif
	@if ! command -v $(NUMOS_CC) >/dev/null 2>&1; then \
		echo "[ERROR] Missing compiler: $(NUMOS_CC)"; \
		echo "Install $(NUMOS_TARGET)-gcc or another usable cross compiler, or set NUMOS_CC=/path/to/compiler"; \
		false; \
	fi
	@if ! command -v $(NUMOS_LD) >/dev/null 2>&1; then \
		echo "[ERROR] Missing linker: $(NUMOS_LD)"; \
		echo "Install $(NUMOS_TARGET)-ld or another usable linker, or set NUMOS_LD=/path/to/linker"; \
		false; \
	fi

.PHONY: check-iso-tools
check-iso-tools:
	@if ! command -v grub-mkrescue >/dev/null 2>&1; then \
		echo "[ERROR] Missing ISO tool: grub-mkrescue"; \
		echo "Ubuntu or Debian: sudo apt update && sudo apt install grub-common grub-pc-bin xorriso mtools"; \
		false; \
	fi
	@if ! command -v xorriso >/dev/null 2>&1; then \
		echo "[ERROR] Missing ISO tool: xorriso"; \
		echo "Ubuntu or Debian: sudo apt update && sudo apt install xorriso"; \
		false; \
	fi
	@if ! command -v mformat >/dev/null 2>&1; then \
		echo "[ERROR] Missing ISO tool: mformat"; \
		echo "Ubuntu or Debian: sudo apt update && sudo apt install mtools"; \
		false; \
	fi

# ---- Kernel ----------------------------------------------------------------
.PHONY: kernel
ifeq ($(NUMOS_ARCH),arm64)
kernel: check-arch check-host-tools $(ARM64_KERNEL)

$(BUILD_KERNEL)/boot/arm64/%.o: $(SRC_DIR)/boot/arm64/%.S
	@echo "[CC]  $<"
	@mkdir -p $(dir $@)
	@$(NUMOS_CC) $(KERNEL_CFLAGS) $< -o $@

$(BUILD_KERNEL)/%.o: $(SRC_DIR)/%.c
	@echo "[CC]  $<"
	@mkdir -p $(dir $@)
	@$(NUMOS_CC) $(KERNEL_CFLAGS) $< -o $@

$(ARM64_KERNEL): $(ARM64_OBJECTS)
	@echo "[LD]  kernel-arm64"
	@mkdir -p $(BUILD_DIR)
	@$(NUMOS_LD) $(LDFLAGS) -o $@ $^
	@echo "[OK]  $(ARM64_KERNEL)"

.PHONY: user_space
user_space:
	@echo "[ERROR] ARM64 user-space is not wired into the kernel yet. See $(ARCH_DOC)."
	@false

.PHONY: boot-support
boot-support:
	@echo "[ERROR] ARM64 does not use the x86 boot-support staging path."
	@false

.PHONY: disk
disk:
	@echo "[ERROR] ARM64 disk image packaging is not implemented yet."
	@false

.PHONY: partition-list
partition-list:
	@python3 $(TOOLS_DIR)/partition_storage.py list

.PHONY: partition
partition:
	@python3 $(TOOLS_DIR)/partition_storage.py create $(PART_TARGET) \
		--table $(PART_TABLE) \
		--fs $(PART_FS) \
		--start $(PART_START) \
		--end $(PART_END) \
		$(if $(filter 1,$(PART_FORMAT)),--format,) \
		$(if $(filter 1,$(PART_APPLY)),--apply,) \
		$(if $(filter 0,$(PART_POPULATE)),--no-populate-numos,)

.PHONY: iso-kernel-only
iso-kernel-only:
	@echo "[ERROR] ARM64 does not use the GRUB ISO flow."
	@false

.PHONY: iso
iso:
	@echo "[ERROR] ARM64 does not use the GRUB ISO flow."
	@false

.PHONY: pkg-download
pkg-download:
	@if [ -z "$(PKG_URL)" ]; then \
		echo "Usage: make pkg-download PKG_URL=https://example.com/OCLDEV.PKG"; \
		false; \
	fi
	@python3 $(TOOLS_DIR)/download_pkg.py "$(PKG_URL)" --stage-dir "$(BOOT_STAGE_DIR)" $(if $(PKG_BASE_URL),--base-url "$(PKG_BASE_URL)",)

.PHONY: run
run: kernel
	@echo "[QEMU] Starting NumOS ARM64..."
	@$(NUMOS_QEMU) \
		-machine virt \
		-cpu cortex-a72 \
		-m 1024 \
		-nographic \
		-kernel $(ARM64_KERNEL)

.PHONY: run-partition
run-partition:
	@echo "[ERROR] ARM64 run-partition is not implemented."
	@false

.PHONY: run-nographic
run-nographic: run

.PHONY: debug
debug: kernel
	@$(NUMOS_QEMU) \
		-machine virt \
		-cpu cortex-a72 \
		-m 1024 \
		-nographic \
		-kernel $(ARM64_KERNEL) \
		-s -S
else
kernel: check-arch check-host-tools $(KERNEL) $(KERNEL_VESA)

$(BUILD_KERNEL)/boot/%.o: $(SRC_DIR)/boot/%.asm
	@echo "[AS]  $<"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(NUMOS_AS) $(ASFLAGS) $< -o $@

# multiboot_header.asm gets its own rule so NASM receives -D ENABLE_FRAMEBUFFER.
# config.h is listed as a dependency so changing NUMOS_ENABLE_FRAMEBUFFER
# automatically triggers a rebuild of the header - without this Make reuses
# the old .o and the framebuffer tag is silently missing from the kernel.
$(MULTIBOOT_OBJECT): $(SRC_DIR)/boot/multiboot_header.asm Include/kernel/config.h $(MULTIBOOT_FLAGS)
	@echo "[AS]  $< (FB=$(FB_ENABLED))"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(NUMOS_AS) $(ASFLAGS_MULTIBOOT) $< -o $@

$(MULTIBOOT_OBJECT_VESA): $(SRC_DIR)/boot/multiboot_header.asm Include/kernel/config.h
	@echo "[AS]  $< (FB=1 forced)"
	@mkdir -p $(BUILD_KERNEL)/boot
	@$(NUMOS_AS) $(ASFLAGS_MULTIBOOT_VESA) $< -o $@

$(MULTIBOOT_FLAGS):
	@mkdir -p $(BUILD_KERNEL)/boot
	@printf '%s\n' "VBE_TAG=$(VBE_TAG)" "FORCE_VBE=$(FORCE_VBE)" > $(MULTIBOOT_FLAGS)

$(TRAMPOLINE_BIN): $(TRAMPOLINE_SRC)
	@echo "[AS]  $<"
	@mkdir -p $(dir $@)
	@$(NUMOS_AS) -f bin $< -o $@

$(TRAMPOLINE_OBJ): $(TRAMPOLINE_BIN)
	@echo "[LD]  ap_trampoline"
	@$(NUMOS_LD) -r -b binary -o $@ $<

$(BUILD_KERNEL)/%.o: $(SRC_DIR)/%.c
	@echo "[CC]  $<"
	@mkdir -p $(dir $@)
	@$(NUMOS_CC) $(KERNEL_CFLAGS) $< -o $@

$(BUILD_KERNEL_ATA)/%.o: $(SRC_DIR)/%.c
	@echo "[CC]  $< (ATA/VGA)"
	@mkdir -p $(dir $@)
	@$(NUMOS_CC) $(KERNEL_CFLAGS) -DNUMOS_ENABLE_FRAMEBUFFER=0 -DNUMOS_FB_DISABLE_ON_VBOX=1 $< -o $@

$(KERNEL): $(COMMON_KERNEL_OBJECTS) $(MULTIBOOT_OBJECT)
	@echo "[LD]  kernel"
	@$(NUMOS_LD) $(LDFLAGS) -o $@ $^
	@echo "[OK]  $(KERNEL)"

$(KERNEL_VESA): $(COMMON_KERNEL_OBJECTS) $(MULTIBOOT_OBJECT_VESA)
	@echo "[LD]  kernel-vesa"
	@$(NUMOS_LD) $(LDFLAGS) -o $@ $^
	@echo "[OK]  $(KERNEL_VESA)"

$(KERNEL_ATA): $(ASM_OBJECTS) $(KERNEL_C_OBJECTS_ATA) $(TRAMPOLINE_OBJ) $(MULTIBOOT_OBJECT)
	@echo "[LD]  kernel-ata"
	@$(NUMOS_LD) $(LDFLAGS) -o $@ $^
	@echo "[OK]  $(KERNEL_ATA)"

.PHONY: user_space
user_space: check-arch check-host-tools
	@$(MAKE) -C $(USER_DIR) install \
		NUMOS_ARCH=$(NUMOS_ARCH) \
		NUMOS_TARGET=$(NUMOS_TARGET) \
		NUMOS_AS=$(NUMOS_AS) \
		NUMOS_CC=$(NUMOS_CC) \
		NUMOS_LD=$(NUMOS_LD)

.PHONY: boot-support
boot-support: check-arch $(BOOT_SUPPORT_STAMP)

$(BOOT_SUPPORT_STAMP): $(KERNEL) $(KERNEL_VESA) $(TOOLS_DIR)/build_boot_support.py
	@mkdir -p $(dir $(BOOT_SUPPORT_STAMP)) $(BOOT_STAGE_DIR)
	@python3 $(TOOLS_DIR)/build_boot_support.py \
		--output-dir $(BOOT_STAGE_DIR) \
		--kernel $(KERNEL_VESA)
	@touch $@

.PHONY: pkg-download
pkg-download:
	@if [ -z "$(PKG_URL)" ]; then \
		echo "Usage: make pkg-download PKG_URL=https://example.com/OCLDEV.PKG"; \
		false; \
	fi
	@python3 $(TOOLS_DIR)/download_pkg.py "$(PKG_URL)" --stage-dir "$(BOOT_STAGE_DIR)" $(if $(PKG_BASE_URL),--base-url "$(PKG_BASE_URL)",)

# ---- Disk image ------------------------------------------------------------
.PHONY: disk
disk: user_space $(KERNEL) boot-support
	@mkdir -p $(BUILD_DIR)
	@NUMOS_PACK_USER_ELF=$(NUMOS_PACK_USER_ELF) \
		python3 $(TOOLS_DIR)/create_disk.py $(DISK_IMAGE) $(INIT_ELF) $(INIT_ELF_NAME)
	@echo "[OK]  $(DISK_IMAGE)"

.PHONY: partition-list
partition-list:
	@python3 $(TOOLS_DIR)/partition_storage.py list

.PHONY: partition
partition:
	@python3 $(TOOLS_DIR)/partition_storage.py create $(PART_TARGET) \
		--table $(PART_TABLE) \
		--fs $(PART_FS) \
		--start $(PART_START) \
		--end $(PART_END) \
		$(if $(filter 1,$(PART_FORMAT)),--format,) \
		$(if $(filter 1,$(PART_APPLY)),--apply,) \
		$(if $(filter 0,$(PART_POPULATE)),--no-populate-numos,)

# ---- Kernel-only ISO (no ramdisk module) ----------------------------------
.PHONY: iso-kernel-only
iso-kernel-only: check-arch check-iso-tools $(ISO_KERNEL_ONLY_FILE)

$(ISO_KERNEL_ONLY_FILE): $(KERNEL) $(KERNEL_VESA)
	@mkdir -p $(GRUB_KERNEL_DIR)
	@cp $(KERNEL) $(ISO_KERNEL_DIR)/boot/$(KERNEL_NAME)
	@cp $(KERNEL_VESA) $(ISO_KERNEL_DIR)/boot/$(KERNEL_VESA_NAME)
	@printf '%s\n' \
	  'set timeout=5' \
	  'set default=0' \
	  'set grubenv_path=""' \
	  'if [ -f (hd0,gpt1)/run/grubenv ]; then' \
	  '    set grubenv_path=(hd0,gpt1)/run/grubenv' \
	  'elif [ -f (hd0,msdos1)/run/grubenv ]; then' \
	  '    set grubenv_path=(hd0,msdos1)/run/grubenv' \
	  'fi' \
	  'if [ -n "$$grubenv_path" ]; then' \
	  '    load_env -f $$grubenv_path saved_entry' \
	  'fi' \
	  'if [ -n "$$saved_entry" ]; then' \
	  '    set default=$$saved_entry' \
	  'fi' \
	  '' \
	  'menuentry "NumOS (ATA disk, VGA)" --id=numos_vga {' \
	  '    if [ -n "$$grubenv_path" ]; then' \
	  '        set saved_entry=numos_vga' \
	  '        save_env -f $$grubenv_path saved_entry' \
	  '    fi' \
	  '    terminal_output console' \
	  '    multiboot2 /boot/$(KERNEL_NAME) init=$(INIT_PATH) gfx=vga' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (ATA disk, VESA)" --id=numos_vesa {' \
	  '    if [ -n "$$grubenv_path" ]; then' \
	  '        set saved_entry=numos_vesa' \
	  '        save_env -f $$grubenv_path saved_entry' \
	  '    fi' \
	  '    insmod all_video' \
	  '    insmod vbe' \
	  '    insmod vga' \
	  '    insmod gfxterm' \
	  '    insmod font' \
	  '    set gfxmode=1024x768x32,1280x720x32,800x600x32,640x480x32' \
	  '    set gfxpayload=keep' \
	  '    if loadfont unicode; then' \
	  '        terminal_output gfxterm' \
	  '    fi' \
	  '    multiboot2 /boot/$(KERNEL_VESA_NAME) init=$(INIT_PATH) gfx=vesa' \
	  '    boot' \
	  '}' \
	  > $(GRUB_KERNEL_DIR)/grub.cfg
	@grub-mkrescue -o $(ISO_KERNEL_ONLY_FILE) $(ISO_KERNEL_DIR)
	@echo "[OK]  $(ISO_KERNEL_ONLY_FILE)"

# ---- ISO -------------------------------------------------------------------
.PHONY: iso
iso: check-arch check-iso-tools $(ISO_FILE)

$(ISO_FILE): $(KERNEL) $(KERNEL_VESA) disk
	@mkdir -p $(GRUB_DIR)
	@cp $(KERNEL)     $(ISO_DIR)/boot/$(KERNEL_NAME)
	@cp $(KERNEL_VESA) $(ISO_DIR)/boot/$(KERNEL_VESA_NAME)
	@cp $(DISK_IMAGE) $(ISO_DIR)/boot/$(DISK_NAME)
	@echo "[GRUB] Generating grub.cfg (FB=$(FB_ENABLED) VBE=$(GRUB_GFX_ENABLED))"
ifeq ($(GRUB_GFX_ENABLED),1)
	@printf '%s\n' \
	  'set timeout=5' \
	  'set default=0' \
	  'insmod all_video' \
	  'insmod vbe' \
	  'insmod vga' \
	  'insmod gfxterm' \
	  'insmod font' \
	  '' \
	  'menuentry "NumOS (VGA default)" {' \
	  '    terminal_output console' \
	  '    multiboot2 /boot/$(KERNEL_NAME) init=$(INIT_PATH) gfx=vga' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (VESA $(GRUB_VESA_MODE))" {' \
	  '    set gfxmode=$(GRUB_VESA_MODE)' \
	  '    set gfxpayload=keep' \
	  '    if loadfont unicode; then' \
	  '        terminal_output gfxterm' \
	  '    fi' \
	  '    multiboot2 /boot/$(KERNEL_VESA_NAME) init=$(INIT_PATH) gfx=vesa' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (VESA 1280x720)" {' \
	  '    set gfxmode=1280x720x32,1280x800x32,1024x768x32,800x600x32' \
	  '    set gfxpayload=keep' \
	  '    if loadfont unicode; then' \
	  '        terminal_output gfxterm' \
	  '    fi' \
	  '    multiboot2 /boot/$(KERNEL_VESA_NAME) init=$(INIT_PATH) gfx=vesa' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (VESA 1920x0)" {' \
	  '    set gfxmode=1920x1080x32,1280x800x32,1280x720x32,1024x768x32' \
	  '    set gfxpayload=keep' \
	  '    if loadfont unicode; then' \
	  '        terminal_output gfxterm' \
	  '    fi' \
	  '    multiboot2 /boot/$(KERNEL_VESA_NAME) init=$(INIT_PATH) gfx=vesa' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (VESA 800x600 safe)" {' \
	  '    set gfxmode=800x600x32,1024x768x32,640x480x32,800x600x16' \
	  '    set gfxpayload=keep' \
	  '    if loadfont unicode; then' \
	  '        terminal_output gfxterm' \
	  '    fi' \
	  '    multiboot2 /boot/$(KERNEL_VESA_NAME) init=$(INIT_PATH) gfx=vesa' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (BGA)" {' \
	  '    terminal_output console' \
	  '    multiboot2 /boot/$(KERNEL_NAME) init=$(INIT_PATH) gfx=bga' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  > $(GRUB_DIR)/grub.cfg
else
	@printf '%s\n' \
	  'set timeout=5' \
	  'set default=0' \
	  '' \
	  'menuentry "NumOS (VGA default)" {' \
	  '    terminal_output console' \
	  '    multiboot2 /boot/$(KERNEL_NAME) init=$(INIT_PATH) gfx=vga' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (BGA)" {' \
	  '    terminal_output console' \
	  '    multiboot2 /boot/$(KERNEL_NAME) init=$(INIT_PATH) gfx=bga' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  '' \
	  'menuentry "NumOS (VESA $(GRUB_VESA_MODE))" {' \
	  '    insmod all_video' \
	  '    insmod vbe' \
	  '    insmod vga' \
	  '    insmod gfxterm' \
	  '    insmod font' \
	  '    set gfxmode=$(GRUB_VESA_MODE)' \
	  '    set gfxpayload=keep' \
	  '    if loadfont unicode; then' \
	  '        terminal_output gfxterm' \
	  '    fi' \
	  '    multiboot2 /boot/$(KERNEL_VESA_NAME) init=$(INIT_PATH) gfx=vesa' \
	  '    module2    /boot/$(DISK_NAME) disk' \
	  '    boot' \
	  '}' \
	  > $(GRUB_DIR)/grub.cfg
endif
	@grub-mkrescue -o $(ISO_FILE) $(ISO_DIR)
	@echo "[OK]  $(ISO_FILE)  (FB=$(FB_ENABLED))"

# ---- QEMU ------------------------------------------------------------------
#
# index=0  -> primary master   (bus 0x1F0): disk.img  - ATA driver reads here
# index=2  -> secondary master (bus 0x170): ISO/CDROM - completely separate bus
#
# -vga std   exposes the BGA / Bochs VGA device which GRUB's VBE driver can
#            use.  Do NOT use -vga none as that disables the framebuffer.
#
.PHONY: run
run: iso
	@echo "[QEMU] Starting NumOS..."
	@$(NUMOS_QEMU) \
		-m 4096 \
		-smp 2 \
		-vga std \
		-display gtk \
		-boot d \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=2 \
		-serial stdio

.PHONY: run-partition
run-partition: iso-kernel-only
	@echo "[QEMU] Starting NumOS with partitioned disk: $(PART_TARGET)"
	@$(NUMOS_QEMU) \
		-m 4096 \
		-smp 2 \
		-vga std \
		-display gtk \
		-boot d \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-drive file=$(PART_TARGET),format=raw,if=ide,index=0 \
		-drive file=$(ISO_KERNEL_ONLY_FILE),if=ide,media=cdrom,index=2 \
		-serial stdio

.PHONY: run-nographic
run-nographic: iso
	$(NUMOS_QEMU) -m 128M \
		-vga std \
		-boot d \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=2 \
		-nographic

# ---- GDB -------------------------------------------------------------------
.PHONY: debug
debug: iso
	@$(NUMOS_QEMU) -m 128M \
		-vga std \
		-boot d \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-drive file=$(DISK_IMAGE),format=raw,if=ide,index=0 \
		-drive file=$(ISO_FILE),if=ide,media=cdrom,index=2 \
		-serial stdio -s -S
endif

# ---- VirtualBox ------------------------------------------------------------
# Attach NumOS.iso as optical drive only.  disk.img is embedded inside as
# a multiboot2 module; the ramdisk driver supplies all FAT32 sector reads.
# Display adapter: VBoxVGA (not VMSVGA or VBoxSVGA) for VBE support.

# ---- Clean -----------------------------------------------------------------
.PHONY: clean
clean:
	@rm -rf $(BUILD_DIR) $(ISO_FILE)
	@$(MAKE) -C $(USER_DIR) clean

.PHONY: help
help:
	@echo "NumOS Build System"
ifeq ($(NUMOS_ARCH),arm64)
	@echo "  make kernel NUMOS_ARCH=arm64 - build the ARM64 QEMU virt kernel"
	@echo "  make run NUMOS_ARCH=arm64    - boot the ARM64 kernel in QEMU"
	@echo "  make debug NUMOS_ARCH=arm64  - boot ARM64 with a GDB stub"
else
	@echo "  make run   - QEMU: disk.img on primary IDE, ISO on secondary IDE"
	@echo "  make run-partition PART_TARGET=build/disk.img"
	@echo "  make debug - same + GDB stub on :1234"
endif
	@echo "  make arch-status - print current architecture support state"
	@echo "  make partition-list - list host block devices"
	@echo "  make partition PART_TARGET=/dev/sdX PART_APPLY=1 PART_FORMAT=1"
	@echo "  make partition PART_TARGET=build/disk.img PART_APPLY=1 PART_FORMAT=1"
	@echo "    add PART_POPULATE=0 to keep filesystem empty"
	@echo "  make pkg-download PKG_URL=https://example.com/OCLDEV.PKG"
	@echo "  make clean - remove build artefacts"
	@echo "OS: $(OS_TYPE)"
	@echo "Architecture: $(NUMOS_ARCH)"
	@echo "Status: $(ARCH_STATUS)"
	@echo ""
ifeq ($(NUMOS_ARCH),arm64)
	@echo "ARM64 display: early bring up uses the PL011 serial console on QEMU virt."
	@echo "Storage, MMU, and user mode are still in progress."
else
	@echo "VESA/VBE display: GRUB reads the framebuffer tag from the multiboot"
	@echo "header and calls INT 0x10 to set the mode before booting the kernel."
	@echo "The active mode is reported at boot and accessible via key [V]."
endif
