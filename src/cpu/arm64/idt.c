#include "cpu/idt.h"
#include "kernel/arm64.h"

extern void arm64_exception_vectors(void);

void idt_init(void) {
    arm64_set_vector_base((uint64_t)(uintptr_t)&arm64_exception_vectors);
}

void idt_set_gate(int num, uint64_t handler, uint16_t selector,
                  uint8_t type_attr) {
    (void)num;
    (void)handler;
    (void)selector;
    (void)type_attr;
}

void idt_flush(uint64_t idt_ptr_addr) {
    (void)idt_ptr_addr;
}

void exception_handler(uint32_t exception_num, uint64_t error_code) {
    (void)exception_num;
    (void)error_code;
}

void irq_handler(uint32_t irq_num) {
    (void)irq_num;
}
