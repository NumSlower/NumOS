################################################################################
#  USER SPACE BUILD — NumOS
#
#  Produces main.elf: a static, position-dependent ELF64 with no libc.
#  The kernel's ELF loader reads this at runtime from /init/SHELL on FAT32.
#
#  Usage:
#    make          — build main.elf
#    make clean    — remove build artefacts
################################################################################

CC  := x86_64-elf-gcc
AS  := x86_64-elf-as
LD  := x86_64-elf-ld

# ---------------------------------------------------------------------------
# Compiler flags
#   -m64                  64-bit code generation (explicit; matches target)
#   -ffreestanding        No hosted-environment assumptions
#   -nostdlib             No standard library or CRT startup files
#   -fno-stack-protector  No __stack_chk_fail (we have no libc)
#   -mno-red-zone         The kernel does not preserve the 128-byte region
#                         below RSP across the syscall boundary; using the
#                         red zone would corrupt user data
#   -O2                   Optimize — the code is simple and safe at this level
#   -Wall -Wextra         Maximum warnings
#   -c                    Compile only; linking is done separately
# ---------------------------------------------------------------------------
CFLAGS := -m64 -ffreestanding -nostdlib -fno-stack-protector \
          -mno-red-zone -O2 -Wall -Wextra -c

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

# Source files
ASM_SRCS := crt0.S
C_SRCS   := main.c
OBJS     := crt0.o main.o
TARGET   := main.elf

.PHONY: all clean

all: $(TARGET)

# Assembly files
%.o: %.S
	@echo "[AS] $<"
	$(AS) $(ASFLAGS) $< -o $@

# C files
%.o: %.c
	@echo "[CC] $<"
	$(CC) $(CFLAGS) $< -o $@

# Link (crt0.o must come first!)
$(TARGET): $(OBJS)
	@echo "[LD] Linking $(TARGET)..."
	$(LD) $(LDFLAGS) -o $@ crt0.o main.o
	@echo "[OK] $(TARGET) ready"

clean:
	@echo "[CLEAN] user_space"
	@rm -f $(OBJS) $(TARGET)