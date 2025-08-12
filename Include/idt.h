#ifndef IDT_H
#define IDT_H

#include <stdint.h>

/* IDT Entry Structure */
struct idt_entry {
    uint16_t base_low;      /* Lower 16 bits of handler address */
    uint16_t sel;           /* Kernel segment selector */
    uint8_t  ist;           /* Interrupt Stack Table offset */
    uint8_t  flags;         /* Type and attributes */
    uint16_t base_mid;      /* Middle 16 bits of handler address */
    uint32_t base_high;     /* Higher 32 bits of handler address */
    uint32_t reserved;      /* Reserved */
} __attribute__((packed));

/* IDT Pointer Structure */
struct idt_ptr {
    uint16_t limit;         /* Size of IDT */
    uint64_t base;          /* Address of IDT */
} __attribute__((packed));

/* Registers structure for interrupt handlers */
struct registers {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_no, err_code;
    uint64_t rip, cs, rflags, rsp, ss;
};

/* IDT flags */
#define IDT_FLAG_PRESENT    0x80
#define IDT_FLAG_RING0      0x00
#define IDT_FLAG_RING1      0x20
#define IDT_FLAG_RING2      0x40
#define IDT_FLAG_RING3      0x60
#define IDT_FLAG_GATE32     0x0E
#define IDT_FLAG_GATE16     0x06

/* Interrupt numbers */
#define IRQ_TIMER           32
#define IRQ_KEYBOARD        33
#define IRQ_CASCADE         34
#define IRQ_COM2            35
#define IRQ_COM1            36
#define IRQ_LPT2            37
#define IRQ_FLOPPY          38
#define IRQ_LPT1            39
#define IRQ_CMOS            40
#define IRQ_FREE1           41
#define IRQ_FREE2           42
#define IRQ_FREE3           43
#define IRQ_PS2MOUSE        44
#define IRQ_FPU             45
#define IRQ_PRIMARY_ATA     46
#define IRQ_SECONDARY_ATA   47

/* Function prototypes */
void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void irq_enable(uint8_t irq);
void irq_disable(uint8_t irq);

/* Interrupt handler type */
typedef void (*interrupt_handler_t)(struct registers *regs);

/* Register interrupt handler */
void register_interrupt_handler(uint8_t n, interrupt_handler_t handler);

/* Assembly interrupt handlers */
extern void isr0(void);   /* Division by zero */
extern void isr1(void);   /* Debug */
extern void isr2(void);   /* Non Maskable Interrupt */
extern void isr3(void);   /* Breakpoint */
extern void isr4(void);   /* Into Detected Overflow */
extern void isr5(void);   /* Out of Bounds */
extern void isr6(void);   /* Invalid Opcode */
extern void isr7(void);   /* No Coprocessor */
extern void isr8(void);   /* Double Fault */
extern void isr9(void);   /* Coprocessor Segment Overrun */
extern void isr10(void);  /* Bad TSS */
extern void isr11(void);  /* Segment Not Present */
extern void isr12(void);  /* Stack Fault */
extern void isr13(void);  /* General Protection Fault */
extern void isr14(void);  /* Page Fault */
extern void isr15(void);  /* Reserved */
extern void isr16(void);  /* Floating Point Exception */
extern void isr17(void);  /* Alignment Check */
extern void isr18(void);  /* Machine Check */
extern void isr19(void);  /* SIMD Floating Point Exception */
extern void isr20(void);  /* Virtualization Exception */
extern void isr21(void);  /* Control Protection Exception */
extern void isr22(void);  /* Reserved */
extern void isr23(void);  /* Reserved */
extern void isr24(void);  /* Reserved */
extern void isr25(void);  /* Reserved */
extern void isr26(void);  /* Reserved */
extern void isr27(void);  /* Reserved */
extern void isr28(void);  /* Reserved */
extern void isr29(void);  /* Reserved */
extern void isr30(void);  /* Security Exception */
extern void isr31(void);  /* Reserved */

/* IRQ handlers */
extern void irq0(void);   /* Timer */
extern void irq1(void);   /* Keyboard */
extern void irq2(void);   /* Cascade */
extern void irq3(void);   /* COM2 */
extern void irq4(void);   /* COM1 */
extern void irq5(void);   /* LPT2 */
extern void irq6(void);   /* Floppy */
extern void irq7(void);   /* LPT1 */
extern void irq8(void);   /* CMOS */
extern void irq9(void);   /* Free */
extern void irq10(void);  /* Free */
extern void irq11(void);  /* Free */
extern void irq12(void);  /* PS2 Mouse */
extern void irq13(void);  /* FPU */
extern void irq14(void);  /* Primary ATA */
extern void irq15(void);  /* Secondary ATA */

/* Assembly function to load IDT */
extern void idt_flush(uint64_t idt_ptr);

#endif /* IDT_H */