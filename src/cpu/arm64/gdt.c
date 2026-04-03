#include "cpu/gdt.h"

void gdt_init(void) {
}

void gdt_set_gate(int num, uint32_t base, uint32_t limit,
                  uint8_t access, uint8_t gran) {
    (void)num;
    (void)base;
    (void)limit;
    (void)access;
    (void)gran;
}

void gdt_print_info(void) {
}
