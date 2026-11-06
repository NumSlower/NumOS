/* NumOS — elf.c — ELF64 user-program loader implementation
 *
 * Validates every header field before touching user page tables. Only
 * PT_LOAD segments are mapped; each 4K page of [vaddr, vaddr+memsz) gets a
 * fresh frame, file bytes are copied in, and the BSS tail (memsz > filesz)
 * is zeroed. W^X is honoured from the segment's p_flags. All ranges are
 * checked against the user region and the image size before any copy.
 */
#include "process/elf.h"
#include "process/userprog.h"
#include "kernel/vmm.h"
#include "kernel/pmm.h"
#include "cpu/paging.h"
#include "libs/string.h"
#include "libs/kprintf.h"

struct elf64_ehdr {
    uint8_t  ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed));

struct elf64_phdr {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed));

#define PT_LOAD 1
#define PF_X 1
#define PF_W 2
#define PF_R 4
#define ET_EXEC 2
#define EM_X86_64 62

static bool range_ok(uint64_t start, uint64_t len, uint64_t limit) {
    return start <= limit && len <= limit - start;
}

int elf_load(uint64_t* pml4, const uint8_t* image, uint64_t size, uint64_t* out_entry) {
    if (!pml4 || !image || size < sizeof(struct elf64_ehdr) || !out_entry) {
        return ELF_EINVAL;
    }

    const struct elf64_ehdr* eh = (const struct elf64_ehdr*)image;
    if (eh->ident[0] != 0x7F || eh->ident[1] != 'E' || eh->ident[2] != 'L' || eh->ident[3] != 'F') {
        kprintf("elf: bad magic\n");
        return ELF_EINVAL;
    }
    if (eh->ident[4] != 2) {
        kprintf("elf: not 64-bit class\n");
        return ELF_EINVAL;
    }
    if (eh->ident[5] != 1) {
        kprintf("elf: not little-endian\n");
        return ELF_EINVAL;
    }
    if (eh->type != ET_EXEC || eh->machine != EM_X86_64) {
        kprintf("elf: type %u machine %u (want ET_EXEC/x86_64)\n", eh->type, eh->machine);
        return ELF_EINVAL;
    }
    if (eh->phentsize < sizeof(struct elf64_phdr) || eh->phnum == 0) {
        kprintf("elf: bad phentsize %u or phnum %u\n", eh->phentsize, eh->phnum);
        return ELF_EINVAL;
    }
    if (!range_ok(eh->phoff, (uint64_t)eh->phnum * eh->phentsize, size)) {
        kprintf("elf: program header table out of image\n");
        return ELF_ERANGE;
    }
    if (!range_ok(eh->entry, 1, USER_VA_LIMIT) || eh->entry < USER_VA_BASE) {
        kprintf("elf: entry 0x%lx outside user region\n", eh->entry);
        return ELF_ERANGE;
    }

    for (uint16_t i = 0; i < eh->phnum; i++) {
        const struct elf64_phdr* ph =
            (const struct elf64_phdr*)(image + eh->phoff + (uint64_t)i * eh->phentsize);

        if (ph->type != PT_LOAD) continue;

        if (ph->filesz > ph->memsz) {
            kprintf("elf: seg %u filesz > memsz\n", i);
            return ELF_EINVAL;
        }
        if (!range_ok(ph->offset, ph->filesz, size)) {
            kprintf("elf: seg %u file bytes out of image\n", i);
            return ELF_ERANGE;
        }
        if (!range_ok(ph->vaddr, ph->memsz, USER_VA_LIMIT) || ph->vaddr < USER_VA_BASE) {
            kprintf("elf: seg %u range 0x%lx+0x%lx outside user region\n",
                    i, ph->vaddr, ph->memsz);
            return ELF_ERANGE;
        }
        if (ph->align >= 4096 &&
            (ph->vaddr & (ph->align - 1)) != (ph->offset & (ph->align - 1))) {
            kprintf("elf: seg %u vaddr/offset alignment mismatch\n", i);
            return ELF_EINVAL;
        }

        uint64_t page_flags = PAGE_USER;
        if (ph->flags & PF_W) page_flags |= PAGE_WRITE;
        if (!(ph->flags & PF_X)) page_flags |= PAGE_NX;

        uint64_t start = ph->vaddr & ~0xFFFULL;
        uint64_t end = ph->vaddr + ph->memsz;

        for (uint64_t page = start; page < end; page += 4096) {
            uint64_t frame = pmm_alloc_frame();
            if (!frame) {
                kprintf("elf: out of frames in seg %u\n", i);
                return ELF_ENOMEM;
            }
            memset((void*)frame, 0, 4096);

            /* File bytes overlapping this page:
             * [max(page, vaddr), min(page+4096, vaddr+filesz)). */
            uint64_t data_start = ph->vaddr;
            uint64_t data_end = ph->vaddr + ph->filesz;
            uint64_t lo = page > data_start ? page : data_start;
            uint64_t hi = (page + 4096) < data_end ? (page + 4096) : data_end;
            if (lo < hi) {
                uint64_t frame_off = lo - page;
                uint64_t file_off = ph->offset + (lo - data_start);
                memcpy((void*)(frame + frame_off), image + file_off, hi - lo);
            }

            vmm_map_in(pml4, page, frame, page_flags);
        }
    }

    *out_entry = eh->entry;
    return ELF_LOAD_OK;
}

const char* elf_error_string(int rc) {
    switch (rc) {
        case ELF_LOAD_OK:   return "ok";
        case ELF_EINVAL:    return "invalid image";
        case ELF_ENOMEM:    return "out of memory";
        case ELF_ERANGE:    return "range violation";
        case ELF_EPERM:     return "permission denied";
        default:            return "unknown error";
    }
}
