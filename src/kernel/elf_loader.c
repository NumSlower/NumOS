/*
 * elf_loader.c - NumOS ELF64 Loader
 *
 * Loads a statically-linked ELF64 executable from the FAT32 volume (or from
 * a memory buffer) into user virtual memory, ready for execution.
 *
 * Steps performed for each PT_LOAD segment:
 *   1. Validate alignment and address range.
 *   2. Allocate physical frames (one per page) via pmm_alloc_frame().
 *   3. Map each frame with the correct flags (RX / R / RW) plus PAGE_USER
 *      via paging_map_page().
 *   4. Copy file bytes from the FAT32 buffer into the mapped pages.
 *   5. Zero-fill the BSS region (memsz > filesz).
 *
 * After all segments are loaded a user stack is allocated and mapped.
 * The caller receives an elf_load_result with the entry point and stack top.
 */

#include "kernel/elf_loader.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/paging.h"
#include "cpu/heap.h"
#include "fs/fat32.h"

/* -------------------------------------------------------------------------
 * Helper: set error string and return code
 * ---------------------------------------------------------------------- */
static int elf_err(struct elf_load_result *r, int code, const char *msg) {
    r->success = 0;
    strncpy(r->error, msg, sizeof(r->error) - 1);
    r->error[sizeof(r->error) - 1] = '\0';
    return code;
}

/* -------------------------------------------------------------------------
 * elf_validate
 * ---------------------------------------------------------------------- */
int elf_validate(const struct elf64_hdr *hdr) {
    if (hdr->e_ident[EI_MAG0] != ELF_MAGIC0 ||
        hdr->e_ident[EI_MAG1] != ELF_MAGIC1 ||
        hdr->e_ident[EI_MAG2] != ELF_MAGIC2 ||
        hdr->e_ident[EI_MAG3] != ELF_MAGIC3) {
        return ELF_ERR_MAGIC;
    }
    if (hdr->e_ident[EI_CLASS] != ELFCLASS64)    return ELF_ERR_CLASS;
    if (hdr->e_ident[EI_DATA]  != ELFDATA2LSB)   return ELF_ERR_CLASS;
    if (hdr->e_machine         != EM_X86_64)     return ELF_ERR_MACHINE;
    if (hdr->e_type            != ET_EXEC)        return ELF_ERR_TYPE;
    if (hdr->e_phnum           == 0)              return ELF_ERR_NOPHDR;
    return ELF_OK;
}

/* -------------------------------------------------------------------------
 * elf_print_info
 * ---------------------------------------------------------------------- */
void elf_print_info(const struct elf64_hdr *hdr) {
    vga_writestring("  ELF64 executable\n");
    vga_writestring("  Entry:   0x");
    print_hex(hdr->e_entry);
    vga_writestring("\n  PHDRs:  ");
    print_dec(hdr->e_phnum);
    vga_writestring("\n");

    const struct elf64_phdr *ph =
        (const struct elf64_phdr *)((const uint8_t *)hdr + hdr->e_phoff);

    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        if (ph[i].p_type != PT_LOAD) continue;
        vga_writestring("  LOAD  vaddr=0x");
        print_hex(ph[i].p_vaddr);
        vga_writestring("  filesz=");
        print_dec(ph[i].p_filesz);
        vga_writestring("  memsz=");
        print_dec(ph[i].p_memsz);
        vga_writestring("  flags=");
        if (ph[i].p_flags & PF_R) vga_putchar('R');
        if (ph[i].p_flags & PF_W) vga_putchar('W');
        if (ph[i].p_flags & PF_X) vga_putchar('X');
        vga_putchar('\n');
    }
}

/* -------------------------------------------------------------------------
 * map_segment
 *
 * Maps one PT_LOAD segment from `data` (file image) into virtual memory.
 * p_flags → PAGE_ flags translation:
 *   PF_R                → PAGE_PRESENT | PAGE_USER
 *   PF_R | PF_W         → PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE
 *   PF_R | PF_X         → PAGE_PRESENT | PAGE_USER   (no NX)
 * ---------------------------------------------------------------------- */
