# Userspace Makefile
# Builds userspace programs separately from kernel

CC := $(shell command -v x86_64-elf-gcc || echo /usr/local/cross/bin/x86_64-elf-gcc)
LD := $(shell command -v x86_64-elf-ld || echo /usr/local/cross/bin/x86_64-elf-ld)

# Directories
BUILD_DIR = ../build
USER_BUILD = $(BUILD_DIR)/userspace

# Flags for userspace
USER_CFLAGS = -m64 -ffreestanding -fno-stack-protector -fno-pic \
              -mno-mmx -mno-sse -mno-sse2 -mno-red-zone \
              -Wall -Wextra -c -I../Include -Os -DUSERSPACE
USER_LDFLAGS = -T user.ld -nostdlib --nmagic

# Source files
USER_SOURCES = shell.c syscalls.c
USER_OBJECTS = $(patsubst %.c,$(USER_BUILD)/%.o,$(USER_SOURCES))

# Targets
SHELL_BIN = $(BUILD_DIR)/shell.bin

.PHONY: all clean

all: $(SHELL_BIN)

$(USER_BUILD)/%.o: %.c
	@echo "[CC-USER] $<"
	@mkdir -p $(USER_BUILD)
	@$(CC) $(USER_CFLAGS) $< -o $@

$(SHELL_BIN): $(USER_OBJECTS) user_start.o
	@echo "[LD-USER] Linking shell..."
	@$(LD) $(USER_LDFLAGS) -o $@ user_start.o $(USER_OBJECTS)
	@echo "[OK] Shell binary created: $(SHELL_BIN)"

user_start.o: user_start.asm
	@echo "[AS-USER] $<"
	@nasm -f elf64 $< -o $@

clean:
	@rm -rf $(USER_BUILD) $(SHELL_BIN) user_start.o
	@echo "[OK] Userspace cleaned"