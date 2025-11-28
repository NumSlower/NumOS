#ifndef ELF_H
#define ELF_H

#include "lib/base.h"

/* ELF Magic Number */
#define ELF_MAGIC 0x464C457F  /* "\x7FELF" in little-endian */

/* ELF Classes */
#define ELFCLASS32 1
#define ELFCLASS64 2

/* ELF Data Encoding */
#define ELFDATA2LSB 1  /* Little-endian */
#define ELFDATA2MSB 2  /* Big-endian */

/* ELF Types */
#define ET_NONE   0  /* No file type */
#define ET_REL    1  /* Relocatable file */
#define ET_EXEC   2  /* Executable file */
#define ET_DYN    3  /* Shared object file */
#define ET_CORE   4  /* Core file */

/* ELF Machine Types */
#define EM_NONE   0
#define EM_X86_64 62  /* AMD x86-64 */

/* Program Header Types */
#define PT_NULL    0
#define PT_LOAD    1
#define PT_DYNAMIC 2
#define PT_INTERP  3
#define PT_NOTE    4
#define PT_SHLIB   5
#define PT_PHDR    6
#define PT_TLS     7

/* Program Header Flags */
#define PF_X 0x1  /* Execute */
#define PF_W 0x2  /* Write */
#define PF_R 0x4  /* Read */

/* Section Header Types */
#define SHT_NULL     0
#define SHT_PROGBITS 1
#define SHT_SYMTAB   2
#define SHT_STRTAB   3
#define SHT_RELA     4
#define SHT_HASH     5
#define SHT_DYNAMIC  6
#define SHT_NOTE     7
#define SHT_NOBITS   8
#define SHT_REL      9
#define SHT_SHLIB    10
#define SHT_DYNSYM   11

/* ELF64 Header */
struct elf64_ehdr {
    uint8_t  e_ident[16];      /* Magic number and other info */
    uint16_t e_type;           /* Object file type */
    uint16_t e_machine;        /* Architecture */
    uint32_t e_version;        /* Object file version */
    uint64_t e_entry;          /* Entry point virtual address */
    uint64_t e_phoff;          /* Program header table file offset */
    uint64_t e_shoff;          /* Section header table file offset */
    uint32_t e_flags;          /* Processor-specific flags */
    uint16_t e_ehsize;         /* ELF header size in bytes */
    uint16_t e_phentsize;      /* Program header table entry size */
    uint16_t e_phnum;          /* Program header table entry count */
    uint16_t e_shentsize;      /* Section header table entry size */
    uint16_t e_shnum;          /* Section header table entry count */
    uint16_t e_shstrndx;       /* Section header string table index */
} __attribute__((packed));

/* ELF64 Program Header */
struct elf64_phdr {
    uint32_t p_type;           /* Segment type */
    uint32_t p_flags;          /* Segment flags */
    uint64_t p_offset;         /* Segment file offset */
    uint64_t p_vaddr;          /* Segment virtual address */
    uint64_t p_paddr;          /* Segment physical address */
    uint64_t p_filesz;         /* Segment size in file */
    uint64_t p_memsz;          /* Segment size in memory */
    uint64_t p_align;          /* Segment alignment */
} __attribute__((packed));

/* ELF64 Section Header */
struct elf64_shdr {
    uint32_t sh_name;          /* Section name (string tbl index) */
    uint32_t sh_type;          /* Section type */
    uint64_t sh_flags;         /* Section flags */
    uint64_t sh_addr;          /* Section virtual addr at execution */
    uint64_t sh_offset;        /* Section file offset */
    uint64_t sh_size;          /* Section size in bytes */
    uint32_t sh_link;          /* Link to another section */
    uint32_t sh_info;          /* Additional section information */
    uint64_t sh_addralign;     /* Section alignment */
    uint64_t sh_entsize;       /* Entry size if section holds table */
} __attribute__((packed));

/* ELF Loader Functions */
int elf_validate(const void *elf_data);
int elf_load(const void *elf_data, uint64_t *entry_point);
uint64_t elf_get_entry(const void *elf_data);
void elf_print_info(const void *elf_data);
uint64_t elf_setup_user_stack(void);

/* Error codes */
#define ELF_SUCCESS           0
#define ELF_ERROR_INVALID    -1
#define ELF_ERROR_NO_MEMORY  -2
#define ELF_ERROR_NOT_64BIT  -3
#define ELF_ERROR_NOT_EXEC   -4
#define ELF_ERROR_BAD_ARCH   -5

#endif /* ELF_H */