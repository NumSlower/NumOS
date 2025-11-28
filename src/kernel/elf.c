/*
 * elf.c - ELF64 binary loader
 * Loads and executes 64-bit ELF executables
 */

#include "kernel/elf.h"
#include "kernel/kernel.h"
#include "drivers/vga.h"
#include "cpu/paging.h"
#include "cpu/heap.h"

/* Helper function to check if address is page-aligned */
static inline int is_page_aligned(uint64_t addr) {
    return (addr & 0xFFF) == 0;
}

/* Helper function to align address down to page boundary */
static inline uint64_t align_down(uint64_t addr) {
    return addr & ~0xFFF;
}

/* Helper function to align address up to page boundary */
static inline uint64_t align_up(uint64_t addr) {
    return (addr + 0xFFF) & ~0xFFF;
}

int elf_validate(const void *elf_data) {
    if (!elf_data) {
        return ELF_ERROR_INVALID;
    }
    
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr*)elf_data;
    
    /* Check ELF magic number */
    if (ehdr->e_ident[0] != 0x7F ||
        ehdr->e_ident[1] != 'E' ||
        ehdr->e_ident[2] != 'L' ||
        ehdr->e_ident[3] != 'F') {
        vga_writestring("ELF: Invalid magic number\n");
        return ELF_ERROR_INVALID;
    }
    
    /* Check for 64-bit ELF */
    if (ehdr->e_ident[4] != ELFCLASS64) {
        vga_writestring("ELF: Not a 64-bit ELF file\n");
        return ELF_ERROR_NOT_64BIT;
    }
    
    /* Check endianness (little-endian) */
    if (ehdr->e_ident[5] != ELFDATA2LSB) {
        vga_writestring("ELF: Not little-endian\n");
        return ELF_ERROR_INVALID;
    }
    
    /* Check ELF version */
    if (ehdr->e_ident[6] != 1) {
        vga_writestring("ELF: Invalid ELF version\n");
        return ELF_ERROR_INVALID;
    }
    
    /* Check file type (must be executable) */
    if (ehdr->e_type != ET_EXEC) {
        vga_writestring("ELF: Not an executable file\n");
        return ELF_ERROR_NOT_EXEC;
    }
    
    /* Check machine type (must be x86-64) */
    if (ehdr->e_machine != EM_X86_64) {
        vga_writestring("ELF: Not an x86-64 executable\n");
        return ELF_ERROR_BAD_ARCH;
    }
    
    return ELF_SUCCESS;
}

uint64_t elf_get_entry(const void *elf_data) {
    if (!elf_data) {
        return 0;
    }
    
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr*)elf_data;
    return ehdr->e_entry;
}

void elf_print_info(const void *elf_data) {
    if (!elf_data) {
        vga_writestring("ELF: NULL pointer\n");
        return;
    }
    
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr*)elf_data;
    
    vga_writestring("ELF Binary Information:\n");
    vga_writestring("  Class: ");
    vga_writestring(ehdr->e_ident[4] == ELFCLASS64 ? "64-bit" : "32-bit");
    vga_writestring("\n  Type: ");
    
    switch (ehdr->e_type) {
        case ET_EXEC: vga_writestring("Executable"); break;
        case ET_REL: vga_writestring("Relocatable"); break;
        case ET_DYN: vga_writestring("Shared object"); break;
        default: vga_writestring("Unknown"); break;
    }
    
    vga_writestring("\n  Machine: ");
    if (ehdr->e_machine == EM_X86_64) {
        vga_writestring("x86-64");
    } else {
        vga_writestring("Unknown");
    }
    
    vga_writestring("\n  Entry point: 0x");
    print_hex(ehdr->e_entry);
    vga_writestring("\n  Program headers: ");
    print_dec(ehdr->e_phnum);
    vga_writestring(" at offset 0x");
    print_hex(ehdr->e_phoff);
    vga_writestring("\n  Section headers: ");
    print_dec(ehdr->e_shnum);
    vga_writestring(" at offset 0x");
    print_hex(ehdr->e_shoff);
    vga_writestring("\n");
}

