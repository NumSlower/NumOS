# Userspace Makefile - FIXED VERSION

CC := $(shell command -v x86_64-elf-gcc || echo /usr/local/cross/bin/x86_64-elf-gcc)
LD := $(shell command -v x86_64-elf-ld || echo /usr/local/cross/bin/x86_64-elf-ld)
AS := nasm

BUILD_DIR := ../build
USER_BUILD := $(BUILD_DIR)/userspace

# Strict userspace flags
USER_CFLAGS := -m64 -ffreestanding -fno-stack-protector -fno-pic \
               -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
               -Wall -Wextra -Werror -c -I../Include -Os \
               -DUSERSPACE -fno-builtin

USER_LDFLAGS := -T user.ld -nostdlib --nmagic

# Source files
USER_SOURCES := shell.c syscalls.c
USER_OBJECTS := $(patsubst %.c,$(USER_BUILD)/%.o,$(USER_SOURCES))
START_OBJECT := $(USER_BUILD)/user_start.o

# Target
SHELL_BIN := $(BUILD_DIR)/shell.bin

.PHONY: all clean verify

all: $(SHELL_BIN) verify

# Compile C sources
$(USER_BUILD)/%.o: %.c
	@echo "[CC-USER] $<"
	@mkdir -p $(USER_BUILD)
	@$(CC) $(USER_CFLAGS) $< -o $@

# Assemble start file
$(START_OBJECT): user_start.asm
	@echo "[AS-USER] $<"
	@mkdir -p $(USER_BUILD)
	@$(AS) -f elf64 $< -o $@

# Link everything
$(SHELL_BIN): $(START_OBJECT) $(USER_OBJECTS)
	@echo "[LD-USER] Linking shell binary..."
	@$(LD) $(USER_LDFLAGS) -o $@ $^
	@echo "[OK] Shell binary created: $(SHELL_BIN)"

# Verify it's a valid ELF
verify: $(SHELL_BIN)
	@echo "[VERIFY] Checking ELF format..."
	@if file $(SHELL_BIN) | grep -q "ELF 64-bit"; then \
		echo "[OK] Valid 64-bit ELF executable"; \
		readelf -h $(SHELL_BIN) | grep -E "Entry|Type|Machine"; \
	else \
		echo "[ERROR] Not a valid 64-bit ELF!"; \
		exit 1; \
	fi
	@echo "[VERIFY] Checking symbols..."
	@nm $(SHELL_BIN) | grep -E "shell_main|_start" || echo "[WARNING] Missing expected symbols"

clean:
	@rm -rf $(USER_BUILD) $(SHELL_BIN)
	@echo "[OK] Userspace cleaned"

.PHONY: info
info:
	@echo "Userspace Build Info:"
	@echo "  CC:          $(CC)"
	@echo "  LD:          $(LD)"
	@echo "  Build dir:   $(USER_BUILD)"
	@echo "  Output:      $(SHELL_BIN)"
	@echo ""
	@echo "Sources:"
	@echo "  $(USER_SOURCES)"
	@echo ""
	@echo "Objects:"
	@echo "  $(USER_OBJECTS)"