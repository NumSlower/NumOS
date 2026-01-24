/*
 * idt.c - Enhanced Interrupt Descriptor Table for 64-bit mode
 * Handles CPU exceptions and hardware interrupts (IRQs)
 */

#include "cpu/idt.h"
#include "kernel/kernel.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "cpu/gdt.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* IDT entries - 256 entries for all possible interrupts */
#define IDT_ENTRIES 256

/* Aligned IDT structure */
static struct idt_entry idt[IDT_ENTRIES] __attribute__((aligned(16)));
static struct idt_ptr idt_pointer __attribute__((aligned(16)));

/* Interrupt statistics */
static uint64_t interrupt_counts[IDT_ENTRIES] = {0};
static uint64_t exception_counts[32] = {0};

/* External assembly function to flush IDT */
extern void idt_flush_asm(uint64_t);

/* Exception names for better error reporting */
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
    "x87 Floating Point Exception",
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
    "Reserved",
    "Reserved"
};

/*
 * Set an IDT entry
 */
void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    if (num >= IDT_ENTRIES) {
        vga_writestring("IDT: Warning - Invalid entry number\n");
        return;
    }
    
    /* Set handler address (split across three fields) */
    idt[num].offset_low = handler & 0xFFFF;           /* Bits 0-15 */
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;   /* Bits 16-31 */
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF; /* Bits 32-63 */
    
    /* Set selector (code segment) */
    idt[num].selector = selector;
    
    /* Set IST (Interrupt Stack Table) - 0 for now (use current stack) */
    idt[num].ist = 0;
    
    /* Set type and attributes */
    idt[num].type_attr = type_attr;
    
    /* Reserved field must be zero */
    idt[num].reserved = 0;
}

/*
 * Initialize the IDT with exception and interrupt handlers
 */
