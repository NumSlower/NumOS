/* NumOS — src/kernel/multiboot2.c — Multiboot2 info tag parser implementation */
#include "kernel/multiboot2.h"

static struct mb2_tag_mmap* mmap_tag = NULL;
static void* rsdp_ptr = NULL;
static const char* cmdline = NULL;
static struct mb2_tag_basic_meminfo* meminfo_tag = NULL;

void mb2_parse(uint64_t mb2_info_addr) {
    if (!mb2_info_addr) return;
    
    uint32_t total_size = *(uint32_t*)mb2_info_addr;
    uint32_t offset = 8;
    
    while (offset < total_size) {
        struct mb2_tag* tag = (struct mb2_tag*)(mb2_info_addr + offset);
        if (tag->type == MB2_TAG_END) {
            break;
        }
        
        switch (tag->type) {
            case MB2_TAG_CMDLINE:
                cmdline = ((struct mb2_tag_string*)tag)->string;
                break;
            case MB2_TAG_BASIC_MEMINFO:
                meminfo_tag = (struct mb2_tag_basic_meminfo*)tag;
                break;
            case MB2_TAG_MMAP:
                mmap_tag = (struct mb2_tag_mmap*)tag;
                break;
            case MB2_TAG_ACPI_OLD:
                if (!rsdp_ptr) {
                    rsdp_ptr = ((struct mb2_tag_old_acpi*)tag)->rsdp;
                }
                break;
            case MB2_TAG_ACPI_NEW:
                rsdp_ptr = ((struct mb2_tag_new_acpi*)tag)->rsdp;
                break;
        }
        
        offset += (tag->size + 7) & ~7;
    }
}

struct mb2_tag_mmap* mb2_get_mmap(void) {
    return mmap_tag;
}

void* mb2_get_rsdp(void) {
    return rsdp_ptr;
}

const char* mb2_get_cmdline(void) {
    return cmdline;
}

uint64_t mb2_get_total_memory(void) {
    if (!mmap_tag) return 0;
    
    uint64_t total = 0;
    uint32_t num_entries = (mmap_tag->size - sizeof(struct mb2_tag_mmap)) / mmap_tag->entry_size;
    
    for (uint32_t i = 0; i < num_entries; i++) {
        struct mb2_mmap_entry* entry = (struct mb2_mmap_entry*)((uint8_t*)mmap_tag->entries + i * mmap_tag->entry_size);
        if (entry->type == MB2_MMAP_AVAILABLE) {
            total += entry->length;
        }
    }
    
    return total;
}
