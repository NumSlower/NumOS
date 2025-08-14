#include "binary.h"
#include "kernel.h"
#include "vga.h"
#include "heap.h"
#include "paging.h"
#include "fat32.h"

/* Binary format definitions */
#define NUMOS_MAGIC 0x4E756D4F  // "NumO" in little endian
#define NUMOS_VERSION 1

size_t strncpy(char *dest, const char *src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for ( ; i < n; i++) {
        dest[i] = '\0';
    }
    return i;
}

/* Global loaded programs list */
static struct loaded_program *g_loaded_programs = NULL;

/* Function implementations */
int binary_load(const char *filename) {
    if (!filename) {
        vga_writestring("Error: No filename provided\n");
        return BINARY_ERROR_INVALID;
    }
    
    vga_writestring("Loading binary: ");
    vga_writestring(filename);
    vga_putchar('\n');
    
    /* Open file */
    struct fat32_file *file = fat32_fopen(filename, "r");
    if (!file) {
        vga_writestring("Error: Cannot open file\n");
        return BINARY_ERROR_NOT_FOUND;
    }
    
    /* Read header */
    struct numos_binary_header header;
    size_t read_size = fat32_fread(&header, sizeof(header), 1, file);
    if (read_size != 1) {
        vga_writestring("Error: Cannot read binary header\n");
        fat32_fclose(file);
        return BINARY_ERROR_IO;
    }
    
    /* Validate header */
    if (header.magic != NUMOS_MAGIC) {
        vga_writestring("Error: Invalid binary format (magic mismatch)\n");
        fat32_fclose(file);
        return BINARY_ERROR_FORMAT;
    }
    
    if (header.version != NUMOS_VERSION) {
        vga_writestring("Error: Unsupported binary version\n");
        fat32_fclose(file);
        return BINARY_ERROR_FORMAT;
    }
    
    if (header.code_size == 0 || header.code_size > MAX_BINARY_SIZE) {
        vga_writestring("Error: Invalid code size\n");
        fat32_fclose(file);
        return BINARY_ERROR_FORMAT;
    }
    
    vga_writestring("Binary header valid:\n");
    vga_writestring("  Entry point: ");
    print_hex(header.entry_point);
    vga_writestring("\n  Code size: ");
    print_dec(header.code_size);
    vga_writestring("\n  Data size: ");
    print_dec(header.data_size);
    vga_writestring("\n  BSS size: ");
    print_dec(header.bss_size);
    vga_putchar('\n');
    
    /* Calculate total memory needed */
    size_t total_size = header.code_size + header.data_size + header.bss_size;
    size_t pages_needed = (total_size + PAGE_SIZE - 1) / PAGE_SIZE;
    
    /* Allocate virtual memory for the program */
    void *program_base = vmm_alloc_pages(pages_needed, PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER);
    if (!program_base) {
        vga_writestring("Error: Cannot allocate memory for program\n");
        fat32_fclose(file);
        return BINARY_ERROR_NO_MEMORY;
    }
    
    vga_writestring("Allocated ");
    print_dec(pages_needed);
    vga_writestring(" pages at ");
    print_hex((uint64_t)program_base);
    vga_putchar('\n');
    
    /* Load code section */
    char *code_ptr = (char*)program_base;
    size_t bytes_read = fat32_fread(code_ptr, 1, header.code_size, file);
    if (bytes_read != header.code_size) {
        vga_writestring("Error: Cannot read code section\n");
        vmm_free_pages(program_base, pages_needed);
        fat32_fclose(file);
        return BINARY_ERROR_IO;
    }
    
    /* Load data section */
    char *data_ptr = code_ptr + header.code_size;
    if (header.data_size > 0) {
        bytes_read = fat32_fread(data_ptr, 1, header.data_size, file);
        if (bytes_read != header.data_size) {
            vga_writestring("Error: Cannot read data section\n");
            vmm_free_pages(program_base, pages_needed);
            fat32_fclose(file);
            return BINARY_ERROR_IO;
        }
    }
    
    /* Initialize BSS section (zero-filled) */
    char *bss_ptr = data_ptr + header.data_size;
    if (header.bss_size > 0) {
        memset(bss_ptr, 0, header.bss_size);
    }
    
    fat32_fclose(file);
    
    /* Create loaded program structure */
    struct loaded_program *program = (struct loaded_program*)kmalloc(sizeof(struct loaded_program));
    if (!program) {
        vga_writestring("Error: Cannot allocate program structure\n");
        vmm_free_pages(program_base, pages_needed);
        return BINARY_ERROR_NO_MEMORY;
    }
    
    /* Initialize program structure */
    strcpy(program->name, filename);
    program->base_address = (uint64_t)program_base;
    program->size = total_size;
    program->pages = pages_needed;
    program->entry_point = header.entry_point + (uint64_t)program_base;
    program->next = g_loaded_programs;
    g_loaded_programs = program;
    
    vga_writestring("Binary loaded successfully!\n");
    vga_writestring("  Base address: ");
    print_hex(program->base_address);
    vga_writestring("\n  Entry point: ");
    print_hex(program->entry_point);
    vga_putchar('\n');
    
    return BINARY_SUCCESS;
}