void idt_init(void) {
    /* Set up IDT pointer structure */
    idt_pointer.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idt_pointer.base = (uint64_t)&idt;
    
    /* Clear entire IDT */
    memset(&idt, 0, sizeof(struct idt_entry) * IDT_ENTRIES);
    memset(interrupt_counts, 0, sizeof(interrupt_counts));
    memset(exception_counts, 0, sizeof(exception_counts));
    
    /* 
     * Install CPU Exception Handlers (ISRs 0-31)
     * These are defined in interrupt_handlers.asm
     */
    
    /* Type attributes for exception handlers:
     * - IDT_ATTR_PRESENT: Gate is present
     * - IDT_ATTR_DPL0: Privilege level 0 (kernel only)
     * - IDT_TYPE_INTERRUPT: 64-bit interrupt gate
     */
    uint8_t exception_attr = IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT;
    
    idt_set_gate(0, (uint64_t)isr0, GDT_KERNEL_CODE, exception_attr);   /* Division By Zero */
    idt_set_gate(1, (uint64_t)isr1, GDT_KERNEL_CODE, exception_attr);   /* Debug */
    idt_set_gate(2, (uint64_t)isr2, GDT_KERNEL_CODE, exception_attr);   /* NMI */
    idt_set_gate(3, (uint64_t)isr3, GDT_KERNEL_CODE, exception_attr);   /* Breakpoint */
    idt_set_gate(4, (uint64_t)isr4, GDT_KERNEL_CODE, exception_attr);   /* Overflow */
    idt_set_gate(5, (uint64_t)isr5, GDT_KERNEL_CODE, exception_attr);   /* Bound Range */
    idt_set_gate(6, (uint64_t)isr6, GDT_KERNEL_CODE, exception_attr);   /* Invalid Opcode */
    idt_set_gate(7, (uint64_t)isr7, GDT_KERNEL_CODE, exception_attr);   /* Device Not Available */
    idt_set_gate(8, (uint64_t)isr8, GDT_KERNEL_CODE, exception_attr);   /* Double Fault */
    idt_set_gate(9, (uint64_t)isr9, GDT_KERNEL_CODE, exception_attr);   /* Coprocessor Segment */
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, exception_attr); /* Invalid TSS */
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, exception_attr); /* Segment Not Present */
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, exception_attr); /* Stack Fault */
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, exception_attr); /* General Protection */
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, exception_attr); /* Page Fault */
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, exception_attr); /* Reserved */
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, exception_attr); /* x87 FPU Error */
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, exception_attr); /* Alignment Check */
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, exception_attr); /* Machine Check */
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, exception_attr); /* SIMD FP */
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, exception_attr); /* Virtualization */
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, exception_attr); /* Control Protection */
    
    /* Fill remaining exception slots (22-31) with reserved handler */
    for (int i = 22; i < 32; i++) {
        idt_set_gate(i, (uint64_t)isr15, GDT_KERNEL_CODE, exception_attr);
    }
    
    /*
     * Install IRQ Handlers (IRQs 0-15 mapped to IDT entries 32-47)
     * Hardware interrupts from PIC
     */
    uint8_t irq_attr = IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT;
    
    idt_set_gate(32, (uint64_t)irq0, GDT_KERNEL_CODE, irq_attr);  /* Timer */
    idt_set_gate(33, (uint64_t)irq1, GDT_KERNEL_CODE, irq_attr);  /* Keyboard */
    idt_set_gate(34, (uint64_t)irq2, GDT_KERNEL_CODE, irq_attr);  /* Cascade */
    idt_set_gate(35, (uint64_t)irq3, GDT_KERNEL_CODE, irq_attr);  /* COM2 */
    idt_set_gate(36, (uint64_t)irq4, GDT_KERNEL_CODE, irq_attr);  /* COM1 */
    idt_set_gate(37, (uint64_t)irq5, GDT_KERNEL_CODE, irq_attr);  /* LPT2 */
    idt_set_gate(38, (uint64_t)irq6, GDT_KERNEL_CODE, irq_attr);  /* Floppy */
    idt_set_gate(39, (uint64_t)irq7, GDT_KERNEL_CODE, irq_attr);  /* LPT1 */
    idt_set_gate(40, (uint64_t)irq8, GDT_KERNEL_CODE, irq_attr);  /* RTC */
    idt_set_gate(41, (uint64_t)irq9, GDT_KERNEL_CODE, irq_attr);  /* Free */
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, irq_attr); /* Free */
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, irq_attr); /* Free */
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, irq_attr); /* Mouse */
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, irq_attr); /* FPU */
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, irq_attr); /* Primary ATA */
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, irq_attr); /* Secondary ATA */
    
    /* Initialize PIC before loading IDT */
    pic_init();
    
    /* Load the IDT */
    idt_flush_asm((uint64_t)&idt_pointer);
    
    /* Enable interrupts */
    __asm__ volatile("sti");
    
    vga_writestring("IDT: Loaded with ");
    print_dec(IDT_ENTRIES);
    vga_writestring(" entries (interrupts enabled)\n");
}

/*
 * CPU Exception Handler
 * Called from assembly interrupt stubs
 */
