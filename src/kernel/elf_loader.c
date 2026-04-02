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
#include "kernel/numloss.h"
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
    if (hdr->e_type != ET_EXEC && hdr->e_type != ET_DYN) return ELF_ERR_TYPE;
    if (hdr->e_phnum           == 0)            return ELF_ERR_NOPHDR;
    return ELF_OK;
}

/* =========================================================================
 * Segment mapping
 * ======================================================================= */

static uint64_t compute_load_bias(const struct elf64_hdr *hdr,
                                  const struct elf64_phdr *phdrs) {
    if (hdr->e_type != ET_DYN) return 0;

    uint64_t min_vaddr = UINT64_MAX;
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const struct elf64_phdr *ph = &phdrs[i];
        if (ph->p_type != PT_LOAD || ph->p_memsz == 0) continue;
        uint64_t seg_start = paging_align_down(ph->p_vaddr, PAGE_SIZE);
        if (seg_start < min_vaddr) min_vaddr = seg_start;
    }

    if (min_vaddr == UINT64_MAX) return 0;
    return USER_VIRTUAL_BASE - min_vaddr;
}

static int apply_relocation_table(const struct elf64_rela *rela,
                                  uint64_t count,
                                  const struct elf64_sym *symtab,
                                  uint64_t sym_ent_size,
                                  uint64_t load_bias) {
    for (uint64_t i = 0; i < count; i++) {
        const struct elf64_rela *ent = &rela[i];
        uint32_t type = ELF64_R_TYPE(ent->r_info);
        uint32_t sym_index = ELF64_R_SYM(ent->r_info);
        uint64_t *where = (uint64_t *)(uintptr_t)(load_bias + ent->r_offset);
        uint64_t value = 0;

        switch (type) {
            case R_X86_64_NONE:
                continue;
            case R_X86_64_RELATIVE:
                value = load_bias + (uint64_t)ent->r_addend;
                break;
            case R_X86_64_64:
            case R_X86_64_GLOB_DAT:
            case R_X86_64_JUMP_SLOT:
                if (!symtab || sym_ent_size != sizeof(struct elf64_sym)) {
                    return ELF_ERR_IO;
                }
                value = load_bias + symtab[sym_index].st_value;
                if (type == R_X86_64_64) value += (uint64_t)ent->r_addend;
                break;
            default:
                return ELF_ERR_IO;
        }

        *where = value;
    }

    return ELF_OK;
}

