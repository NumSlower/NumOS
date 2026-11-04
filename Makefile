# NumOS 64-bit Operating System Makefile
CC = x86_64-elf-gcc
LD = x86_64-elf-ld
ASM = nasm
QEMU = qemu-system-x86_64

CFLAGS = -m64 -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -Werror -c -ffreestanding -mno-red-zone -mno-mmx -mno-sse -mno-sse2
LDFLAGS = -T linker.ld -nostdlib
ASMFLAGS = -f elf64

SRCDIR := src
OBJDIR := obj

C_SOURCES := $(shell find $(SRCDIR) -type f -name '*.c' | sort)
ASM_SOURCES := $(shell find $(SRCDIR) -type f -name '*.asm' | sort)

C_OBJS := $(patsubst $(SRCDIR)/%.c,$(OBJDIR)/%.o,$(C_SOURCES))
ASM_OBJS := $(patsubst $(SRCDIR)/%.asm,$(OBJDIR)/%.o,$(ASM_SOURCES))
ALL_OBJS := $(C_OBJS) $(ASM_OBJS)

all: numos.iso

numos.iso: numos.bin
	mkdir -p iso/boot/grub
	cp numos.bin iso/boot/
	cp $(SRCDIR)/boot/grub/grub.cfg iso/boot/grub/
	grub-mkrescue -o numos.iso iso

numos.bin: $(ALL_OBJS)
	$(LD) $(LDFLAGS) -o $@ $^

$(OBJDIR)/%.o: $(SRCDIR)/%.c
	mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I include -o $@ $<

$(OBJDIR)/%.o: $(SRCDIR)/%.asm
	mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) -o $@ $<

run: numos.iso
	$(QEMU) -cdrom numos.iso -m 512M -serial stdio

run-headless: numos.iso
	$(QEMU) -cdrom numos.iso -m 512M -nographic

clean:
	rm -rf $(OBJDIR) numos.bin numos.iso iso

.PHONY: all run clean