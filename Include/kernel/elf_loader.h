/*
 * elf_loader.h — NumOS minimal ELF64 loader (kernel side)
 *
 * Loads a static or position-independent ELF64 executable from the FAT32
 * filesystem, copies its PT_LOAD segments into low physical memory
 * (identity-mapped by the boot page tables), and transfers control via iretq.
 *
 * Scope limitations (by design, for this kernel stage):
 *   - Little-endian x86_64 only.
 *   - One process at a time; exec_user_elf does not return on success.
 *   - No TLS, no shared objects.
 *   - Relocation support: R_X86_64_RELATIVE only (for ET_DYN / PIE).
 *
 * GDT layout assumed (must match gdt.c):
 *   GDT[0] = null
 *   GDT[1] = kernel code  (0x08)
 *   GDT[2] = kernel data  (0x10)
 *   GDT[3] = user data    (0x1B with RPL=3)  ← Data BEFORE Code (sysret requirement)
 *   GDT[4] = user code    (0x23 with RPL=3)
 *
 * iretq fixes:
 *   - All pushed values use "r" (register) constraints, not "i" (immediate).
 *   - Interrupts are disabled (cli) before building the iretq frame to prevent
 *     a timer IRQ from corrupting the partially-built frame.
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "lib/base.h"

/* ─── ELF magic ─────────────────────────────────────────────────
 * 0x7F 'E' 'L' 'F' as little-endian uint32_t = 0x464C457F         */
#define ELF_MAGIC       0x464C457F

/* e_ident byte indices */
#define EI_CLASS        4
#define EI_DATA         5

/* e_ident class */
#define ELFCLASS64      2

/* e_ident data encoding */
#define ELFDATA2LSB     1

/* e_machine */
#define EM_X86_64       62

/* e_type */
#define ET_EXEC         2
#define ET_DYN          3

/* Section-header types */
#define SHT_RELA        4
#define SHT_SYMTAB      2
#define SHT_STRTAB      3

/* Relocation types for x86-64 */
#define R_X86_64_RELATIVE   8
#define R_X86_64_GLOB_DAT   6
#define R_X86_64_JUMP_SLOT  7

/* ─── ELF64 file header (64 bytes) ─────────────────────────────── */
struct elf64_ehdr {
    uint8_t  e_ident[16];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed));

/* ─── ELF64 program header (56 bytes) ───────────────────────────── */
struct elf64_phdr {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed));

/* p_type values */
#define PT_LOAD         1
#define PT_DYNAMIC      2

/* ─── ELF64 section header (64 bytes) ───────────────────────────── */
struct elf64_shdr {
    uint32_t sh_name;
    uint32_t sh_type;
    uint64_t sh_flags;
    uint64_t sh_addr;
    uint64_t sh_offset;
    uint64_t sh_size;
    uint32_t sh_link;
    uint32_t sh_info;
    uint64_t sh_addralign;
    uint64_t sh_entsize;
} __attribute__((packed));

/* ─── ELF64 relocation entry ────────────────────────────────────── */
struct elf64_rela {
    uint64_t r_offset;
    uint64_t r_info;
    int64_t  r_addend;
} __attribute__((packed));

/* ─── Public API ────────────────────────────────────────────────── */

/**
 * exec_user_elf(path)
 *
 *   path  — FAT32 path relative to the current directory on the mounted
 *            filesystem.  Caller must have already called fat32_chdir("init")
 *            if the file is in /init.
 *
 *   On success: does NOT return.  Transfers control to user space via iretq.
 *   On failure: prints a diagnostic to VGA and returns -1.
 */
int exec_user_elf(const char *path);

#endif /* ELF_LOADER_H */