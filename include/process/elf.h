/* NumOS — elf.h — ELF64 user-program loader (Phase 11)
 *
 * Loads a static ELF64 executable into a cloned user address space with
 * strict validation: magic, class (64-bit), endianness (little), type
 * (ET_EXEC), machine (EM_X86_64), program-header bounds, segment ranges
 * inside the user region, filesz <= memsz, alignment consistency, and
 * per-segment permissions (PF_X/PF_W/PF_R -> page flags incl. NX).
 */
#ifndef NUMOS_PROCESS_ELF_H
#define NUMOS_PROCESS_ELF_H

#include "libs/types.h"

#define ELF_LOAD_OK        0
#define ELF_EINVAL        -1
#define ELF_ENOMEM        -2
#define ELF_ERANGE        -3
#define ELF_EPERM         -4

int elf_load(uint64_t* pml4, const uint8_t* image, uint64_t size, uint64_t* out_entry);
const char* elf_error_string(int rc);

#endif