void exception_handler(uint32_t exception_num, uint64_t error_code) {
    /* Update statistics */
    if (exception_num < 32) {
        exception_counts[exception_num]++;
    }
    interrupt_counts[exception_num]++;
    
    /* Disable interrupts for safety */
    __asm__ volatile("cli");
    
    /* Special handling for page fault */
    if (exception_num == EXCEPTION_PAGE_FAULT) {
        uint64_t fault_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
        page_fault_handler(error_code, fault_addr);
        return;
    }
    
    /* Display exception information */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\n===== CPU EXCEPTION =====\n");
    
    /* Exception name */
    if (exception_num < 32) {
        vga_writestring("Exception: ");
        vga_writestring(exception_names[exception_num]);
    } else {
        vga_writestring("Unknown Exception");
    }
    
    vga_writestring(" (#");
    print_dec(exception_num);
    vga_writestring(")\n");
    
    /* Error code if present */
    if (error_code != 0) {
        vga_writestring("Error Code: 0x");
        print_hex(error_code);
        vga_writestring("\n");
        
        /* Decode error code for specific exceptions */
        if (exception_num == EXCEPTION_PAGE_FAULT) {
            vga_writestring("  ");
            if (error_code & 0x01) vga_writestring("Page-protection violation ");
            else vga_writestring("Page not present ");
            if (error_code & 0x02) vga_writestring("(write) ");
            else vga_writestring("(read) ");
            if (error_code & 0x04) vga_writestring("(user mode) ");
            else vga_writestring("(kernel mode) ");
            if (error_code & 0x08) vga_writestring("(reserved bits set) ");
            if (error_code & 0x10) vga_writestring("(instruction fetch) ");
            vga_writestring("\n");
        }
    }
    
    /* Exception count */
    vga_writestring("Occurrence: ");
    print_dec(exception_counts[exception_num]);
    vga_writestring("\n");
    
    vga_writestring("=========================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("System halted.\n");
    
    /* Halt the system */
    hang();
}

/*
 * IRQ Handler
 * Called from assembly interrupt stubs
 */
void irq_handler(uint32_t irq_num) {
    /* Update statistics */
    if (irq_num < 16) {
        interrupt_counts[32 + irq_num]++;
    }
    
    /* Handle specific IRQs */
    switch (irq_num) {
        case 0: /* Timer IRQ (IRQ0) */
            timer_handler();
            break;
            
        case 1: /* Keyboard IRQ (IRQ1) */
            keyboard_handler();
            break;
            
        /* Add more IRQ handlers here as needed */
        
        default:
            /* Unknown IRQ - just acknowledge */
            break;
    }
    
    /* Send End of Interrupt signal to PIC */
    pic_send_eoi(irq_num);
}

/*
 * Print IDT information for debugging
 */
void idt_print_info(void) {
    vga_writestring("IDT Information:\n");
    vga_writestring("  Base: 0x");
    print_hex(idt_pointer.base);
    vga_writestring("\n  Limit: ");
    print_dec(idt_pointer.limit + 1);
    vga_writestring(" bytes\n");
    vga_writestring("  Entries: ");
    print_dec(IDT_ENTRIES);
    vga_writestring("\n\n");
    
    vga_writestring("Exception Handlers (0-31):\n");
    for (int i = 0; i < 32; i++) {
        if (idt[i].offset_low != 0 || idt[i].offset_mid != 0) {
            vga_writestring("  ");
            print_dec(i);
            vga_writestring(": ");
            vga_writestring(exception_names[i]);
            vga_writestring("\n");
        }
    }
    
    vga_writestring("\nIRQ Handlers (32-47):\n");
    const char* irq_names[] = {
        "Timer", "Keyboard", "Cascade", "COM2", "COM1", "LPT2", "Floppy", "LPT1",
        "RTC", "Free", "Free", "Free", "Mouse", "FPU", "Primary ATA", "Secondary ATA"
    };
    
    for (int i = 0; i < 16; i++) {
        if (idt[32 + i].offset_low != 0 || idt[32 + i].offset_mid != 0) {
            vga_writestring("  IRQ");
            print_dec(i);
            vga_writestring(": ");
            vga_writestring(irq_names[i]);
            vga_writestring("\n");
        }
    }
}

/*
 * Print interrupt statistics
 */
void idt_print_stats(void) {
    vga_writestring("Interrupt Statistics:\n\n");
    
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
    
    vga_writestring("\nIRQs:\n");
    const char* irq_names[] = {
        "Timer", "Keyboard", "Cascade", "COM2", "COM1", "LPT2", "Floppy", "LPT1",
        "RTC", "Free", "Free", "Free", "Mouse", "FPU", "Primary ATA", "Secondary ATA"
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