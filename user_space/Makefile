################################################################################
#  USER SPACE BUILD — NumOS
#
#  Produces main.elf: a static, position-dependent ELF64 with no libc.
#  The kernel's ELF loader reads this at runtime from /init/SHELL on FAT32.
#
#  Uses pure assembly (crt0.S) to display "Hello World From User space"
#  and then halt via SYS_EXIT.
#
#  Usage:
#    make          — build main.elf
#    make clean    — remove build artefacts
################################################################################

CC  := x86_64-elf-gcc
AS  := x86_64-elf-as
LD  := x86_64-elf-ld

# ---------------------------------------------------------------------------
# Assembler flags
#   --64              64-bit assembly
# ---------------------------------------------------------------------------
ASFLAGS := --64

# ---------------------------------------------------------------------------
# Linker flags
#   -T linker.ld   Custom layout script (single PT_LOAD at 0x400000)
#   -nostdlib      No CRT objects (crt1.o, crti.o, crtn.o)
#   --static       Fully static binary; no dynamic interpreter
# ---------------------------------------------------------------------------
LDFLAGS := -T linker.ld -nostdlib --static

# Source files (assembly only, no C code)
ASM_SRCS := crt0.S
OBJS     := crt0.o
TARGET   := main.elf

.PHONY: all clean

all: $(TARGET)

# Assembly files
%.o: %.S
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) $< -o $@

# Link
$(TARGET): $(OBJS)
	@echo "[LD] Linking $(TARGET)..."
	$(LD) $(LDFLAGS) -o $@ crt0.o
	@echo "[OK] $(TARGET) ready"

clean:
	@echo "[CLEAN] user_space"
	@rm -f $(OBJS) $(TARGET)