################################################################################
#  NUMOS USER-SPACE MAKEFILE  (user/Makefile)
################################################################################

NUMOS_ARCH ?= x86_64
ARCH_BUILD_READY := 0
ARCH_STATUS := Unsupported NUMOS_ARCH=$(NUMOS_ARCH). Supported values: x86_64, arm64.
NUMOS_TARGET_TRIPLE :=
USER_ASFORMAT :=
USER_CFLAGS_ARCH :=
USER_LDFLAGS_ARCH :=

ifeq ($(NUMOS_ARCH),x86_64)
ARCH_BUILD_READY := 1
ARCH_STATUS := Ready. User-space ELFs target AMD64 and Intel x86-64.
NUMOS_TARGET_TRIPLE := x86_64-elf
USER_ASFORMAT := elf64
USER_CFLAGS_ARCH := -m64 -fno-tree-vectorize -mno-mmx -msse -msse2 -mfpmath=sse
USER_LDFLAGS_ARCH := -m elf_x86_64
else ifeq ($(NUMOS_ARCH),arm64)
ARCH_STATUS := Planned. NumOS user-space is still x86-64 only. See ../docs/PORTING_RPI5_ARM64.md.
NUMOS_TARGET_TRIPLE := aarch64-none-elf
else
NUMOS_TARGET_TRIPLE := $(NUMOS_ARCH)-elf
endif

NUMOS_TARGET ?= $(NUMOS_TARGET_TRIPLE)
NUMOS_AS  ?= $(or $(shell command -v nasm 2>/dev/null),$(shell command -v yasm 2>/dev/null))
NUMOS_CC  ?= $(or $(shell command -v $(NUMOS_TARGET)-gcc 2>/dev/null),$(shell command -v gcc 2>/dev/null),$(shell command -v clang 2>/dev/null))
NUMOS_LD  ?= $(or $(shell command -v $(NUMOS_TARGET)-ld 2>/dev/null),$(shell command -v ld 2>/dev/null),$(shell command -v ld.lld 2>/dev/null))

USER_CFLAGS := $(USER_CFLAGS_ARCH) -ffreestanding -fstack-protector-strong \
               -mstack-protector-guard=global -fno-pic \
               -fno-builtin -nostdlib -nostartfiles -nodefaultlibs \
               -U__linux__ \
               -Wall -Wextra -O2 -Iinclude

USER_LDFLAGS := $(USER_LDFLAGS_ARCH) -nostdlib -static -T linker/linker.ld \
                --entry=_start --build-id=none -z noexecstack

# Root Makefile expects ELFs at ../build/user/*.elf
OUTPUT_DIR ?= ../build/user

TOPLEVEL_SRCS := $(shell find . -maxdepth 1 -type f -name '*.c' -printf '%P\n' | sort)
COMMON_SRCS   := runtime/runtime.c runtime/libc.c
PROGRAM_TOPLEVEL_SRCS := $(filter-out $(COMMON_SRCS),$(TOPLEVEL_SRCS))
PROGRAM_FILE_SRCS := $(shell find programs -mindepth 1 -maxdepth 1 -type f -name '*.c' -printf '%P\n' | sort)
PROGRAM_BUNDLES := $(shell find programs -mindepth 1 -maxdepth 1 -type d ! -name '.*' ! -name build -printf '%P\n' | sort)
DIRECT_PROGS := $(sort $(basename $(notdir $(PROGRAM_TOPLEVEL_SRCS) $(PROGRAM_FILE_SRCS))))
USER_ELFS    := $(addprefix $(OUTPUT_DIR)/,$(addsuffix .elf,$(DIRECT_PROGS) $(PROGRAM_BUNDLES)))
COMMON_OBJS   := $(addprefix build/,$(COMMON_SRCS:.c=.o))

define DIRECT_PROGRAM_template
PROGRAM_$(1)_SRC := $(2)
PROGRAM_$(1)_OBJ := build/$(2:.c=.o)

