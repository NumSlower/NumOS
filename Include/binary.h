#ifndef BINARY_H
#define BINARY_H

#include <stdint.h>
#include <stddef.h>

/* NumOS Binary Format Header */
struct numos_binary_header {
    uint32_t magic;         // Magic number: "NumO" (0x4E756D4F)
    uint32_t version;       // Binary format version
    uint64_t entry_point;   // Entry point offset from base
    uint32_t code_size;     // Size of code section
    uint32_t data_size;     // Size of data section  
    uint32_t bss_size;      // Size of BSS section (uninitialized data)
    uint32_t flags;         // Binary flags
    uint8_t reserved[32];   // Reserved for future use
} __attribute__((packed));

/* Binary flags */
#define BINARY_FLAG_EXECUTABLE  0x01
#define BINARY_FLAG_RELOCATABLE 0x02
#define BINARY_FLAG_DEBUG       0x04

/* Binary constraints */
#define MAX_BINARY_SIZE (16 * 1024 * 1024)  // 16MB max program size
#define MAX_BINARY_NAME 256

/* Loaded program structure */
struct loaded_program {
    char name[MAX_BINARY_NAME];
    uint64_t base_address;
    size_t size;
    size_t pages;
    uint64_t entry_point;
    struct loaded_program *next;
};

/* Binary entry point function type */
typedef int (*binary_entry_point_t)(void);

/* Error codes */
#define BINARY_SUCCESS          0
#define BINARY_ERROR_GENERIC    -1
#define BINARY_ERROR_NOT_FOUND  -2
#define BINARY_ERROR_NO_MEMORY  -3
#define BINARY_ERROR_INVALID    -4
#define BINARY_ERROR_IO         -5
#define BINARY_ERROR_FORMAT     -6

/* Function prototypes */
int binary_load(const char *filename);
int binary_execute(const char *filename);
int binary_unload(const char *filename);
struct loaded_program* binary_find_program(const char *filename);
void binary_list_programs(void);
void binary_cleanup(void);

/* Binary creation utilities */
int binary_create_simple(const char *filename, const void *code, size_t code_size, uint64_t entry_offset);

size_t strncpy(char *dest, const char *src, size_t n);

#endif /* BINARY_H */