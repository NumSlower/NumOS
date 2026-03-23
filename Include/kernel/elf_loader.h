#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include "lib/base.h"

/* =========================================================================
 * ELF-64 loader for NumOS
 *
 * Parses a statically-linked ELF64 executable from the init program path
 * and maps its PT_LOAD segments into user virtual memory.
 *
 * After a successful load the caller gets back an elf_load_result
 * containing the entry point and the top of a freshly-allocated user
 * stack ready for execution.
 * ========================================================================= */

/* ---- ELF magic ----------------------------------------------------------- */
#define ELF_MAGIC0      0x7F
#define ELF_MAGIC1      'E'
#define ELF_MAGIC2      'L'
#define ELF_MAGIC3      'F'

/* ---- e_ident indices ----------------------------------------------------- */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4   /* 1 = 32-bit, 2 = 64-bit                        */
#define EI_DATA         5   /* 1 = little-endian, 2 = big-endian             */
#define EI_VERSION      6   /* Must be EV_CURRENT (1)                        */
#define EI_OSABI        7
#define EI_ABIVERSION   8
#define EI_NIDENT       16

/* ---- ELF class / data ---------------------------------------------------- */
#define ELFCLASS64      2
#define ELFDATA2LSB     1   /* Little-endian                                  */

/* ---- Object file types (e_type) ------------------------------------------ */
#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2   /* Executable file                                */
#define ET_DYN          3
#define ET_CORE         4

/* ---- Machine type (e_machine) -------------------------------------------- */
#define EM_X86_64       62

/* ---- ELF version --------------------------------------------------------- */
#define EV_CURRENT      1

/* ---- Program header types (p_type) --------------------------------------- */
#define PT_NULL         0
#define PT_LOAD         1   /* Loadable segment                               */
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

/* ---- Program header flags (p_flags) -------------------------------------- */
#define PF_X            0x1   /* Execute                                      */
#define PF_W            0x2   /* Write                                        */
#define PF_R            0x4   /* Read                                         */

/* =========================================================================
 * ELF64 on-disk structures  (all fields little-endian)
 * ======================================================================= */

/* ELF64 file header (64 bytes) */
struct elf64_hdr {
    uint8_t  e_ident[EI_NIDENT]; /* Magic + class + data + version + OS/ABI  */
    uint16_t e_type;             /* Object file type                          */
    uint16_t e_machine;          /* Architecture                              */
    uint32_t e_version;          /* ELF version                               */
    uint64_t e_entry;            /* Entry point virtual address               */
    uint64_t e_phoff;            /* Program header table file offset          */
    uint64_t e_shoff;            /* Section header table file offset          */
    uint32_t e_flags;            /* Processor-specific flags                  */
    uint16_t e_ehsize;           /* ELF header size in bytes                  */
    uint16_t e_phentsize;        /* Program header table entry size           */
    uint16_t e_phnum;            /* Program header table entry count          */
    uint16_t e_shentsize;        /* Section header table entry size           */
    uint16_t e_shnum;            /* Section header table entry count          */
    uint16_t e_shstrndx;         /* Section name string table index           */
} __attribute__((packed));

/* ELF64 program header (56 bytes) */
struct elf64_phdr {
    uint32_t p_type;             /* Segment type                              */
    uint32_t p_flags;            /* Segment flags                             */
    uint64_t p_offset;           /* Segment file offset                       */
    uint64_t p_vaddr;            /* Segment virtual address                   */
    uint64_t p_paddr;            /* Segment physical address (ignored)        */
    uint64_t p_filesz;           /* Bytes in file image of segment            */
    uint64_t p_memsz;            /* Bytes in memory image of segment          */
    uint64_t p_align;            /* Segment alignment                         */
} __attribute__((packed));

/* ---- Result returned by elf_load() --------------------------------------- */
struct elf_load_result {
    int      success;       /* Non-zero on success                            */
    uint64_t entry;         /* Virtual entry-point address (_start)           */
    uint64_t load_base;     /* Lowest virtual address mapped                  */
    uint64_t load_end;      /* Highest virtual address mapped (page-aligned)  */
    uint64_t stack_top;     /* Top of freshly-allocated user stack            */
    uint64_t stack_bottom;  /* Bottom of user stack (for cleanup/unmap)       */
    char     error[64];     /* Human-readable error string on failure         */
};

/* ---- Error codes --------------------------------------------------------- */
#define ELF_OK              0
#define ELF_ERR_IO         -1   /* File read error                            */
#define ELF_ERR_MAGIC      -2   /* Not a valid ELF file                       */
#define ELF_ERR_CLASS      -3   /* Not ELF64                                  */
#define ELF_ERR_MACHINE    -4   /* Not x86-64                                 */
#define ELF_ERR_TYPE       -5   /* Not an ET_EXEC binary                      */
#define ELF_ERR_NOPHDR     -6   /* No program headers                         */
#define ELF_ERR_NOMEM      -7   /* Out of physical memory                     */
#define ELF_ERR_MAP        -8   /* Page mapping failed                        */
#define ELF_ERR_STACK      -9   /* Stack allocation failed                    */

/* =========================================================================
 * Public API
 * ======================================================================= */

/*
 * elf_load_from_file()
 *
 *   Opens the file at `path` on the FAT32 volume, validates the ELF header,
 *   maps every PT_LOAD segment into user virtual memory with the correct
 *   page permissions, allocates and maps a user stack, and fills in `result`.
 *
 *   Returns ELF_OK (0) on success, a negative ELF_ERR_* code on failure.
 *   On failure result->error contains a descriptive message.
 */
int elf_load_from_file(const char *path, struct elf_load_result *result);

/*
 * elf_load_from_memory()
 *
 *   Same as elf_load_from_file() but operates on a buffer already in memory.
 *   Useful for testing / loading from a RAM disk.
 */
int elf_load_from_memory(const void *elf_data, size_t elf_size,
                         struct elf_load_result *result);

/*
 * elf_validate()
 *
 *   Quick sanity check on the ELF header — does not map anything.
 *   Returns ELF_OK or a negative ELF_ERR_* code.
 */
int elf_validate(const struct elf64_hdr *hdr);

/*
 * elf_unload()
 *
 *   Unmaps all virtual pages in [vaddr_start, vaddr_end) and frees their
 *   physical frames.  Call with:
 *     - load_base / load_end from elf_load_result  (ELF segments)
 *     - stack_bottom / (stack_top rounded up) from elf_load_result (stack)
 *
 *   After this call the virtual address range is reusable.
 */
void elf_unload(uint64_t load_base, uint64_t load_end,
                uint64_t stack_bottom, uint64_t stack_top_page);

#endif /* ELF_LOADER_H */
