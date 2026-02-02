/*
 * elf_loader.c — NumOS minimal ELF64 loader with basic relocation support
 * CRITICAL: GDT indices 3 and 4 are swapped for syscall/sysret compatibility!
 */

#include "kernel/elf_loader.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "fs/fat32.h"
#include "cpu/heap.h"

/* CRITICAL GDT LAYOUT FOR SYSCALL/SYSRET:
 * The sysret instruction expects:
 *   User SS = (STAR[63:48] + 8) | 3
 *   User CS = (STAR[63:48] + 16) | 3
 * This means User Data must come BEFORE User Code in the GDT!
 *
 * Our layout:
 *   GDT[0] = Null
 *   GDT[1] = Kernel Code (0x08)
 *   GDT[2] = Kernel Data (0x10)
 *   GDT[3] = User Data   (0x18/0x1B with RPL=3) ← Note: Data comes first!
 *   GDT[4] = User Code   (0x20/0x23 with RPL=3) ← Code comes second!
 */

#define USER_DATA_DESC  0x00CFF3000000FFFFULL  /* At index 3 */
#define USER_CODE_DESC  0x00AFFB000000FFFFULL  /* At index 4 */

#define GDT_USER_DATA   3
#define GDT_USER_CODE   4

#define USER_DS         ((GDT_USER_DATA << 3) | 3)   /* 0x1B */
#define USER_CS         ((GDT_USER_CODE << 3) | 3)   /* 0x23 */

#define USER_STACK_TOP     0x800000ULL
#define USER_STACK_PAGES   4
#define USER_STACK_BOTTOM  (USER_STACK_TOP - (USER_STACK_PAGES * 4096ULL))

/* Check if address is within valid user space bounds */
static int is_valid_user_address(uint64_t addr) {
    return addr < 128ULL * 1024 * 1024 && addr >= 0x1000;
}

/* Check if memory range doesn't overlap with kernel */
static int is_valid_user_range(uint64_t start, uint64_t size) {
    if (start < 0x1000) return 0;  /* Reserved for null pointer dereference */
    if (start + size > 128ULL * 1024 * 1024) return 0;
    /* Check against user stack area - reject if overlaps with stack region */
    if (start < USER_STACK_TOP && start + size > USER_STACK_BOTTOM) {
        /* Segments must not overlap with the stack */
        if (start >= USER_STACK_BOTTOM) return 0;
    }
    return 1;
}

static void install_user_gdt_entries(void)
{
    struct {
        uint16_t limit;
        uint64_t base;
    } __attribute__((packed)) gdtr;
    
    __asm__ volatile ("sgdt %0" : "=m"(gdtr));
    
    uint64_t *gdt = (uint64_t *)gdtr.base;
    
    /* Install in the correct order: Data at 3, Code at 4 */
    gdt[GDT_USER_DATA] = USER_DATA_DESC;
    gdt[GDT_USER_CODE] = USER_CODE_DESC;
    
    vga_writestring("[ELF] GDT entries installed:\n");
    vga_writestring("[ELF]   [3] User Data = ");
    print_hex(gdt[3]);
    vga_writestring("\n");
    vga_writestring("[ELF]   [4] User Code = ");
    print_hex(gdt[4]);
    vga_writestring("\n");
}

/* Process ELF relocations for dynamic linking support */
static int process_relocations(
    uint8_t *buf,
    struct elf64_ehdr *ehdr,
    uint64_t load_base
) {
    vga_writestring("[ELF] Processing relocations...\n");
    
    /* Iterate through section headers to find relocation sections */
    for (uint16_t i = 0; i < ehdr->e_shnum; i++) {
        struct elf64_shdr *shdr = (struct elf64_shdr *)
            (buf + ehdr->e_shoff + (uint64_t)i * ehdr->e_shentsize);
        
        if (shdr->sh_type != SHT_RELA) continue;
        
        vga_writestring("[ELF] Found relocation section: ");
        print_dec(i);
        vga_writestring(" with ");
        print_dec(shdr->sh_size / shdr->sh_entsize);
        vga_writestring(" entries\n");
        
        /* Process each relocation entry */
        uint64_t num_relocations = shdr->sh_size / shdr->sh_entsize;
        for (uint64_t j = 0; j < num_relocations; j++) {
            struct elf64_rela *rela = (struct elf64_rela *)
                (buf + shdr->sh_offset + j * shdr->sh_entsize);
            
            uint64_t reloc_type = rela->r_info & 0xFFFFFFFF;
            uint64_t reloc_addr = rela->r_offset + load_base;
            
            if (!is_valid_user_address(reloc_addr)) {
                vga_writestring("[ELF] WARNING: invalid relocation address 0x");
                print_hex(reloc_addr);
                vga_writestring("\n");
                continue;
            }
            
            switch (reloc_type) {
                case R_X86_64_RELATIVE: {
                    /* Add load_base to the value at reloc_addr */
                    uint64_t *p = (uint64_t *)reloc_addr;
                    *p += load_base;
                    break;
                }
                case R_X86_64_GLOB_DAT:
                case R_X86_64_JUMP_SLOT:
                    /* For now: skip symbol-based relocations */
                    vga_writestring("[ELF] Skipping symbol relocation type ");
                    print_dec(reloc_type);
                    vga_writestring("\n");
                    break;
                default:
                    vga_writestring("[ELF] WARNING: unsupported relocation type ");
                    print_dec(reloc_type);
                    vga_writestring("\n");
                    break;
            }
        }
    }
    
    return 0;
}

