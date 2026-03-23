/*
 * elf_loader.c - NumOS ELF64 Loader
 *
 * Loads a statically-linked ELF64 executable from the FAT32 volume (or a
 * memory buffer) into user virtual memory, ready for SYSRETQ execution.
 *
 * Steps performed for each PT_LOAD segment:
 *   1. Validate alignment and address range against the file buffer.
 *   2. Allocate one physical frame per 4 KB page via pmm_alloc_frame().
 *   3. Map each frame with correct flags (RX / R / RW) plus PAGE_USER
 *      via paging_map_page().
 *   4. Copy file bytes into the mapped frames using the identity-mapped
 *      physical addresses (physical == virtual for low memory in NumOS).
 *   5. Zero-fill the BSS region (memsz > filesz).
 *
 * After loading a user stack is allocated and mapped immediately below
 * USER_STACK_TOP.  The caller receives an elf_load_result with the entry
 * point and aligned stack top.
 */

#include "kernel/elf_loader.h"
#include "kernel/kernel.h"
#include "drivers/graphices/vga.h"
#include "cpu/paging.h"
#include "fs/fat32.h"
#include "fs/vfs.h"

/* =========================================================================
 * Internal helpers
 * ======================================================================= */

/*
 * elf_err - record an error string in result and return the error code.
 */
static int elf_err(struct elf_load_result *r, int code, const char *msg) {
    r->success = 0;
    strncpy(r->error, msg, sizeof(r->error) - 1);
    r->error[sizeof(r->error) - 1] = '\0';
    return code;
}

/* =========================================================================
 * Validation
 * ======================================================================= */

/*
 * elf_validate - check the ELF header magic, class, machine, and type.
 * Returns ELF_OK on success, or a negative ELF_ERR_* code on failure.
 * Does not map any memory.
 */
int elf_validate(const struct elf64_hdr *hdr) {
    if (hdr->e_ident[EI_MAG0] != ELF_MAGIC0 ||
        hdr->e_ident[EI_MAG1] != ELF_MAGIC1 ||
        hdr->e_ident[EI_MAG2] != ELF_MAGIC2 ||
        hdr->e_ident[EI_MAG3] != ELF_MAGIC3) {
        return ELF_ERR_MAGIC;
    }
    if (hdr->e_ident[EI_CLASS] != ELFCLASS64)  return ELF_ERR_CLASS;
    if (hdr->e_ident[EI_DATA]  != ELFDATA2LSB) return ELF_ERR_CLASS;
    if (hdr->e_machine         != EM_X86_64)   return ELF_ERR_MACHINE;
    if (hdr->e_type            != ET_EXEC)      return ELF_ERR_TYPE;
    if (hdr->e_phnum           == 0)            return ELF_ERR_NOPHDR;
    return ELF_OK;
}

/* =========================================================================
 * Segment mapping
 * ======================================================================= */

/*
 * map_segment - allocate physical frames and map one PT_LOAD segment.
 *
 * Page permission mapping:
 *   PF_R                -> PAGE_PRESENT | PAGE_USER
 *   PF_R | PF_W         -> PAGE_PRESENT | PAGE_USER | PAGE_WRITABLE
 *   PF_R | PF_X         -> PAGE_PRESENT | PAGE_USER   (NX not set)
 *
 * File bytes are copied using the physical address directly because NumOS
 * identity-maps the first 1 GB of RAM in 2 MB huge pages; physical addresses
 * below 1 GB are simultaneously valid virtual addresses.
 *
 * Updates *load_base_out and *load_end_out to track the overall mapped extent.
 */
