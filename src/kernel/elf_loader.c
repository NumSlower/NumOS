/*
 * elf_loader.c — NumOS minimal ELF64 loader
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

    uint64_t entry = ehdr->e_entry;
    vga_writestring("[ELF] Entry: ");
    print_hex(entry);
    vga_writestring("\n");

    int loaded = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        struct elf64_phdr *ph = (struct elf64_phdr *)
            (buf + ehdr->e_phoff + (uint64_t)i * ehdr->e_phentsize);

        if (ph->p_type != PT_LOAD) continue;

        vga_writestring("[ELF] Loading vaddr=");
        print_hex(ph->p_vaddr);
        vga_writestring(" size=");
        print_dec(ph->p_memsz);
        vga_writestring("\n");

        if (ph->p_vaddr + ph->p_memsz > 128ULL * 1024 * 1024) {
            vga_writestring("[ELF] ERROR: segment too large\n");
            kfree(buf);
            return -1;
        }

        uint8_t *dst = (uint8_t *)(uintptr_t)ph->p_vaddr;
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

    kfree(buf);

    memset((void *)USER_STACK_BOTTOM, 0, USER_STACK_TOP - USER_STACK_BOTTOM);
    
    vga_writestring("[ELF] Stack: ");
    print_hex(USER_STACK_BOTTOM);
    vga_writestring(" - ");
    print_hex(USER_STACK_TOP);
    vga_writestring("\n");

    install_user_gdt_entries();

    vga_writestring("\n[ELF] Jumping to user space:\n");
    vga_writestring("[ELF]   RIP = ");
    print_hex(entry);
    vga_writestring("\n[ELF]   RSP = ");
    print_hex(USER_STACK_TOP);
    vga_writestring("\n[ELF]   CS  = 0x");
    print_hex(USER_CS);
    vga_writestring(" (GDT[4])\n[ELF]   SS  = 0x");
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