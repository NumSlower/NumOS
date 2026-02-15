/*
 * elf_loader.c — NumOS minimal ELF64 loader
 *
 * Fixes applied:
 *  1. iretq inline asm: "i" → "r" constraints for USER_CS/USER_DS
 *  2. install_user_gdt_entries() removed (gdt.c already correct)
 *  3. Removed per-cluster debug VGA spam (fat32.c)
 *  4. cli before iretq frame to prevent timer IRQ corruption
 *  5. PAGE FAULT FIX: boot page tables map 0–1 GB as supervisor-only.
 *     Ring 3 faults on first instruction/stack access.
 *     Fix: use paging_set_user_range() which correctly sets U/S=1 on
 *     ALL levels: PML4E, PDPTE, PDE, and PTE.
 *     The previous mark_user_range() only set the leaf PDE — missing
 *     the upper levels which the MMU also checks.
 */

#include "kernel/elf_loader.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "fs/fat32.h"
#include "cpu/heap.h"
#include "cpu/paging.h"

/*
 * GDT selector constants — must match gdt.c exactly.
 *
 *   GDT[0] = null
 *   GDT[1] = kernel code  (0x08)
 *   GDT[2] = kernel data  (0x10)
 *   GDT[3] = user data    (0x18 | RPL=3 = 0x1B)   ← Data before Code (sysret needs this)
 *   GDT[4] = user code    (0x20 | RPL=3 = 0x23)
 */
#define USER_DS  0x1B   /* (3 << 3) | 3 */
#define USER_CS  0x23   /* (4 << 3) | 3 */

/* User-space memory layout */
#define USER_STACK_TOP    0x800000ULL           /* 8 MB */
#define USER_STACK_PAGES  4
#define USER_STACK_BOTTOM (USER_STACK_TOP - (USER_STACK_PAGES * 4096ULL))

/* ── helpers ──────────────────────────────────────────────────── */

static int is_valid_user_address(uint64_t addr)
{
    /* Must be in user space: above null-guard page, below 128 MB */
    return (addr >= 0x1000) && (addr < 128ULL * 1024 * 1024);
}

static int is_valid_user_range(uint64_t start, uint64_t size)
{
    if (start < 0x1000)  return 0;
    if (start + size > 128ULL * 1024 * 1024) return 0;
    return 1;
}

/* ── relocation processing (for ET_DYN / PIE) ────────────────── */

static int process_relocations(uint8_t *buf,
                                struct elf64_ehdr *ehdr,
                                uint64_t load_base)
{
    if (ehdr->e_shoff == 0 || ehdr->e_shnum == 0) return 0;

    for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
        struct elf64_shdr *shdr = (struct elf64_shdr *)
            (buf + ehdr->e_shoff + (uint64_t)i * ehdr->e_shentsize);

        if (shdr->sh_type != SHT_RELA) continue;

        uint64_t nrel = shdr->sh_size / shdr->sh_entsize;
        for (uint64_t j = 0; j < nrel; j++) {
            struct elf64_rela *rela = (struct elf64_rela *)
                (buf + shdr->sh_offset + j * shdr->sh_entsize);

            uint64_t reloc_type = rela->r_info & 0xFFFFFFFF;
            uint64_t reloc_addr = rela->r_offset + load_base;

            if (!is_valid_user_address(reloc_addr)) continue;

            switch (reloc_type) {
            case R_X86_64_RELATIVE: {
                uint64_t *p = (uint64_t *)(uintptr_t)reloc_addr;
                *p += load_base;
                break;
            }
            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
                /* symbol-based: skip (no dynamic linker) */
                break;
            default:
                break;
            }
        }
    }
    return 0;
}

/* ── public entry point ───────────────────────────────────────── */