static int map_segment(const uint8_t *data, size_t data_size,
                       const struct elf64_phdr *ph,
                       uint64_t *load_base_out, uint64_t *load_end_out) {
    if (ph->p_memsz == 0) return ELF_OK;

    /* Page-align the virtual address range */
    uint64_t vaddr_start = paging_align_down(ph->p_vaddr, PAGE_SIZE);
    uint64_t vaddr_end   = paging_align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);

    /* Build page flags */
    uint64_t pflags = PAGE_PRESENT | PAGE_USER;
    if (ph->p_flags & PF_W) pflags |= PAGE_WRITABLE;
    /* NX not set for executable segments (no PAGE_NX) */

    /* Map page by page */
    for (uint64_t virt = vaddr_start; virt < vaddr_end; virt += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return ELF_ERR_NOMEM;

        if (paging_map_page(virt, phys, pflags) != 0) {
            pmm_free_frame(phys);
            return ELF_ERR_MAP;
        }

        /* Zero the page via its virtual address (now that it's mapped) */
        memset((void *)virt, 0, PAGE_SIZE);

        /*
         * Copy the portion of the file image that falls in this page.
         * Offset within the segment: page_virt_offset = virt - p_vaddr (clamped).
         */
        int64_t seg_offset = (int64_t)virt - (int64_t)ph->p_vaddr;

        if (seg_offset < (int64_t)ph->p_filesz) {
            /* There is file data to copy into this page */
            uint64_t file_off    = ph->p_offset + (uint64_t)(seg_offset < 0 ? 0 : seg_offset);
            uint64_t copy_start  = (seg_offset < 0) ? (uint64_t)(-seg_offset) : 0;
            uint64_t copy_count  = PAGE_SIZE - copy_start;

            /* Clamp to what's actually available in the file */
            uint64_t avail = ph->p_filesz - (uint64_t)(seg_offset < 0 ? 0 : seg_offset);
            if (copy_count > avail) copy_count = avail;
            if (file_off + copy_count > data_size) {
                copy_count = (file_off < data_size) ? (data_size - file_off) : 0;
            }

            if (copy_count > 0) {
                memcpy((void *)(virt + copy_start),
                       data + file_off,
                       (size_t)copy_count);
            }
        }
        /* BSS area is already zeroed by the memset above */
    }

    /* Track the overall load extent */
    if (vaddr_start < *load_base_out || *load_base_out == 0) {
        *load_base_out = vaddr_start;
    }
    if (vaddr_end > *load_end_out) {
        *load_end_out = vaddr_end;
    }

    return ELF_OK;
}

/* -------------------------------------------------------------------------
 * allocate_user_stack
 *
 * Allocates USER_STACK_SIZE bytes below `stack_top_virt` and maps them
 * as PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER.
 * Returns the stack top (highest address, ready for rsp) or 0 on failure.
 * ---------------------------------------------------------------------- */
#define USER_STACK_PAGES   16    /* 16 × 4096 = 64 KB user stack */

static uint64_t allocate_user_stack(uint64_t stack_top_virt) {
    uint64_t stack_bottom = stack_top_virt - (USER_STACK_PAGES * PAGE_SIZE);
    uint64_t pflags       = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    for (uint64_t virt = stack_bottom; virt < stack_top_virt; virt += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return 0;

        if (paging_map_page(virt, phys, pflags) != 0) {
            pmm_free_frame(phys);
            return 0;
        }

        /* Zero via virtual address now that the page is mapped */
        memset((void *)virt, 0, PAGE_SIZE);
    }

    /* Return the top of the stack (16-byte aligned, subtract 8 for ABI) */
    return (stack_top_virt - 8) & ~(uint64_t)0xF;
}

/* -------------------------------------------------------------------------
 * elf_load_from_memory  (core implementation)
 * ---------------------------------------------------------------------- */
