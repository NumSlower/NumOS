/*
 * idt.c - Interrupt Descriptor Table for x86-64
 * Handles CPU exceptions and hardware interrupts
 */

#include "cpu/idt.h"
#include "kernel/kernel.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "cpu/gdt.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* IDT with 256 entries */
#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idt_pointer __attribute__((aligned(16)));

/* Interrupt statistics */
static uint64_t interrupt_counts[IDT_ENTRIES] = {0};
static uint64_t exception_counts[32] = {0};

/* External assembly function */
extern void idt_flush_asm(uint64_t);

/* Exception names */
static const char* exception_names[32] = {
    "Division By Zero",
    "Debug",
    "Non-Maskable Interrupt",
    "Breakpoint",
    "Overflow",
    "Bound Range Exceeded",
    "Invalid Opcode",
    "Device Not Available",
    "Double Fault",
    "Coprocessor Segment Overrun",
    "Invalid TSS",
    "Segment Not Present",
    "Stack Segment Fault",
    "General Protection Fault",
    "Page Fault",
    "Reserved",
    "x87 FPU Error",
    "Alignment Check",
    "Machine Check",
    "SIMD Floating Point",
    "Virtualization",
    "Control Protection",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved"
};

/*
 * Set an IDT entry
 */
void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    if (num >= IDT_ENTRIES) {
        return;
    }
    
    /* Set handler address */
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    
    /* Set selector (code segment) */
    idt[num].selector = selector;
    
    /* Set IST (Interrupt Stack Table) */
    if (num == EXCEPTION_DOUBLE_FAULT || num == EXCEPTION_MACHINE_CHECK) {
        idt[num].ist = 1;  /* Use IST 1 for critical exceptions */
    } else {
        idt[num].ist = 0;  /* Use current stack */
    }
    
    /* Set type and attributes */
    idt[num].type_attr = type_attr;
    
    /* Reserved must be zero */
    idt[num].reserved = 0;
}

/*
 * Initialize IDT
 */
void idt_init(void) {
    /* Set up IDT pointer */
    idt_pointer.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idt_pointer.base = (uint64_t)&idt;
    
    /* Clear IDT and statistics */
    memset(&idt, 0, sizeof(struct idt_entry) * IDT_ENTRIES);
    memset(interrupt_counts, 0, sizeof(interrupt_counts));
    memset(exception_counts, 0, sizeof(exception_counts));
    
    /* Install exception handlers (ISRs 0-31) */
    uint8_t exc_attr = IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT;
    
    idt_set_gate(0, (uint64_t)isr0, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(1, (uint64_t)isr1, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(2, (uint64_t)isr2, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(3, (uint64_t)isr3, GDT_KERNEL_CODE, exc_attr | IDT_ATTR_DPL3);
    idt_set_gate(4, (uint64_t)isr4, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(5, (uint64_t)isr5, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(6, (uint64_t)isr6, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(7, (uint64_t)isr7, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(8, (uint64_t)isr8, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(9, (uint64_t)isr9, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, exc_attr);
    
    /* Install IRQ handlers (IRQs 0-15 → interrupts 32-47) */
    uint8_t irq_attr = IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT;
    
    idt_set_gate(32, (uint64_t)irq0, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(33, (uint64_t)irq1, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(34, (uint64_t)irq2, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(35, (uint64_t)irq3, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(36, (uint64_t)irq4, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(37, (uint64_t)irq5, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(38, (uint64_t)irq6, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(39, (uint64_t)irq7, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(40, (uint64_t)irq8, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(41, (uint64_t)irq9, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, irq_attr);
    
    /* Initialize PIC */
    pic_init();
    
    /* Load IDT */
    idt_flush_asm((uint64_t)&idt_pointer);
    
    /* Enable interrupts */
    __asm__ volatile("sti");
    
    vga_writestring("IDT: Initialized with ");
    print_dec(IDT_ENTRIES);
    vga_writestring(" entries\n");
}

/*
 * Exception handler (called from assembly)
 */
void exception_handler(uint32_t exception_num, uint64_t error_code) {
    /* Update statistics */
    if (exception_num < 32) {
        exception_counts[exception_num]++;
    }
    interrupt_counts[exception_num]++;
    
    /* Disable interrupts */
    __asm__ volatile("cli");
    
    /* Handle page fault specially */
    if (exception_num == EXCEPTION_PAGE_FAULT) {
        uint64_t fault_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
        page_fault_handler(error_code, fault_addr);
        __asm__ volatile("sti");
        return;
    }
    
    /* Display exception information */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\n===== CPU EXCEPTION =====\n");
    
    if (exception_num < 32) {
        vga_writestring("Exception: ");
        vga_writestring(exception_names[exception_num]);
    } else {
        vga_writestring("Unknown Exception");
    }
    
    vga_writestring(" (#");
    print_dec(exception_num);
    vga_writestring(")\n");
    
    vga_writestring("Error Code: 0x");
    print_hex(error_code);
    vga_writestring("\n");
    
    vga_writestring("Count: ");
    print_dec(exception_counts[exception_num]);
    vga_writestring("\n");
    
    vga_writestring("=========================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("System halted.\n");
    
    hang();
}

/*
 * IRQ handler (called from assembly)
 */
void irq_handler(uint32_t irq_num) {
    /* Update statistics */
    if (irq_num < 16) {
        interrupt_counts[32 + irq_num]++;
    }
    
    /* Handle specific IRQs */
    switch (irq_num) {
        case 0:  /* Timer */
            timer_handler();
            break;
            
        case 1:  /* Keyboard */
            keyboard_handler();
            break;
            
        default:
            break;
    }
    
    /* Send EOI to PIC */
    pic_send_eoi(irq_num);
}

/*
 * Print IDT information
 */
void idt_print_info(void) {
    vga_writestring("\nIDT Information:\n");
    vga_writestring("  Base: 0x");
    print_hex(idt_pointer.base);
    vga_writestring("\n  Limit: ");
    print_dec(idt_pointer.limit + 1);
    vga_writestring(" bytes\n");
    vga_writestring("  Entries: ");
    print_dec(IDT_ENTRIES);
    vga_writestring("\n");
}

/*
 * Print interrupt statistics
 */
void idt_print_stats(void) {
    vga_writestring("\nInterrupt Statistics:\n");
    
    vga_writestring("Exceptions:\n");
    for (int i = 0; i < 32; i++) {
        if (exception_counts[i] > 0) {
            vga_writestring("  ");
            vga_writestring(exception_names[i]);
            vga_writestring(": ");
            print_dec(exception_counts[i]);
            vga_writestring("\n");
        }
    }
    
    vga_writestring("\nHardware IRQs:\n");
    const char* irq_names[] = {
        "Timer", "Keyboard", "Cascade", "COM2", "COM1", "LPT2",
        "Floppy", "LPT1", "RTC", "Free", "Free", "Free",
        "Mouse", "FPU", "Primary ATA", "Secondary ATA"
    };
    
    for (int i = 0; i < 16; i++) {
        if (interrupt_counts[32 + i] > 0) {
            vga_writestring("  IRQ");
            print_dec(i);
            vga_writestring(" (");
            vga_writestring(irq_names[i]);
            vga_writestring("): ");
            print_dec(interrupt_counts[32 + i]);
            vga_writestring("\n");
        }
    }
}