int elf_load(const void *elf_data, uint64_t *entry_point) {
    if (!elf_data) {
        return ELF_ERROR_INVALID;
    }
    
    vga_writestring("ELF: === Starting ELF load process ===\n");
    
    /* Validate ELF file */
    int valid = elf_validate(elf_data);
    if (valid != ELF_SUCCESS) {
        return valid;
    }
    
    const struct elf64_ehdr *ehdr = (const struct elf64_ehdr*)elf_data;
    
    vga_writestring("ELF: File validated successfully\n");
    vga_writestring("ELF: Entry point: 0x");
    print_hex(ehdr->e_entry);
    vga_writestring("\n");
    
    /* Get program header table */
    const struct elf64_phdr *phdr = (const struct elf64_phdr*)((uint8_t*)elf_data + ehdr->e_phoff);
    
    vga_writestring("ELF: Processing ");
    print_dec(ehdr->e_phnum);
    vga_writestring(" program headers...\n");
    
    /* Load each LOAD segment */
    for (int i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type != PT_LOAD) {
            continue;
        }
        
        vga_writestring("ELF: Loading segment ");
        print_dec(i);
        vga_writestring("\n");
        vga_writestring("  Virtual address: 0x");
        print_hex(phdr[i].p_vaddr);
        vga_writestring("\n  File size: ");
        print_dec(phdr[i].p_filesz);
        vga_writestring(" bytes\n  Memory size: ");
        print_dec(phdr[i].p_memsz);
        vga_writestring(" bytes\n  Flags: ");
        if (phdr[i].p_flags & PF_R) vga_writestring("R");
        if (phdr[i].p_flags & PF_W) vga_writestring("W");
        if (phdr[i].p_flags & PF_X) vga_writestring("X");
        vga_writestring("\n");
        
        /* Calculate page-aligned region */
        uint64_t vaddr_start = align_down(phdr[i].p_vaddr);
        uint64_t vaddr_end = align_up(phdr[i].p_vaddr + phdr[i].p_memsz);
        uint64_t num_pages = (vaddr_end - vaddr_start) / PAGE_SIZE;
        
        vga_writestring("  Mapping ");
        print_dec(num_pages);
        vga_writestring(" pages starting at 0x");
        print_hex(vaddr_start);
        vga_writestring("\n");
        
        /* Map pages for this segment */
        for (uint64_t page = 0; page < num_pages; page++) {
            uint64_t vaddr = vaddr_start + (page * PAGE_SIZE);
            
            /* Allocate physical frame */
            uint64_t paddr = pmm_alloc_frame();
            if (paddr == 0) {
                vga_writestring("ELF: ERROR - Failed to allocate frame for segment\n");
                return ELF_ERROR_NO_MEMORY;
            }
            
            /* Determine page flags based on segment flags */
            uint64_t page_flags = PAGE_PRESENT | PAGE_USER;
            if (phdr[i].p_flags & PF_W) {
                page_flags |= PAGE_WRITABLE;
            }
            
            /* Map the page */
            if (paging_map_page(vaddr, paddr, page_flags) != 0) {
                vga_writestring("ELF: ERROR - Failed to map page at 0x");
                print_hex(vaddr);
                vga_writestring("\n");
                pmm_free_frame(paddr);
                return ELF_ERROR_NO_MEMORY;
            }
        }
        
        /* Copy segment data from file */
        const uint8_t *src = (const uint8_t*)elf_data + phdr[i].p_offset;
        uint8_t *dest = (uint8_t*)phdr[i].p_vaddr;
        
        vga_writestring("  Copying ");
        print_dec(phdr[i].p_filesz);
        vga_writestring(" bytes from file to 0x");
        print_hex((uint64_t)dest);
        vga_writestring("\n");
        
        /* Copy file data */
        memcpy(dest, src, phdr[i].p_filesz);
        
        /* Zero out BSS section (memsz > filesz) */
        if (phdr[i].p_memsz > phdr[i].p_filesz) {
            uint64_t bss_size = phdr[i].p_memsz - phdr[i].p_filesz;
            vga_writestring("  Zeroing BSS: ");
            print_dec(bss_size);
            vga_writestring(" bytes\n");
            memset(dest + phdr[i].p_filesz, 0, bss_size);
        }
        
        vga_writestring("  Segment loaded successfully\n");
    }
    
    /* Set entry point */
    if (entry_point) {
        *entry_point = ehdr->e_entry;
    }
    
    vga_writestring("ELF: === Load completed successfully ===\n");
    return ELF_SUCCESS;
}

/* Allocate and setup user stack */
uint64_t elf_setup_user_stack(void) {
    /* User stack will be at high address (e.g., 0x7FFFFFFFFFFF) */
    /* We'll use a simpler address for now: 0x800000 (8MB) */
    const uint64_t stack_top = 0x800000;
    const uint64_t stack_size = 0x10000;  /* 64KB stack */
    const uint64_t stack_bottom = stack_top - stack_size;
    
    vga_writestring("ELF: Setting up user stack\n");
    vga_writestring("  Stack bottom: 0x");
    print_hex(stack_bottom);
    vga_writestring("\n  Stack top: 0x");
    print_hex(stack_top);
    vga_writestring("\n");
    
    /* Allocate and map stack pages */
    uint64_t num_pages = stack_size / PAGE_SIZE;
    for (uint64_t i = 0; i < num_pages; i++) {
        uint64_t vaddr = stack_bottom + (i * PAGE_SIZE);
        uint64_t paddr = pmm_alloc_frame();
        
        if (paddr == 0) {
            vga_writestring("ELF: ERROR - Failed to allocate stack frame\n");
            return 0;
        }
        
        if (paging_map_page(vaddr, paddr, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER) != 0) {
            vga_writestring("ELF: ERROR - Failed to map stack page\n");
            pmm_free_frame(paddr);
            return 0;
        }
    }
    
    /* Zero out stack */
    memset((void*)stack_bottom, 0, stack_size);
    
    vga_writestring("ELF: User stack setup complete\n");
    return stack_top;
}