int elf_load_from_memory(const void *elf_data, size_t elf_size,
                         struct elf_load_result *result) {
    memset(result, 0, sizeof(*result));

    if (!elf_data || elf_size < sizeof(struct elf64_hdr)) {
        return elf_err(result, ELF_ERR_IO, "Buffer too small");
    }

    const struct elf64_hdr *hdr = (const struct elf64_hdr *)elf_data;

    /* Validate the ELF header */
    int v = elf_validate(hdr);
    if (v != ELF_OK) {
        const char *msg = "ELF validation failed";
        switch (v) {
            case ELF_ERR_MAGIC:   msg = "Not an ELF file (bad magic)"; break;
            case ELF_ERR_CLASS:   msg = "Not an ELF64 file";           break;
            case ELF_ERR_MACHINE: msg = "Not x86-64";                  break;
            case ELF_ERR_TYPE:    msg = "Not an executable (ET_EXEC)"; break;
            case ELF_ERR_NOPHDR:  msg = "No program headers";          break;
            default: break;
        }
        return elf_err(result, v, msg);
    }

    vga_writestring("ELF: Loading ");
    print_dec(hdr->e_phnum);
    vga_writestring(" program headers, entry=0x");
    print_hex(hdr->e_entry);
    vga_writestring("\n");

    /* Validate program header table bounds */
    if (hdr->e_phoff + (uint64_t)hdr->e_phnum * sizeof(struct elf64_phdr) > elf_size) {
        return elf_err(result, ELF_ERR_IO, "PHDR table out of bounds");
    }

    const struct elf64_phdr *phdrs =
        (const struct elf64_phdr *)((const uint8_t *)elf_data + hdr->e_phoff);

    uint64_t load_base = 0;
    uint64_t load_end  = 0;

    /* Process every PT_LOAD segment */
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const struct elf64_phdr *ph = &phdrs[i];

        if (ph->p_type != PT_LOAD) continue;

        vga_writestring("ELF:   Segment ");
        print_dec(i);
        vga_writestring(": vaddr=0x");
        print_hex(ph->p_vaddr);
        vga_writestring(" filesz=");
        print_dec(ph->p_filesz);
        vga_writestring(" memsz=");
        print_dec(ph->p_memsz);
        vga_writestring("\n");

        /* Validate segment file range */
        if (ph->p_offset + ph->p_filesz > elf_size) {
            return elf_err(result, ELF_ERR_IO, "Segment extends past file end");
        }

        int err = map_segment((const uint8_t *)elf_data, elf_size,
                              ph, &load_base, &load_end);
        if (err != ELF_OK) {
            const char *msg = "Segment mapping failed";
            if (err == ELF_ERR_NOMEM) msg = "Out of physical memory";
            if (err == ELF_ERR_MAP)   msg = "Page table mapping failed";
            return elf_err(result, err, msg);
        }
    }

    /* Allocate user stack just below USER_STACK_TOP */
    uint64_t stack_top_virt = USER_STACK_TOP;  /* 0x800000 from paging.h */
    uint64_t stack_top = allocate_user_stack(stack_top_virt);
    if (!stack_top) {
        return elf_err(result, ELF_ERR_STACK, "User stack allocation failed");
    }

    vga_writestring("ELF: User stack: 0x");
    print_hex(stack_top_virt - USER_STACK_PAGES * PAGE_SIZE);
    vga_writestring(" - 0x");
    print_hex(stack_top_virt);
    vga_writestring("\n");

    /* Fill in result */
    result->success    = 1;
    result->entry      = hdr->e_entry;
    result->load_base  = load_base;
    result->load_end   = load_end;
    result->stack_top  = stack_top;

    vga_writestring("ELF: Load complete. entry=0x");
    print_hex(result->entry);
    vga_writestring(" stack_top=0x");
    print_hex(result->stack_top);
    vga_writestring("\n");

    return ELF_OK;
}

/* -------------------------------------------------------------------------
 * elf_load_from_file
 *
 * Reads the entire file into a heap buffer, then calls elf_load_from_memory.
 * ---------------------------------------------------------------------- */
int elf_load_from_file(const char *path, struct elf_load_result *result) {
    memset(result, 0, sizeof(*result));

    vga_writestring("ELF: Opening '");
    vga_writestring(path);
    vga_writestring("'...\n");

    /* Stat the file to get its size */
    struct fat32_dirent stat;
    if (fat32_stat(path, &stat) != 0) {
        return elf_err(result, ELF_ERR_IO, "File not found");
    }

    if (stat.size == 0) {
        return elf_err(result, ELF_ERR_IO, "File is empty");
    }

    vga_writestring("ELF: File size = ");
    print_dec(stat.size);
    vga_writestring(" bytes\n");

    /* Allocate a heap buffer to hold the entire file */
    uint8_t *buf = (uint8_t *)kmalloc(stat.size);
    if (!buf) {
        return elf_err(result, ELF_ERR_NOMEM, "Cannot allocate read buffer");
    }

    /* Open and read the file */
    int fd = fat32_open(path, 0x01 /* O_RDONLY */);
    if (fd < 0) {
        kfree(buf);
        return elf_err(result, ELF_ERR_IO, "Cannot open file");
    }

    ssize_t got = fat32_read(fd, buf, stat.size);
    fat32_close(fd);

    if (got < 0 || (uint32_t)got != stat.size) {
        kfree(buf);
        return elf_err(result, ELF_ERR_IO, "Read returned wrong size");
    }

    vga_writestring("ELF: Read ");
    print_dec((uint64_t)got);
    vga_writestring(" bytes OK\n");

    /* Parse and map */
    int rc = elf_load_from_memory(buf, (size_t)got, result);

    kfree(buf);
    return rc;
}