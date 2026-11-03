/* NumOS — include/kernel/multiboot2.h — Multiboot2 info tag parser header */
#ifndef NUMOS_KERNEL_MULTIBOOT2_H
#define NUMOS_KERNEL_MULTIBOOT2_H

#include "kernel/types.h"

#define MB2_TAG_END 0
#define MB2_TAG_CMDLINE 1
#define MB2_TAG_BOOTLOADER 2
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_MMAP 6
#define MB2_TAG_FRAMEBUFFER 8
#define MB2_TAG_ACPI_OLD 14
#define MB2_TAG_ACPI_NEW 15

#define MB2_MMAP_AVAILABLE 1
#define MB2_MMAP_RESERVED 2
#define MB2_MMAP_ACPI_RECLAIMABLE 3
#define MB2_MMAP_NVS 4
#define MB2_MMAP_BAD 5

struct mb2_tag {
    uint32_t type;
    uint32_t size;
} __attribute__((packed));

struct mb2_mmap_entry {
    uint64_t base_addr;
    uint64_t length;
    uint32_t type;
    uint32_t reserved;
} __attribute__((packed));

struct mb2_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct mb2_mmap_entry entries[0];
} __attribute__((packed));

struct mb2_tag_string {
    uint32_t type;
    uint32_t size;
    char string[0];
} __attribute__((packed));

struct mb2_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
} __attribute__((packed));

struct mb2_tag_old_acpi {
    uint32_t type;
    uint32_t size;
    uint8_t rsdp[0];
} __attribute__((packed));

struct mb2_tag_new_acpi {
    uint32_t type;
    uint32_t size;
    uint8_t rsdp[0];
} __attribute__((packed));

void mb2_parse(uint64_t mb2_info_addr);
struct mb2_tag_mmap* mb2_get_mmap(void);
void* mb2_get_rsdp(void);
const char* mb2_get_cmdline(void);
uint64_t mb2_get_total_memory(void);

#endif
