################################################################################
#  NUMOS USER-SPACE MAKEFILE  (user/Makefile)
################################################################################

NUMOS_ARCH ?= x86_64
HOST_CPU_COUNT := $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
NUMOS_BUILD_JOBS ?= $(HOST_CPU_COUNT)
NUMOS_OPT_LEVEL ?= 3
NUMOS_COMMON_OPT_FLAGS ?= -O$(NUMOS_OPT_LEVEL) -pipe
ARCH_BUILD_READY := 0
ARCH_STATUS := Unsupported NUMOS_ARCH=$(NUMOS_ARCH). Supported values: x86_64, arm64.
NUMOS_TARGET_TRIPLE :=
USER_ASFORMAT :=
USER_CFLAGS_ARCH :=
USER_LDFLAGS_ARCH :=
USER_LINK_MODE ?= pie
USER_ENTRY_SOURCE := runtime/entry.asm
USER_ENTRY_OBJECT := build/enteryusr.o
USER_LINKER_SCRIPT_STD := linker/linker.ld
USER_LINKER_SCRIPT_PIE := linker/linker-pie.ld

ifeq ($(NUMOS_ARCH),x86_64)
ARCH_BUILD_READY := 1
ARCH_STATUS := Ready. User-space ELFs target AMD64 and Intel x86-64.
NUMOS_TARGET_TRIPLE := x86_64-elf
USER_ASFORMAT := elf64
USER_CFLAGS_ARCH := -m64 -fno-tree-vectorize -mno-mmx -msse -msse2 -mfpmath=sse
USER_LDFLAGS_ARCH := -m elf_x86_64
else ifeq ($(NUMOS_ARCH),arm64)
ARCH_BUILD_READY := 1
ARCH_STATUS := Preview. ARM64 user-space objects build with an AArch64 runtime and linker scripts.
NUMOS_TARGET_TRIPLE := aarch64-none-elf
USER_ENTRY_SOURCE := runtime/entry_arm64.S
USER_LINKER_SCRIPT_STD := linker/linker-arm64.ld
USER_LINKER_SCRIPT_PIE := linker/linker-pie-arm64.ld
USER_LDFLAGS_ARCH := -m aarch64elf
else
NUMOS_TARGET_TRIPLE := $(NUMOS_ARCH)-elf
endif

NUMOS_TARGET ?= $(NUMOS_TARGET_TRIPLE)
NUMOS_AS  ?= $(or $(shell command -v nasm 2>/dev/null),$(shell command -v yasm 2>/dev/null))
ifeq ($(NUMOS_ARCH),arm64)
NUMOS_CC  ?= $(or $(shell command -v $(NUMOS_TARGET)-gcc 2>/dev/null),$(shell command -v aarch64-linux-gnu-gcc 2>/dev/null),$(NUMOS_TARGET)-gcc)
NUMOS_LD  ?= $(or $(shell command -v $(NUMOS_TARGET)-ld 2>/dev/null),$(shell command -v aarch64-linux-gnu-ld 2>/dev/null),$(NUMOS_TARGET)-ld)
else
NUMOS_CC  ?= $(or $(shell command -v $(NUMOS_TARGET)-gcc 2>/dev/null),$(shell command -v gcc 2>/dev/null),$(shell command -v clang 2>/dev/null))
NUMOS_LD  ?= $(or $(shell command -v $(NUMOS_TARGET)-ld 2>/dev/null),$(shell command -v ld 2>/dev/null),$(shell command -v ld.lld 2>/dev/null))
endif

USER_LINKER_SCRIPT := $(USER_LINKER_SCRIPT_STD)

ifeq ($(USER_LINK_MODE),pie)
USER_CFLAGS_MODE := -fpie
USER_LDFLAGS_MODE := -pie --no-dynamic-linker -T $(USER_LINKER_SCRIPT_PIE)
USER_LINKER_SCRIPT := $(USER_LINKER_SCRIPT_PIE)
else
USER_CFLAGS_MODE := -fno-pic
USER_LDFLAGS_MODE := -static -T $(USER_LINKER_SCRIPT_STD)
endif

USER_CFLAGS := $(USER_CFLAGS_ARCH) -ffreestanding -fstack-protector-strong \
               -mstack-protector-guard=global $(USER_CFLAGS_MODE) \
               -ftls-model=local-exec \
                -fno-builtin -nostdlib -nostartfiles -nodefaultlibs \
                -U__linux__ \
               -Wall -Wextra $(NUMOS_COMMON_OPT_FLAGS) -Iinclude

