/* NumOS — userprog.h — Ring 3 user-process creation
 *
 * User address-space layout (per process, in its own cloned page tables):
 *   0x08000000  code + rodata + data (loaded from the embedded program)
 *   0x08080000  user heap start (grows up via SYS_SBRK)
 *   0x0FF00000  user stack top (16 KB below; grows down)
 * Kernel identity mappings are cloned as supervisor pages, so Ring 3 code
 * cannot touch kernel memory; the CPU enforces isolation.
 */
#ifndef NUMOS_PROCESS_USERPROG_H
#define NUMOS_PROCESS_USERPROG_H

#include "libs/types.h"
#include "process/process.h"

#define USER_VA_BASE  0x08000000ULL
#define USER_VA_LIMIT 0x10000000ULL
#define USER_CODE_BASE  0x08000000ULL
#define USER_HEAP_BASE  0x08080000ULL
#define USER_HEAP_LIMIT 0x0F000000ULL
#define USER_STACK_TOP  0x0FF00000ULL
#define USER_STACK_PAGES 8

extern const uint8_t* user_program_start;
extern const uint8_t* user_program_end;

/* Filled by the linker-generated blob symbols (Makefile embeds
 * build/user_init.elf — the full ELF64 image). */
extern const uint8_t _binary_user_init_elf_start[];
extern const uint8_t _binary_user_init_elf_end[];

bool userprog_init(void);
struct process* user_process_create(const char* name);
struct process* user_process_create_image(const char* name, const uint8_t* image, uint64_t size);
struct process* user_process_exec(const char* name, const uint8_t* image, uint64_t size,
                                  const char* const* argv, const char* const* envp,
                                  uint32_t ppid, uint8_t tty_index);
struct process* user_shell_spawn(uint8_t tty_index);
int user_fd_alloc(struct process* proc, void* file);
void user_fd_free(struct process* proc, int fd);
void* user_fd_get(struct process* proc, int fd);

#endif