static int map_segment(const uint8_t        *data,
                       size_t                data_size,
                       const struct elf64_phdr *ph,
                       uint64_t             *load_base_out,
                       uint64_t             *load_end_out) {
    if (ph->p_memsz == 0) return ELF_OK;

    uint64_t vaddr_start = paging_align_down(ph->p_vaddr, PAGE_SIZE);
    uint64_t vaddr_end   = paging_align_up(ph->p_vaddr + ph->p_memsz, PAGE_SIZE);

    uint64_t pflags = PAGE_PRESENT | PAGE_USER;
    if (ph->p_flags & PF_W) pflags |= PAGE_WRITABLE;

    for (uint64_t virt = vaddr_start; virt < vaddr_end; virt += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return ELF_ERR_NOMEM;

        if (paging_map_page(virt, phys, pflags) != 0) {
            pmm_free_frame(phys);
            return ELF_ERR_MAP;
        }

        /* Zero-fill the frame before writing segment data */
        memset((void *)phys, 0, PAGE_SIZE);

        /* Calculate how many file bytes fall in this page */
        int64_t seg_offset = (int64_t)virt - (int64_t)ph->p_vaddr;

        if (seg_offset < (int64_t)ph->p_filesz) {
            uint64_t file_off   = ph->p_offset +
                                  (uint64_t)(seg_offset < 0 ? 0 : seg_offset);
            uint64_t copy_start = (seg_offset < 0) ? (uint64_t)(-seg_offset) : 0;
            uint64_t copy_count = PAGE_SIZE - copy_start;

            uint64_t avail = ph->p_filesz -
                             (uint64_t)(seg_offset < 0 ? 0 : seg_offset);
            if (copy_count > avail) copy_count = avail;
            if (file_off + copy_count > data_size) {
                copy_count = (file_off < data_size)
                             ? (data_size - file_off) : 0;
            }

            if (copy_count > 0) {
                memcpy((void *)(phys + copy_start),
                       data + file_off,
                       (size_t)copy_count);
            }
        }
    }

    /* Update the overall load extent */
    if (*load_base_out == 0 || vaddr_start < *load_base_out)
        *load_base_out = vaddr_start;
    if (vaddr_end > *load_end_out)
        *load_end_out = vaddr_end;

    return ELF_OK;
}

/* =========================================================================
 * Stack allocation
 * ======================================================================= */

/* Number of 4 KB pages to allocate for the user stack */
#define USER_STACK_PAGES  16   /* 64 KB */

/*
 * allocate_user_stack - map USER_STACK_PAGES pages immediately below
 * stack_top_virt as PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER.
 *
 * Returns the aligned RSP value (stack_top_virt - 8, 16-byte aligned) on
 * success, or 0 on failure.  Sets *stack_bottom_out to the lowest mapped
 * virtual address.
 */
static uint64_t allocate_user_stack(uint64_t  stack_top_virt,
                                    uint64_t *stack_bottom_out) {
    uint64_t stack_bottom = stack_top_virt - (USER_STACK_PAGES * PAGE_SIZE);
    uint64_t pflags       = PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;

    for (uint64_t virt = stack_bottom; virt < stack_top_virt; virt += PAGE_SIZE) {
        uint64_t phys = pmm_alloc_frame();
        if (!phys) return 0;

        if (paging_map_page(virt, phys, pflags) != 0) {
            pmm_free_frame(phys);
            return 0;
        }

        memset((void *)phys, 0, PAGE_SIZE);
    }

    if (stack_bottom_out) *stack_bottom_out = stack_bottom;

    /* Return 16-byte aligned RSP, 8 bytes below the top (System V ABI) */
    return (stack_top_virt - 8) & ~(uint64_t)0xF;
}

/* =========================================================================
 * Core loader: from memory buffer
 * ======================================================================= */

/*
 * elf_load_from_memory - parse elf_data, map PT_LOAD segments, and allocate
 * a user stack.  Fills *result on both success and failure.
 * Returns ELF_OK (0) or a negative ELF_ERR_* code.
 */
int elf_load_from_memory(const void *elf_data,
                         size_t      elf_size,
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
            case ELF_ERR_MAGIC:   msg = "Not an ELF file (bad magic)";    break;
            case ELF_ERR_CLASS:   msg = "Not an ELF64 / little-endian";   break;
            case ELF_ERR_MACHINE: msg = "Not x86-64";                     break;
            case ELF_ERR_TYPE:    msg = "Not an executable (ET_EXEC)";    break;
            case ELF_ERR_NOPHDR:  msg = "No program headers";             break;
            default: break;
        }
        return elf_err(result, v, msg);
    }

    vga_writestring("ELF: Loading ");
    print_dec(hdr->e_phnum);
    vga_writestring(" program headers, entry=0x");
    print_hex(hdr->e_entry);
    vga_writestring("\n");

    /* Bounds-check the program header table */
    if (hdr->e_phoff +
        (uint64_t)hdr->e_phnum * sizeof(struct elf64_phdr) > elf_size) {
        return elf_err(result, ELF_ERR_IO, "PHDR table out of bounds");
    }

    const struct elf64_phdr *phdrs =
        (const struct elf64_phdr *)((const uint8_t *)elf_data + hdr->e_phoff);

    uint64_t load_base = 0;
    uint64_t load_end  = 0;

    /* Map each PT_LOAD segment */
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const struct elf64_phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD) continue;

        vga_writestring("ELF:   Segment ");
        print_dec(i);
        vga_writestring(": vaddr=0x"); print_hex(ph->p_vaddr);
        vga_writestring(" filesz=");   print_dec(ph->p_filesz);
        vga_writestring(" memsz=");    print_dec(ph->p_memsz);
        vga_writestring("\n");

        if (ph->p_offset + ph->p_filesz > elf_size) {
            return elf_err(result, ELF_ERR_IO,
                           "Segment extends past file end");
        }

        int err = map_segment((const uint8_t *)elf_data, elf_size,
                              ph, &load_base, &load_end);
        if (err != ELF_OK) {
            return elf_err(result, err,
                           (err == ELF_ERR_NOMEM) ? "Out of physical memory"
                                                  : "Page table mapping failed");
        }
    }

    /* Allocate the user stack below USER_STACK_TOP */
    uint64_t stack_bottom = 0;
    uint64_t stack_top    = allocate_user_stack(USER_STACK_TOP, &stack_bottom);
    if (!stack_top) {
        return elf_err(result, ELF_ERR_STACK,
                       "User stack allocation failed");
    }

    vga_writestring("ELF: User stack: 0x");
    print_hex(stack_bottom);
    vga_writestring(" - 0x");
    print_hex(USER_STACK_TOP);
    vga_writestring("\n");

    /* Populate the result */
    result->success      = 1;
    result->entry        = hdr->e_entry;
    result->load_base    = load_base;
    result->load_end     = load_end;
    result->stack_top    = stack_top;
    result->stack_bottom = stack_bottom;

    vga_writestring("ELF: Load complete. entry=0x");
    print_hex(result->entry);
    vga_writestring(" stack_top=0x");
    print_hex(result->stack_top);
    vga_writestring("\n");

    return ELF_OK;
}