static int apply_dynamic_relocations(const struct elf64_phdr *phdrs,
                                     uint16_t phnum,
                                     uint64_t load_bias) {
    const struct elf64_dyn *dyn = NULL;
    uint64_t dyn_count = 0;

    for (uint16_t i = 0; i < phnum; i++) {
        if (phdrs[i].p_type != PT_DYNAMIC || phdrs[i].p_memsz == 0) continue;
        dyn = (const struct elf64_dyn *)(uintptr_t)(load_bias + phdrs[i].p_vaddr);
        dyn_count = phdrs[i].p_memsz / sizeof(struct elf64_dyn);
        break;
    }

    if (!dyn || dyn_count == 0) return ELF_OK;

    const struct elf64_rela *rela = NULL;
    uint64_t rela_size = 0;
    uint64_t rela_ent = sizeof(struct elf64_rela);
    uint64_t rela_count_hint = 0;
    const struct elf64_sym *symtab = NULL;
    uint64_t sym_ent = sizeof(struct elf64_sym);

    for (uint64_t i = 0; i < dyn_count; i++) {
        switch ((uint64_t)dyn[i].d_tag) {
            case DT_NULL:
                i = dyn_count;
                break;
            case DT_RELA:
                rela = (const struct elf64_rela *)(uintptr_t)(load_bias + dyn[i].d_un.d_ptr);
                break;
            case DT_RELASZ:
                rela_size = dyn[i].d_un.d_val;
                break;
            case DT_RELAENT:
                rela_ent = dyn[i].d_un.d_val;
                break;
            case DT_RELACOUNT:
                rela_count_hint = dyn[i].d_un.d_val;
                break;
            case DT_SYMTAB:
                symtab = (const struct elf64_sym *)(uintptr_t)(load_bias + dyn[i].d_un.d_ptr);
                break;
            case DT_SYMENT:
                sym_ent = dyn[i].d_un.d_val;
                break;
            default:
                break;
        }
    }

    if (!rela || rela_size == 0) return ELF_OK;
    if (rela_ent != sizeof(struct elf64_rela)) return ELF_ERR_IO;

    uint64_t rela_count = rela_size / rela_ent;
    if (rela_count_hint != 0 && rela_count_hint < rela_count) {
        rela_count = rela_count_hint;
    }

    return apply_relocation_table(rela, rela_count, symtab, sym_ent, load_bias);
}

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
                       uint64_t              load_bias,
                       uint64_t             *load_base_out,
                       uint64_t             *load_end_out) {
    if (ph->p_memsz == 0) return ELF_OK;

    uint64_t seg_vaddr = ph->p_vaddr + load_bias;
    uint64_t vaddr_start = paging_align_down(seg_vaddr, PAGE_SIZE);
    uint64_t vaddr_end   = paging_align_up(seg_vaddr + ph->p_memsz, PAGE_SIZE);

    uint64_t pflags = PAGE_PRESENT | PAGE_USER;
    if (ph->p_flags & PF_W) pflags |= PAGE_WRITABLE;

    for (uint64_t virt = vaddr_start; virt < vaddr_end; virt += PAGE_SIZE) {
        page_entry_t *entry = paging_get_page_entry(virt, 0);
        uint64_t phys = 0;

        if (entry && (*entry & PAGE_PRESENT)) {
            phys = PAGE_ENTRY_ADDR(*entry);
            uint64_t entry_flags = (*entry & ~0x000FFFFFFFFFF000ULL) |
                                   pflags | PAGE_PRESENT;
            *entry = phys | entry_flags;
            paging_flush_page(virt);
        } else {
            phys = pmm_alloc_frame();
            if (!phys) return ELF_ERR_NOMEM;

            if (paging_map_page(virt, phys, pflags) != 0) {
                pmm_free_frame(phys);
                return ELF_ERR_MAP;
            }

            /* Zero-fill a newly allocated frame before writing segment data */
            memset((void *)(uintptr_t)phys, 0, PAGE_SIZE);
        }

        /* Calculate how many file bytes fall in this page */
        int64_t seg_offset = (int64_t)virt - (int64_t)seg_vaddr;

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
                memcpy((void *)(uintptr_t)(phys + copy_start),
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
    uint64_t load_bias = compute_load_bias(hdr, phdrs);
    uint64_t tls_image_start = 0;
    uint64_t tls_filesz = 0;
    uint64_t tls_memsz = 0;
    uint64_t tls_align = 0;

    /* Map each PT_LOAD segment */
    for (uint16_t i = 0; i < hdr->e_phnum; i++) {
        const struct elf64_phdr *ph = &phdrs[i];
        if (ph->p_type == PT_TLS) {
            tls_image_start = load_bias + ph->p_vaddr;
            tls_filesz = ph->p_filesz;
            tls_memsz = ph->p_memsz;
            tls_align = ph->p_align;
            continue;
        }
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
                              ph, load_bias, &load_base, &load_end);
        if (err != ELF_OK) {
            return elf_err(result, err,
                           (err == ELF_ERR_NOMEM) ? "Out of physical memory"
                                                  : "Page table mapping failed");
        }
    }

    int reloc_rc = apply_dynamic_relocations(phdrs, hdr->e_phnum, load_bias);
    if (reloc_rc != ELF_OK) {
        return elf_err(result, reloc_rc, "Dynamic relocation failed");
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
    result->entry        = hdr->e_entry + load_bias;
    result->load_base    = load_base;
    result->load_end     = load_end;
    result->load_bias    = load_bias;
    result->stack_top    = stack_top;
    result->stack_bottom = stack_bottom;
    result->tls_image_start = tls_image_start;
    result->tls_filesz      = tls_filesz;
    result->tls_memsz       = tls_memsz;
    result->tls_align       = tls_align;

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

    uint8_t *load_buf = buf;
    size_t load_size = (size_t)got;

    if (numloss_is_archive(buf, (uint32_t)got)) {
        uint32_t original_size = 0;
        uint32_t decoded_size = 0;

        if (numloss_read_header(buf, (uint32_t)got, &original_size, 0) != NUMLOSS_OK ||
            original_size == 0) {
            kfree(buf);
            return elf_err(result, ELF_ERR_IO, "Invalid numloss archive");
        }

        load_buf = (uint8_t *)kmalloc(original_size);
        if (!load_buf) {
            kfree(buf);
            return elf_err(result, ELF_ERR_NOMEM,
                           "Cannot allocate numloss buffer");
        }

        if (numloss_decode(buf, (uint32_t)got, load_buf, original_size,
                           &decoded_size) != NUMLOSS_OK ||
            decoded_size != original_size) {
            kfree(load_buf);
            kfree(buf);
            return elf_err(result, ELF_ERR_IO, "Cannot unpack numloss ELF");
        }

        kfree(buf);

        vga_writestring("ELF: Numloss unpacked to ");
        print_dec(decoded_size);
        vga_writestring(" bytes\n");

        load_size = decoded_size;
    }

    int rc = elf_load_from_memory(load_buf, load_size, result);
    kfree(load_buf);
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