USER_LDFLAGS := $(USER_LDFLAGS_ARCH) -nostdlib $(USER_LDFLAGS_MODE) \
                --entry=_start --build-id=none -z noexecstack

# Root Makefile expects ELFs at ../build/user/*.elf
OUTPUT_DIR ?= ../build/user
STAGED_BIN_DIR ?= files/bin
PREINSTALLED_BIN_CANDIDATES := connect shell install pkg numloss empty edit proc see tcp usb

TOPLEVEL_SRCS := $(shell find . -maxdepth 1 -type f -name '*.c' -printf '%P\n' | sort)
COMMON_SRCS   := runtime/runtime.c runtime/libc.c
PROGRAM_TOPLEVEL_SRCS := $(filter-out $(COMMON_SRCS),$(TOPLEVEL_SRCS))
PROGRAM_FILE_SRCS := $(shell find programs -mindepth 1 -maxdepth 1 -type f -name '*.c' -printf '%P\n' | sort)
PROGRAM_BUNDLES := $(shell find programs -mindepth 1 -maxdepth 1 -type d ! -name '.*' ! -name build -printf '%P\n' | sort)
DIRECT_PROGS := $(sort $(basename $(notdir $(PROGRAM_TOPLEVEL_SRCS) $(PROGRAM_FILE_SRCS))))
AVAILABLE_USER_BINS := $(sort $(DIRECT_PROGS) $(PROGRAM_BUNDLES))
PREINSTALLED_BINS := $(strip $(foreach bin,$(PREINSTALLED_BIN_CANDIDATES),$(if $(filter $(bin),$(AVAILABLE_USER_BINS)),$(bin))))
PREINSTALLED_ELFS := $(addprefix $(OUTPUT_DIR)/,$(addsuffix .elf,$(PREINSTALLED_BINS)))
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

# Fallback for direct programs stored as programs/*.c.
# This keeps stage-bin working even if source discovery misses a file.
$(OUTPUT_DIR)/%.elf: build/enteryusr.o $(COMMON_OBJS) build/programs/%.o | $(OUTPUT_DIR)
	@echo "[LD user] $@"
	@$(NUMOS_LD) $(USER_LDFLAGS) -o $@ build/enteryusr.o $(COMMON_OBJS) build/programs/$*.o
	@echo "[INFO] $@: $$(wc -c < $@) bytes, entry=0x60000000"

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

$(STAGED_BIN_DIR):
	@mkdir -p $(STAGED_BIN_DIR)

# ---- Assembly entry object --------------------------------------------------

build/enteryusr.o: $(USER_ENTRY_SOURCE)
ifeq ($(NUMOS_ARCH),x86_64)
	@echo "[AS user] runtime/entry.asm"
	@mkdir -p build
	@$(NUMOS_AS) -f $(USER_ASFORMAT) runtime/entry.asm -o build/enteryusr.o
else
	@echo "[CC user] $(USER_ENTRY_SOURCE)"
	@mkdir -p build
	@$(NUMOS_CC) $(USER_CFLAGS) -c $(USER_ENTRY_SOURCE) -o build/enteryusr.o
endif

# ---- Compile: top-level sources --------------------------------------------

build/%.o: %.c include/syscalls.h
	@echo "[CC user] $<"
	@mkdir -p $(dir $@)
	@$(NUMOS_CC) $(USER_CFLAGS) $(EXTRA_INCLUDES) -c $< -o $@

# ---- Install ----------------------------------------------------------------

.PHONY: stage-bin
stage-bin: check-arch sanitize-preinstalled-elfs $(PREINSTALLED_ELFS) | $(STAGED_BIN_DIR)

.PHONY: sanitize-preinstalled-elfs
sanitize-preinstalled-elfs:
	@set -e; \
	for elf in $(PREINSTALLED_ELFS); do \
		if [ -e "$$elf" ] && [ ! -f "$$elf" ]; then \
			rm -rf "$$elf"; \
		fi; \
	done

stage-bin:
	@set -e; \
	for bin in $(PREINSTALLED_BINS); do \
		src="$(OUTPUT_DIR)/$$bin.elf"; \
		dst="$(STAGED_BIN_DIR)/$$bin.elf"; \
		if [ ! -f "$$src" ]; then \
			echo "[ERROR] Missing built ELF: $$src"; \
			exit 1; \
		fi; \
		cp "$$src" "$$dst"; \
	done

.PHONY: install
install: check-arch stage-bin

# ---- Clean ------------------------------------------------------------------

.PHONY: clean
clean:
	@rm -rf build $(OUTPUT_DIR)