int binary_execute(const char *filename) {
    if (!filename) {
        return BINARY_ERROR_INVALID;
    }
    
    /* Find loaded program */
    struct loaded_program *program = binary_find_program(filename);
    if (!program) {
        vga_writestring("Program not loaded: ");
        vga_writestring(filename);
        vga_putchar('\n');
        return BINARY_ERROR_NOT_FOUND;
    }
    
    vga_writestring("Executing: ");
    vga_writestring(filename);
    vga_writestring(" at ");
    print_hex(program->entry_point);
    vga_putchar('\n');
    
    /* Create function pointer and execute */
    binary_entry_point_t entry = (binary_entry_point_t)program->entry_point;
    
    /* Note: In a real OS, you'd switch to user mode, set up proper
       stack, handle system calls, etc. This is simplified for demo. */
    int result = entry();
    
    vga_writestring("Program exited with code: ");
    print_dec(result);
    vga_putchar('\n');
    
    return BINARY_SUCCESS;
}

int binary_unload(const char *filename) {
    if (!filename) {
        return BINARY_ERROR_INVALID;
    }
    
    /* Find and remove from list */
    struct loaded_program **current = &g_loaded_programs;
    while (*current) {
        if (strcmp((*current)->name, filename) == 0) {
            struct loaded_program *program = *current;
            *current = program->next;
            
            /* Free memory */
            vmm_free_pages((void*)program->base_address, program->pages);
            kfree(program);
            
            vga_writestring("Unloaded program: ");
            vga_writestring(filename);
            vga_putchar('\n');
            
            return BINARY_SUCCESS;
        }
        current = &((*current)->next);
    }
    
    return BINARY_ERROR_NOT_FOUND;
}

struct loaded_program* binary_find_program(const char *filename) {
    struct loaded_program *current = g_loaded_programs;
    while (current) {
        if (strcmp(current->name, filename) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

void binary_list_programs(void) {
    vga_writestring("Loaded Programs:\n");
    vga_writestring("Name            Base Address   Size      Entry Point\n");
    vga_writestring("--------------- -------------- --------- --------------\n");
    
    struct loaded_program *current = g_loaded_programs;
    int count = 0;
    
    while (current) {
        /* Print name (truncated to 15 chars) */
        char name_buf[16];
        strncpy(name_buf, current->name, 15);
        name_buf[15] = '\0';
        vga_writestring(name_buf);
        
        /* Pad name to 15 characters */
        int name_len = strlen(name_buf);
        for (int i = name_len; i < 16; i++) {
            vga_putchar(' ');
        }
        
        /* Print base address */
        print_hex(current->base_address);
        vga_writestring("  ");
        
        /* Print size */
        print_dec(current->size);
        vga_writestring("   ");
        
        /* Print entry point */
        print_hex(current->entry_point);
        vga_putchar('\n');
        
        current = current->next;
        count++;
    }
    
    if (count == 0) {
        vga_writestring("No programs loaded.\n");
    } else {
        vga_writestring("Total programs: ");
        print_dec(count);
        vga_putchar('\n');
    }
}

int binary_create_simple(const char *filename, const void *code, size_t code_size, uint64_t entry_offset) {
    if (!filename || !code || code_size == 0) {
        return BINARY_ERROR_INVALID;
    }
    
    /* Create binary header */
    struct numos_binary_header header;
    header.magic = NUMOS_MAGIC;
    header.version = NUMOS_VERSION;
    header.entry_point = entry_offset;
    header.code_size = code_size;
    header.data_size = 0;
    header.bss_size = 0;
    header.flags = 0;
    memset(header.reserved, 0, sizeof(header.reserved));
    
    /* Open file for writing */
    struct fat32_file *file = fat32_fopen(filename, "w");
    if (!file) {
        vga_writestring("Error: Cannot create binary file\n");
        return BINARY_ERROR_IO;
    }
    
    /* Write header */
    size_t written = fat32_fwrite(&header, sizeof(header), 1, file);
    if (written != 1) {
        vga_writestring("Error: Cannot write binary header\n");
        fat32_fclose(file);
        return BINARY_ERROR_IO;
    }
    
    /* Write code */
    written = fat32_fwrite(code, 1, code_size, file);
    if (written != code_size) {
        vga_writestring("Error: Cannot write code section\n");
        fat32_fclose(file);
        return BINARY_ERROR_IO;
    }
    
    fat32_fclose(file);
    
    vga_writestring("Created binary file: ");
    vga_writestring(filename);
    vga_putchar('\n');
    
    return BINARY_SUCCESS;
}

void binary_cleanup(void) {
    /* Unload all programs */
    while (g_loaded_programs) {
        struct loaded_program *next = g_loaded_programs->next;
        vmm_free_pages((void*)g_loaded_programs->base_address, g_loaded_programs->pages);
        kfree(g_loaded_programs);
        g_loaded_programs = next;
    }
    
    vga_writestring("All loaded programs cleaned up\n");
}