int exec_user_elf(const char *path)
{
    vga_writestring("\n[ELF] Loading: ");
    vga_writestring(path);
    vga_writestring("\n");

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
        vga_writestring("[ELF] ERROR: too small\n");
        return -1;
    }

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
        vga_writestring("[ELF] ERROR: read failed\n");
        kfree(buf);
        return -1;
    }

    struct elf64_ehdr *ehdr = (struct elf64_ehdr *)buf;

    if (*(uint32_t *)ehdr->e_ident != ELF_MAGIC ||
        ehdr->e_ident[EI_CLASS] != ELFCLASS64 ||
        ehdr->e_ident[EI_DATA] != ELFDATA2LSB ||
        ehdr->e_machine != EM_X86_64) {
        vga_writestring("[ELF] ERROR: invalid ELF\n");
        kfree(buf);
        return -1;
    }

    /* Support both ET_EXEC (static) and ET_DYN (dynamic/PIE) */
    uint64_t load_base = 0;
    if (ehdr->e_type == ET_DYN) {
        vga_writestring("[ELF] Type: Position-Independent Executable (ET_DYN)\n");
        /* For ET_DYN, use a fixed load base in user space */
        load_base = 0x400000ULL;
    } else if (ehdr->e_type == ET_EXEC) {
        vga_writestring("[ELF] Type: Static Executable (ET_EXEC)\n");
        load_base = 0;  /* No relocation needed */
    } else {
        vga_writestring("[ELF] ERROR: unsupported ELF type: ");
        print_dec(ehdr->e_type);
        vga_writestring("\n");
        kfree(buf);
        return -1;
    }

    uint64_t entry = ehdr->e_entry + load_base;
    vga_writestring("[ELF] Entry (base=0x");
    print_hex(load_base);
    vga_writestring("): 0x");
    print_hex(entry);
    vga_writestring("\n");

    /* Load segments */
    int loaded = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)
            (buf + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        uint64_t vaddr = ph->p_vaddr + load_base;
        uint64_t memsz = ph->p_memsz;
        
        vga_writestring("[ELF] Loading PT_LOAD segment ");
        print_dec(loaded + 1);
        vga_writestring(": vaddr=0x");
        print_hex(vaddr);
        vga_writestring(" size=");
        print_dec(memsz);
        vga_writestring(" bytes\n");

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
        loaded++;
    }

    if (loaded == 0) {
        vga_writestring("[ELF] ERROR: no segments\n");
        kfree(buf);
        return -1;
    }

    vga_writestring("[ELF] Loaded ");
    print_dec(loaded);
    vga_writestring(" segments\n");

    /* Process relocations if present (needed for ET_DYN) */
    if (ehdr->e_shoff > 0 && ehdr->e_shnum > 0) {
        if (process_relocations(buf, ehdr, load_base) != 0) {
            vga_writestring("[ELF] WARNING: relocation processing failed\n");
            /* Don't fail completely - relocation might not be critical */
        }
    }

    kfree(buf);

    /* Set up user stack and validate it doesn't collide */
    if (!is_valid_user_range(USER_STACK_BOTTOM, USER_STACK_PAGES * 4096ULL)) {
        vga_writestring("[ELF] ERROR: user stack range invalid\n");
        return -1;
    }
    
    memset((void *)USER_STACK_BOTTOM, 0, USER_STACK_TOP - USER_STACK_BOTTOM);
    
    vga_writestring("[ELF] Stack: 0x");
    print_hex(USER_STACK_BOTTOM);
    vga_writestring(" - 0x");
    print_hex(USER_STACK_TOP);
    vga_writestring(" (");
    print_dec(USER_STACK_PAGES * 4096);
    vga_writestring(" bytes)\n");

    install_user_gdt_entries();

    vga_writestring("\n[ELF] Transferring to user space:\n");
    vga_writestring("[ELF]   RIP = 0x");
    print_hex(entry);
    vga_writestring("\n[ELF]   RSP = 0x");
    print_hex(USER_STACK_TOP);
    vga_writestring("\n[ELF]   CS  = 0x");
    print_hex(USER_CS);
    vga_writestring(" (GDT[4])\n");
    vga_writestring("[ELF]   SS  = 0x");
    print_hex(USER_DS);
    vga_writestring(" (GDT[3])\n\n");

    __asm__ volatile (
        "pushq %[ss]\n\t"
        "pushq %[rsp]\n\t"
        "pushq %[rflags]\n\t"
        "pushq %[cs]\n\t"
        "pushq %[rip]\n\t"
        "iretq\n\t"
        :
        : [rip]    "r" (entry),
          [rsp]    "r" (USER_STACK_TOP),
          [cs]     "i" (USER_CS),
          [ss]     "i" (USER_DS),
          [rflags] "i" (0x202)
        : "memory"
    );

    return 0;
}