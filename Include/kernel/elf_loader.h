/*
 * elf_loader.h — NumOS minimal ELF64 loader (kernel side)
 *
 * Loads a static ELF64 executable from the FAT32 filesystem, maps
 * its PT_LOAD segments into low physical memory (identity-mapped),
 * installs ring-3 GDT descriptors, sets up a user stack, and
 * transfers control via iretq.
 *
 * Scope limitations (by design, for this kernel stage):
 *   - Static executables only.  No dynamic linker, no PLT/GOT.
 *   - Little-endian x86_64 only (matches the host; no byte-swap).
 *   - One process at a time.  exec_user_elf does not return on success.
 *   - No TLS, no shared objects, no relocation.
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "lib/base.h"

/* ─── ELF constants ──────────────────────────────────────────────
 * Only the values we actually check or use are defined here.
 * ─────────────────────────────────────────────────────────────── */

/* ELF Magic: bytes 0x7F 'E' 'L' 'F' (0x7F 0x45 0x4C 0x46)
 * When interpreted as a little-endian uint32_t, this becomes 0x464C457F
 * (least significant byte first: 0x7F, then 0x45, 0x4C, 0x46) */
#define ELF_MAGIC       0x464C457F

/* e_ident byte indices */
#define EI_CLASS        4
#define EI_DATA         5

/* e_ident class */
#define ELFCLASS64      2

/* e_ident data encoding */
#define ELFDATA2LSB     1            /* Little-endian */

/* e_machine */
#define EM_X86_64       62

/* Program-header type */
#define PT_LOAD         1

/* ─── ELF64 file header (64 bytes) ───────────────────────────────
 * Every field is present so that sizeof(struct elf64_ehdr) == 64
 * and pointer arithmetic over the program-header table is correct.
 * ─────────────────────────────────────────────────────────────── */
struct elf64_ehdr {
    uint8_t  e_ident[16];       /* Magic, class, data, version, ...  */
    uint16_t e_type;            /* ET_EXEC = 2                       */
    uint16_t e_machine;         /* EM_X86_64 = 62                    */
    uint32_t e_version;         /* 1                                 */
    uint64_t e_entry;           /* Entry point virtual address       */
    uint64_t e_phoff;           /* Program-header table byte offset  */
    uint64_t e_shoff;           /* Section-header offset (unused)    */
    uint32_t e_flags;           /* Processor flags                   */
    uint16_t e_ehsize;          /* ELF header size in bytes (64)     */
    uint16_t e_phentsize;       /* Bytes per program-header entry    */
    uint16_t e_phnum;           /* Number of program-header entries  */
    uint16_t e_shentsize;       /* Bytes per section-header entry    */
    uint16_t e_shnum;           /* Number of section-header entries  */
    uint16_t e_shstrndx;        /* Section-header string-table index */
} __attribute__((packed));

/* ─── ELF64 program header (56 bytes) ────────────────────────────
 * sizeof must be 56 for e_phoff + i * e_phentsize to work.
 * ─────────────────────────────────────────────────────────────── */
struct elf64_phdr {
    uint32_t p_type;            /* PT_LOAD, PT_NOTE, ...             */
    uint32_t p_flags;           /* PF_R | PF_W | PF_X                */
    uint64_t p_offset;          /* Byte offset in the ELF file       */
    uint64_t p_vaddr;           /* Virtual address to load at        */
    uint64_t p_paddr;           /* Physical address (= p_vaddr here) */
    uint64_t p_filesz;          /* Bytes to copy from the file       */
    uint64_t p_memsz;           /* Bytes in memory (≥ p_filesz)      */
    uint64_t p_align;           /* Alignment requirement             */
} __attribute__((packed));

/* ─── Public API ─────────────────────────────────────────────────
 * exec_user_elf(path)
 *   path  — FAT32 path relative to the current directory on the
 *           mounted filesystem.  The caller must have already done
 *           fat32_chdir("init") if the file is in /init.
 *
 *   On success: does NOT return.  Transfers control to user space.
 *   On failure: prints a diagnostic to VGA and returns -1.
 * ─────────────────────────────────────────────────────────────── */
int exec_user_elf(const char *path);

#endif /* ELF_LOADER_H */