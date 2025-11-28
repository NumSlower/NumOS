#ifndef IDT_H
#define IDT_H

#include "lib/base.h"

/* IDT Entry Structure */
struct idt_entry {
    uint16_t offset_low;    // Lower 16 bits of handler address
    uint16_t selector;      // Segment selector (kernel code segment)
    uint8_t ist;            // Interrupt Stack Table offset (0 for now)
    uint8_t type_attr;      // Type and attributes
    uint16_t offset_mid;    // Middle 16 bits of handler address
    uint32_t offset_high;   // Upper 32 bits of handler address
    uint32_t reserved;      // Reserved (must be zero)
} __attribute__((packed));

/* IDT Pointer Structure */
struct idt_ptr {
    uint16_t limit;         // Size of IDT - 1
    uint64_t base;          // Address of first IDT entry
} __attribute__((packed));

/* IDT Type and Attribute Flags */
#define IDT_TYPE_TASK_GATE     0x05
#define IDT_TYPE_INTERRUPT     0x0E  // Interrupt gate
#define IDT_TYPE_TRAP          0x0F  // Trap gate

#define IDT_ATTR_PRESENT       0x80  // Present bit
#define IDT_ATTR_DPL0          0x00  // Ring 0 (kernel)
#define IDT_ATTR_DPL1          0x20  // Ring 1
#define IDT_ATTR_DPL2          0x40  // Ring 2
#define IDT_ATTR_DPL3          0x60  // Ring 3 (user)

/* Exception numbers */
#define EXCEPTION_DIVIDE_ERROR          0
#define EXCEPTION_DEBUG                 1
#define EXCEPTION_NMI                   2
#define EXCEPTION_BREAKPOINT            3
#define EXCEPTION_OVERFLOW              4
#define EXCEPTION_BOUND_RANGE           5
#define EXCEPTION_INVALID_OPCODE        6
#define EXCEPTION_DEVICE_NOT_AVAILABLE  7
#define EXCEPTION_DOUBLE_FAULT          8
#define EXCEPTION_COPROCESSOR_SEGMENT   9
#define EXCEPTION_INVALID_TSS           10
#define EXCEPTION_SEGMENT_NOT_PRESENT   11
#define EXCEPTION_STACK_SEGMENT         12
#define EXCEPTION_GENERAL_PROTECTION    13
#define EXCEPTION_PAGE_FAULT            14
#define EXCEPTION_RESERVED              15
#define EXCEPTION_X87_FPU               16
#define EXCEPTION_ALIGNMENT_CHECK       17
#define EXCEPTION_MACHINE_CHECK         18
#define EXCEPTION_SIMD_FP               19
#define EXCEPTION_VIRTUALIZATION        20
#define EXCEPTION_CONTROL_PROTECTION    21

/* IRQ numbers (hardware interrupts) */
#define IRQ_TIMER                       32
#define IRQ_KEYBOARD                    33
#define IRQ_CASCADE                     34
#define IRQ_COM2                        35
#define IRQ_COM1                        36
#define IRQ_LPT2                        37
#define IRQ_FLOPPY                      38
#define IRQ_LPT1                        39
#define IRQ_RTC                         40
#define IRQ_FREE1                       41
#define IRQ_FREE2                       42
#define IRQ_FREE3                       43
#define IRQ_MOUSE                       44
#define IRQ_FPU                         45
#define IRQ_PRIMARY_ATA                 46
#define IRQ_SECONDARY_ATA               47

/* Function prototypes */
void idt_init(void);
void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t type_attr);
void idt_flush(uint64_t idt_ptr_addr);

/* Exception handlers */
void exception_handler(uint32_t exception_num, uint64_t error_code);

/* IRQ handlers */
void irq_handler(uint32_t irq_num);

/* Assembly interrupt handlers */
extern void isr0(void);   // Division by zero
extern void isr1(void);   // Debug
extern void isr2(void);   // NMI
extern void isr3(void);   // Breakpoint
extern void isr4(void);   // Overflow
extern void isr5(void);   // Bound range exceeded
extern void isr6(void);   // Invalid opcode
extern void isr7(void);   // Device not available
extern void isr8(void);   // Double fault
extern void isr9(void);   // Coprocessor segment overrun
extern void isr10(void);  // Invalid TSS
extern void isr11(void);  // Segment not present
extern void isr12(void);  // Stack segment fault
extern void isr13(void);  // General protection fault
extern void isr14(void);  // Page fault
extern void isr15(void);  // Reserved
extern void isr16(void);  // x87 FPU error
extern void isr17(void);  // Alignment check
extern void isr18(void);  // Machine check
extern void isr19(void);  // SIMD floating point
extern void isr20(void);  // Virtualization
extern void isr21(void);  // Control protection

extern void irq0(void);   // Timer
extern void irq1(void);   // Keyboard
extern void irq2(void);   // Cascade
extern void irq3(void);   // COM2
extern void irq4(void);   // COM1
extern void irq5(void);   // LPT2
extern void irq6(void);   // Floppy
extern void irq7(void);   // LPT1
extern void irq8(void);   // RTC
extern void irq9(void);   // Free
extern void irq10(void);  // Free
extern void irq11(void);  // Free
extern void irq12(void);  // Mouse
extern void irq13(void);  // FPU
extern void irq14(void);  // Primary ATA
extern void irq15(void);  // Secondary ATA

#endif /* IDT_H */