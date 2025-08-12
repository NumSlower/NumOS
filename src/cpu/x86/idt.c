#include "idt.h"
#include "gdt.h"
#include "kernel.h"
#include "vga.h"

/* IDT entries array */
static struct idt_entry idt_entries[256];
static struct idt_ptr idt_pointer;

/* Interrupt handlers array */
static interrupt_handler_t interrupt_handlers[256];

/* Exception messages */
static const char* exception_messages[] = {
    "Division By Zero",
    "Debug",
    "Non Maskable Interrupt",
    "Breakpoint",
    "Into Detected Overflow",
    "Out of Bounds",
    "Invalid Opcode",
    "No Coprocessor",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Bad TSS",
    "Segment Not Present",
    "Stack Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "Floating Point Exception",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point Exception",
    "Virtualization Exception",
    "Control Protection Exception",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Reserved",
    "Security Exception",
    "Reserved"
};

void idt_init(void) {
    idt_pointer.limit = sizeof(idt_entries) - 1;
    idt_pointer.base = (uint64_t)&idt_entries;

    /* Clear the IDT */
    memset(&idt_entries, 0, sizeof(idt_entries));
    memset(&interrupt_handlers, 0, sizeof(interrupt_handlers));

    /* Remap the IRQ table */
    outb(0x20, 0x11);  /* Initialize PIC1 */
    outb(0xA0, 0x11);  /* Initialize PIC2 */
    outb(0x21, 0x20);  /* PIC1 starts at 32 */
    outb(0xA1, 0x28);  /* PIC2 starts at 40 */
    outb(0x21, 0x04);  /* PIC1 is master */
    outb(0xA1, 0x02);  /* PIC2 is slave */
    outb(0x21, 0x01);  /* 8086/88 mode for PIC1 */
    outb(0xA1, 0x01);  /* 8086/88 mode for PIC2 */
    outb(0x21, 0x0);   /* Enable all IRQs on PIC1 */
    outb(0xA1, 0x0);   /* Enable all IRQs on PIC2 */

    /* Install ISR handlers */
    idt_set_gate(0, (uint64_t)isr0, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(1, (uint64_t)isr1, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(2, (uint64_t)isr2, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(3, (uint64_t)isr3, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(4, (uint64_t)isr4, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(5, (uint64_t)isr5, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(6, (uint64_t)isr6, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(7, (uint64_t)isr7, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(8, (uint64_t)isr8, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(9, (uint64_t)isr9, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(22, (uint64_t)isr22, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(23, (uint64_t)isr23, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(24, (uint64_t)isr24, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(25, (uint64_t)isr25, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(26, (uint64_t)isr26, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(27, (uint64_t)isr27, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(28, (uint64_t)isr28, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(29, (uint64_t)isr29, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(30, (uint64_t)isr30, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(31, (uint64_t)isr31, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);

    /* Install IRQ handlers */
    idt_set_gate(32, (uint64_t)irq0, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(33, (uint64_t)irq1, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(34, (uint64_t)irq2, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(35, (uint64_t)irq3, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(36, (uint64_t)irq4, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(37, (uint64_t)irq5, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(38, (uint64_t)irq6, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(39, (uint64_t)irq7, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(40, (uint64_t)irq8, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(41, (uint64_t)irq9, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, IDT_FLAG_PRESENT | IDT_FLAG_RING0 | IDT_FLAG_GATE32);

    /* Load the IDT */
    idt_flush((uint64_t)&idt_pointer);

    /* Enable interrupts */
    __asm__ volatile("sti");
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt_entries[num].base_low = base & 0xFFFF;
    idt_entries[num].base_mid = (base >> 16) & 0xFFFF;
    idt_entries[num].base_high = (base >> 32) & 0xFFFFFFFF;
    
    idt_entries[num].sel = sel;
    idt_entries[num].ist = 0;
    idt_entries[num].flags = flags;
    idt_entries[num].reserved = 0;
}

void register_interrupt_handler(uint8_t n, interrupt_handler_t handler) {
    interrupt_handlers[n] = handler;
}

void irq_enable(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    value = inb(port) & ~(1 << irq);
    outb(port, value);
}

void irq_disable(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = 0x21;
    } else {
        port = 0xA1;
        irq -= 8;
    }
    value = inb(port) | (1 << irq);
    outb(port, value);
}

/* Common interrupt handler */
void interrupt_handler(struct registers *regs) {
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    } else {
        /* Unhandled interrupt */
        if (regs->int_no < 32) {
            /* CPU exception */
            vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
            vga_writestring("EXCEPTION: ");
            vga_writestring(exception_messages[regs->int_no]);
            vga_writestring("\n");
            
            if (regs->int_no == 14) {
                /* Page fault - get the faulting address */
                uint64_t faulting_address;
                __asm__ volatile("mov %%cr2, %0" : "=r" (faulting_address));
                vga_writestring("Faulting address: 0x");
                /* Print hex address - simplified version */
                for (int i = 60; i >= 0; i -= 4) {
                    char hex = (faulting_address >> i) & 0xF;
                    vga_putchar(hex < 10 ? '0' + hex : 'A' + hex - 10);
                }
                vga_writestring("\n");
            }
            
            panic("Unhandled exception");
        }
    }
}

/* IRQ handler */
void irq_handler(struct registers *regs) {
    /* Send EOI to PICs */
    if (regs->int_no >= 40) {
        outb(0xA0, 0x20);  /* Send EOI to slave PIC */
    }
    outb(0x20, 0x20);      /* Send EOI to master PIC */

    /* Call registered handler if exists */
    if (interrupt_handlers[regs->int_no] != 0) {
        interrupt_handler_t handler = interrupt_handlers[regs->int_no];
        handler(regs);
    }
}