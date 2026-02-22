################################################################################
#  NumOS User-Space Makefile
#
#  Builds all user-space programs into ELF64 binaries.
#  Intentionally SEPARATE from the kernel Makefile — user code is never
#  compiled with kernel flags or linked against kernel objects.
#
#  Usage (from the user/ directory):
#    make              - build all programs
#    make elftest.elf  - build one program
#    make clean        - remove build artefacts
#    make install      - copy ELFs to ../build/user/ (called by root Makefile)
################################################################################

# ==============================
#  TOOLCHAIN
# ==============================
AS := nasm
LD := ld

# ==============================
#  FLAGS
# ==============================
ASFLAGS := -f elf64

# --entry=_start   : explicit entry symbol (belt-and-suspenders alongside ENTRY()
#                    in linker.ld; guards against ld ignoring the script on some
#                    distros or when the script is not found)
# --build-id=none  : reproducible, smaller output; kernel ELF loader doesn't
#                    need a .note.gnu.build-id section
# -z noexecstack   : mark stack non-executable (good hygiene; no RWX warning)
LDFLAGS := -m elf_x86_64      \
           -nostdlib           \
           -static             \
           -T linker.ld        \
           --entry=_start      \
           --build-id=none     \
           -z noexecstack

# ==============================
#  DIRECTORIES
# ==============================
BUILD_DIR := build

# ==============================
#  PROGRAMS
#  Add the stem of each new program here (no extension).
#  Pattern rules below handle .asm -> .o -> .elf automatically.
# ==============================
PROGRAMS := elftest

ELFS    := $(addsuffix .elf, $(PROGRAMS))
OBJECTS := $(addprefix $(BUILD_DIR)/, $(addsuffix .o, $(PROGRAMS)))

# ==============================
#  KEEP OBJECT FILES
#  Without this, Make treats $(BUILD_DIR)/%.o as an intermediate file
#  and silently deletes it after linking ("rm build/elftest.o").
#  .PRECIOUS stops that deletion and also prevents re-assembly every
#  run when the .asm hasn't changed.
# ==============================
.PRECIOUS: $(BUILD_DIR)/%.o

# ==============================
#  DEFAULT TARGET
# ==============================
.PHONY: all
all: $(ELFS)
	@echo "[USER] All user-space programs built."

# ==============================
#  PATTERN RULES  (.asm -> .o -> .elf)
# ==============================
$(BUILD_DIR)/%.o: %.asm | $(BUILD_DIR)
	@echo "[AS user] $<"
	$(AS) $(ASFLAGS) $< -o $@

%.elf: $(BUILD_DIR)/%.o linker.ld
	@echo "[LD user] $@"
	$(LD) $(LDFLAGS) -o $@ $<
	@echo "[INFO] $@: $$(wc -c < $@) bytes, entry=$$(readelf -h $@ 2>/dev/null | awk '/Entry point/{print $$4}')"
	@readelf -l $@ 2>/dev/null | grep -E "LOAD|GNU_STACK" || true

# ==============================
#  BUILD DIRECTORY
# ==============================
$(BUILD_DIR):
	@mkdir -p $@

# ==============================
#  INSTALL  (called by root Makefile's user_space target)
# ==============================
INSTALL_DIR := ../build/user

.PHONY: install
install: all
	@mkdir -p $(INSTALL_DIR)
	@for elf in $(ELFS); do \
		cp $$elf $(INSTALL_DIR)/$$elf; \
		echo "[INSTALL] $$elf -> $(INSTALL_DIR)/$$elf"; \
	done

# ==============================
#  CLEAN
# ==============================
.PHONY: clean
clean:
	@echo "[USER CLEAN] Removing user build artefacts..."
	@rm -rf $(BUILD_DIR)
	@rm -f $(ELFS)
	@echo "[USER CLEAN] Done."

# ==============================
#  HELP
# ==============================
.PHONY: help
help:
	@echo "NumOS User-Space Build"
	@echo "======================"
	@echo "Targets:"
	@echo "  all       - build: $(PROGRAMS)"
	@echo "  install   - build + copy to ../build/user/"
	@echo "  clean     - remove build/ and *.elf"
	@echo ""
	@echo "To add a new user program:"
	@echo "  1. Create user/<name>.asm"
	@echo "  2. Add <name> to PROGRAMS in this Makefile"
	@echo "  3. Run: make"
	@echo ""
	@echo "Linker script : linker.ld  (base 0x400000, separate RX/R/RW segments)"
	@echo "Toolchain     : $(AS) / $(LD)"