$(OUTPUT_DIR)/$(1).elf: build/enteryusr.o $(COMMON_OBJS) $$(PROGRAM_$(1)_OBJ) | $(OUTPUT_DIR)
	@echo "[LD user] $$@"
	@$(NUMOS_LD) $(USER_LDFLAGS) -o $$@ build/enteryusr.o $(COMMON_OBJS) $$(PROGRAM_$(1)_OBJ)
	@echo "[INFO] $$@: $$(wc -c < $$@) bytes, entry=0x60000000"
endef

define BUNDLE_PROGRAM_template
PROGRAM_$(1)_DIRS := $(shell find programs/$(1) -type d ! -path '*/.*' ! -path '*/build*' | sort)
PROGRAM_$(1)_SRCS := $(shell find programs/$(1) -type f -name '*.c' ! -path '*/.*/*' ! -path '*/build/*' | sort)
PROGRAM_$(1)_PORT_DIRS := $(shell find ports/$(1) -type d ! -path '*/.*' ! -path '*/build*' 2>/dev/null | sort)
PROGRAM_$(1)_PORT_SRCS := $(shell find ports/$(1) -type f -name '*.c' ! -path '*/.*/*' ! -path '*/build/*' 2>/dev/null | sort)
PROGRAM_$(1)_OBJS := $$(patsubst %.c,build/%.o,$$(PROGRAM_$(1)_SRCS))
PROGRAM_$(1)_OBJS += $$(patsubst %.c,build/%.o,$$(PROGRAM_$(1)_PORT_SRCS))
PROGRAM_$(1)_INCLUDES := $$(addprefix -I,$$(PROGRAM_$(1)_DIRS) $$(PROGRAM_$(1)_PORT_DIRS))

$$(PROGRAM_$(1)_OBJS): EXTRA_INCLUDES += $$(PROGRAM_$(1)_INCLUDES)

$(OUTPUT_DIR)/$(1).elf: build/enteryusr.o $(COMMON_OBJS) $$(PROGRAM_$(1)_OBJS) | $(OUTPUT_DIR)
	@echo "[LD user] $$@"
	@$(NUMOS_LD) $(USER_LDFLAGS) -o $$@ build/enteryusr.o $(COMMON_OBJS) $$(PROGRAM_$(1)_OBJS)
	@echo "[INFO] $$@: $$(wc -c < $$@) bytes, entry=0x60000000"
endef

$(foreach src,$(PROGRAM_TOPLEVEL_SRCS),$(eval $(call DIRECT_PROGRAM_template,$(basename $(notdir $(src))),$(src))))
$(foreach src,$(PROGRAM_FILE_SRCS),$(eval $(call DIRECT_PROGRAM_template,$(basename $(notdir $(src))),programs/$(src))))
$(foreach bundle,$(PROGRAM_BUNDLES),$(eval $(call BUNDLE_PROGRAM_template,$(bundle))))

# ---- Targets ----------------------------------------------------------------

.PHONY: all
all: check-arch $(USER_ELFS)

.PHONY: check-arch
check-arch:
ifeq ($(ARCH_BUILD_READY),1)
	@true
else
	@echo "[ERROR] $(ARCH_STATUS)"
	@false
endif

$(OUTPUT_DIR):
	@mkdir -p $(OUTPUT_DIR)

# ---- Assembly entry object --------------------------------------------------

build/enteryusr.o: runtime/entry.asm
	@echo "[AS user] runtime/entry.asm"
	@mkdir -p build
	@$(NUMOS_AS) -f $(USER_ASFORMAT) runtime/entry.asm -o build/enteryusr.o

# ---- Compile: top-level sources --------------------------------------------

build/%.o: %.c include/syscalls.h
	@echo "[CC user] $<"
	@mkdir -p $(dir $@)
	@$(NUMOS_CC) $(USER_CFLAGS) $(EXTRA_INCLUDES) -c $< -o $@

# ---- Install ----------------------------------------------------------------

.PHONY: install
install: check-arch all

# ---- Clean ------------------------------------------------------------------

.PHONY: clean
clean:
	@rm -rf build $(OUTPUT_DIR)