/* =========================================================================
 * Loader: from FAT32 file
 * ======================================================================= */

/*
 * elf_load_from_file - read the file at path into a heap buffer, then call
 * elf_load_from_memory().  Frees the buffer before returning.
 */
int elf_load_from_file(const char *path, struct elf_load_result *result) {
    memset(result, 0, sizeof(*result));

    vga_writestring("ELF: Opening '");
    vga_writestring(path);
    vga_writestring("'...\n");

    /* Stat the file to obtain its size */
    struct vfs_stat stat;
    if (vfs_stat(path, &stat) != 0) {
        return elf_err(result, ELF_ERR_IO, "File not found");
    }
    if (stat.size == 0) {
        return elf_err(result, ELF_ERR_IO, "File is empty");
    }

    vga_writestring("ELF: File size = ");
    print_dec(stat.size);
    vga_writestring(" bytes\n");

    /* Allocate a contiguous heap buffer for the entire file */
    uint8_t *buf = (uint8_t *)kmalloc(stat.size);
    if (!buf) {
        return elf_err(result, ELF_ERR_NOMEM,
                       "Cannot allocate read buffer");
    }

    /* Read the file */
    int fd = vfs_open(path, FAT32_O_RDONLY);
    if (fd < 0) {
        kfree(buf);
        return elf_err(result, ELF_ERR_IO, "Cannot open file");
    }

    ssize_t got = vfs_read(fd, buf, stat.size);
    vfs_close(fd);

    if (got < 0 || (uint32_t)got != stat.size) {
        kfree(buf);
        return elf_err(result, ELF_ERR_IO, "Read returned wrong byte count");
    }

    vga_writestring("ELF: Read ");
    print_dec((uint64_t)got);
    vga_writestring(" bytes OK\n");

    int rc = elf_load_from_memory(buf, (size_t)got, result);
    kfree(buf);
    return rc;
}

/* =========================================================================
 * Cleanup
 * ======================================================================= */

/*
 * elf_unload - unmap the ELF segment pages and user stack pages and free
 * their backing physical frames.
 *
 * paging_unmap_page() unmaps the page AND frees the physical frame.
 * Flushes the TLB by reloading CR3 after all unmaps.
 */
void elf_unload(uint64_t load_base,    uint64_t load_end,
                uint64_t stack_bottom, uint64_t stack_top_page) {
    uint64_t pages_freed = 0;

    /* Unmap ELF segment pages */
    if (load_base && load_end > load_base) {
        for (uint64_t virt = load_base; virt < load_end; virt += PAGE_SIZE) {
            if (paging_unmap_page(virt) == 0) pages_freed++;
        }
    }

    /* Unmap user stack pages */
    if (stack_bottom && stack_top_page > stack_bottom) {
        for (uint64_t virt = stack_bottom; virt < stack_top_page; virt += PAGE_SIZE) {
            if (paging_unmap_page(virt) == 0) pages_freed++;
        }
    }

    /* Full TLB flush via CR3 reload */
    __asm__ volatile(
        "mov %%cr3, %%rax\n\t"
        "mov %%rax, %%cr3\n\t"
        ::: "rax", "memory"
    );

    vga_writestring("ELF: Unloaded ");
    print_dec(pages_freed);
    vga_writestring(" pages\n");
}