int exec_user_elf(const char *path)
{
    vga_writestring("\n[ELF] Loading: ");
    vga_writestring(path);
    vga_writestring("\n");

    /* ── stat the file ──────────────────────────────────────────── */
    struct fat32_dirent info;
    if (fat32_stat(path, &info) != 0) {
        vga_writestring("[ELF] ERROR: file not found\n");
        return -1;
    }

    uint32_t file_size = info.size;
    vga_writestring("[ELF] Size: ");
    print_dec(file_size);
    vga_writestring(" bytes\n");

    if (file_size < sizeof(struct elf64_ehdr)) {
        vga_writestring("[ELF] ERROR: file too small to be an ELF\n");
        return -1;
    }

    /* ── read the whole file into kernel heap ───────────────────── */
    uint8_t *buf = (uint8_t *)kmalloc(file_size);
    if (!buf) {
        vga_writestring("[ELF] ERROR: kmalloc failed\n");
        return -1;
    }

    int fd = fat32_open(path, FAT32_O_RDONLY);
    if (fd < 0) {
        vga_writestring("[ELF] ERROR: open failed\n");
        kfree(buf);
        return -1;
    }

    ssize_t got = fat32_read(fd, buf, file_size);
    fat32_close(fd);

    if (got < 0 || (uint32_t)got != file_size) {
        vga_writestring("[ELF] ERROR: read failed (got ");
        print_dec((uint64_t)(got < 0 ? 0 : (uint64_t)got));
        vga_writestring(" of ");
        print_dec(file_size);
        vga_writestring(" bytes)\n");
        kfree(buf);
        return -1;
    }

    /* ── validate ELF header ────────────────────────────────────── */
    struct elf64_ehdr *ehdr = (struct elf64_ehdr *)buf;

    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC ||
        ehdr->e_ident[EI_CLASS]    != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA]     != ELFDATA2LSB ||
        ehdr->e_machine            != EM_X86_64) {
        vga_writestring("[ELF] ERROR: not a valid x86-64 LE ELF64\n");
        kfree(buf);
        return -1;
    }

    /* ── determine load base ────────────────────────────────────── */
    uint64_t load_base = 0;
    if (ehdr->e_type == ET_DYN) {
        vga_writestring("[ELF] Type: ET_DYN (position-independent)\n");
        load_base = 0x400000ULL;
    } else if (ehdr->e_type == ET_EXEC) {
        vga_writestring("[ELF] Type: ET_EXEC (static)\n");
        load_base = 0;
    } else {
        vga_writestring("[ELF] ERROR: unsupported e_type=");
        print_dec(ehdr->e_type);
        vga_writestring("\n");
        kfree(buf);
        return -1;
    }

    uint64_t entry = ehdr->e_entry + load_base;
    vga_writestring("[ELF] Entry: 0x");
    print_hex(entry);
    vga_writestring("\n");

    if (!is_valid_user_address(entry)) {
        vga_writestring("[ELF] ERROR: entry point outside valid user range\n");
        kfree(buf);
        return -1;
    }

    /* ── load PT_LOAD segments ──────────────────────────────────── */
    int loaded = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)
            (buf + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        uint64_t vaddr = ph->p_vaddr + load_base;
        uint64_t memsz = ph->p_memsz;

        vga_writestring("[ELF] PT_LOAD vaddr=0x");
        print_hex(vaddr);
        vga_writestring(" filesz=");
        print_dec(ph->p_filesz);
        vga_writestring(" memsz=");
        print_dec(memsz);
        vga_writestring("\n");

        if (!is_valid_user_range(vaddr, memsz)) {
            vga_writestring("[ELF] ERROR: segment outside valid user range\n");
            kfree(buf);
            return -1;
        }

        uint8_t *dst = (uint8_t *)(uintptr_t)vaddr;
        memcpy(dst, buf + ph->p_offset, ph->p_filesz);
        if (ph->p_memsz > ph->p_filesz) {
            memset(dst + ph->p_filesz, 0, ph->p_memsz - ph->p_filesz);
        }

        /* FIX: set U/S on ALL page table levels covering this segment.
         * paging_set_user_range() updates PML4E + PDPTE + PDE/PTE.
         * The previous mark_user_range() only touched the leaf PDE. */
        paging_set_user_range(vaddr, memsz);

        loaded++;
    }

    if (loaded == 0) {
        vga_writestring("[ELF] ERROR: no PT_LOAD segments\n");
        kfree(buf);
        return -1;
    }

    vga_writestring("[ELF] Loaded ");
    print_dec(loaded);
    vga_writestring(" segment(s)\n");

    /* ── process relocations (needed for ET_DYN) ────────────────── */
    process_relocations(buf, ehdr, load_base);

    kfree(buf);

    /* ── set up user stack ──────────────────────────────────────── */
    if (!is_valid_user_range(USER_STACK_BOTTOM, USER_STACK_PAGES * 4096ULL)) {
        vga_writestring("[ELF] ERROR: user stack range invalid\n");
        return -1;
    }

    memset((void *)(uintptr_t)USER_STACK_BOTTOM, 0,
           USER_STACK_TOP - USER_STACK_BOTTOM);

    /* FIX: mark stack pages user-accessible on ALL page table levels */
    paging_set_user_range(USER_STACK_BOTTOM, USER_STACK_TOP - USER_STACK_BOTTOM);

    vga_writestring("[ELF] Stack: 0x");
    print_hex(USER_STACK_BOTTOM);
    vga_writestring(" - 0x");
    print_hex(USER_STACK_TOP);
    vga_writestring("\n");

    vga_writestring("[ELF] CS=0x");
    print_hex(USER_CS);
    vga_writestring(" SS=0x");
    print_hex(USER_DS);
    vga_writestring("\n");

    vga_writestring("[ELF] Jumping to Ring 3...\n");

    /*
     * Transfer control to user space via IRETQ.
     *
     * Stack frame for IRETQ (64-bit, privilege change):
     *   [RSP+32]  SS      (user stack segment)
     *   [RSP+24]  RSP     (user stack pointer)
     *   [RSP+16]  RFLAGS  (user flags: IF=1, reserved=1)
     *   [RSP+ 8]  CS      (user code segment)
     *   [RSP+ 0]  RIP     (user entry point)   <-- iretq pops this first
     *
     * FIX: Use "r" (register) constraints for USER_CS and USER_DS instead
     * of "i" (immediate).  Although pushq $small_imm is valid in x86-64,
     * GCC may emit incorrect code when an "i" constraint is used inside an
     * asm block that also uses memory operands, especially under -O2.
     * Routing through a register is always safe.
     *
     * FIX: Disable interrupts (cli) before building the iretq frame.
     * A timer IRQ between the first push and the iretq would corrupt the
     * kernel stack and triple-fault on return.
     */
    uint64_t rip    = entry;
    uint64_t rsp    = USER_STACK_TOP;
    uint64_t rflags = 0x202;    /* IF=1, reserved bit 1 = 1 */
    uint64_t cs     = USER_CS;
    uint64_t ss     = USER_DS;

    __asm__ volatile (
        "cli\n\t"
        "pushq %[ss]\n\t"
        "pushq %[rsp]\n\t"
        "pushq %[rflags]\n\t"
        "pushq %[cs]\n\t"
        "pushq %[rip]\n\t"
        "iretq\n\t"
        :
        : [rip]    "r" (rip),
          [rsp]    "r" (rsp),
          [rflags] "r" (rflags),
          [cs]     "r" (cs),
          [ss]     "r" (ss)
        : "memory"
    );

    /* unreachable */
    return 0;
}