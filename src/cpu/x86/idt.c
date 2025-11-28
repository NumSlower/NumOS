#include "cpu/idt.h"
#include "kernel/kernel.h"
#include "drivers/keyboard.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "cpu/gdt.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* IDT entries - we'll use 256 entries (0-255) */
#define IDT_ENTRIES 256
static struct idt_entry idt[IDT_ENTRIES];
static struct idt_ptr idt_pointer;

/* External assembly function to flush IDT */
extern void idt_flush_asm(uint64_t);

void idt_set_gate(int num, uint64_t handler, uint16_t selector, uint8_t type_attr) {
    idt[num].offset_low = handler & 0xFFFF;
    idt[num].offset_mid = (handler >> 16) & 0xFFFF;
    idt[num].offset_high = (handler >> 32) & 0xFFFFFFFF;
    idt[num].selector = selector;
    idt[num].ist = 0;  // No IST for now
    idt[num].type_attr = type_attr;
    idt[num].reserved = 0;
}

void idt_init(void) {
    /* Set up IDT pointer */
    idt_pointer.limit = (sizeof(struct idt_entry) * IDT_ENTRIES) - 1;
    idt_pointer.base = (uint64_t)&idt;
    
    /* Clear IDT */
    memset(&idt, 0, sizeof(struct idt_entry) * IDT_ENTRIES);
    
    /* Install exception handlers (ISRs 0-21) */
    idt_set_gate(0, (uint64_t)isr0, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(1, (uint64_t)isr1, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(2, (uint64_t)isr2, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(3, (uint64_t)isr3, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(4, (uint64_t)isr4, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(5, (uint64_t)isr5, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(6, (uint64_t)isr6, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(7, (uint64_t)isr7, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(8, (uint64_t)isr8, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(9, (uint64_t)isr9, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    
    /* Install IRQ handlers (IRQs 0-15 mapped to IDT entries 32-47) */
    idt_set_gate(32, (uint64_t)irq0, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(33, (uint64_t)irq1, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(34, (uint64_t)irq2, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(35, (uint64_t)irq3, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(36, (uint64_t)irq4, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(37, (uint64_t)irq5, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(38, (uint64_t)irq6, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(39, (uint64_t)irq7, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(40, (uint64_t)irq8, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(41, (uint64_t)irq9, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT);
    
    /* Initialize PIC before loading IDT */
    pic_init();
    
    /* Load the IDT */
    idt_flush_asm((uint64_t)&idt_pointer);
    
    /* Enable interrupts */
    __asm__ volatile("sti");
}

void exception_handler(uint32_t exception_num, uint64_t error_code) {
    /* Disable interrupts */
    __asm__ volatile("cli");
    
    /* Special handling for page fault */
    if (exception_num == EXCEPTION_PAGE_FAULT) {
        uint64_t fault_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
        page_fault_handler(error_code, fault_addr);
        return;
    }
    
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\nEXCEPTION: ");
    
    switch (exception_num) {
        case EXCEPTION_DIVIDE_ERROR:
            vga_writestring("Division by Zero");
            break;
        case EXCEPTION_DEBUG:
            vga_writestring("Debug Exception");
            break;
        case EXCEPTION_NMI:
            vga_writestring("Non-Maskable Interrupt");
            break;
        case EXCEPTION_BREAKPOINT:
            vga_writestring("Breakpoint");
            break;
        case EXCEPTION_OVERFLOW:
            vga_writestring("Overflow");
            break;
        case EXCEPTION_BOUND_RANGE:
            vga_writestring("Bound Range Exceeded");
            break;
        case EXCEPTION_INVALID_OPCODE:
            vga_writestring("Invalid Opcode");
            break;
        case EXCEPTION_DEVICE_NOT_AVAILABLE:
            vga_writestring("Device Not Available");
            break;
        case EXCEPTION_DOUBLE_FAULT:
            vga_writestring("Double Fault");
            break;
        case EXCEPTION_INVALID_TSS:
            vga_writestring("Invalid TSS");
            break;
        case EXCEPTION_SEGMENT_NOT_PRESENT:
            vga_writestring("Segment Not Present");
            break;
        case EXCEPTION_STACK_SEGMENT:
            vga_writestring("Stack Segment Fault");
            break;
        case EXCEPTION_GENERAL_PROTECTION:
            vga_writestring("General Protection Fault");
            break;
        case EXCEPTION_X87_FPU:
            vga_writestring("x87 FPU Error");
            break;
        case EXCEPTION_ALIGNMENT_CHECK:
            vga_writestring("Alignment Check");
            break;
        case EXCEPTION_MACHINE_CHECK:
            vga_writestring("Machine Check");
            break;
        case EXCEPTION_SIMD_FP:
            vga_writestring("SIMD Floating Point");
            break;
        default:
            vga_writestring("Unknown Exception");
            break;
    }
    
    vga_writestring(" (Exception #");
    print_hex(exception_num);
    
    if (error_code) {
        vga_writestring(", Error Code: 0x");
        print_hex(error_code);
    }
    
    vga_writestring(")\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("System halted.\n");
    
    /* Hang the system */
    hang();
}

void irq_handler(uint32_t irq_num) {
    /* Handle specific IRQs */
    switch (irq_num) {
        case 0: /* Timer IRQ */
            /* Handle timer interrupt */
            timer_handler();
            break;
            
        case 1: /* Keyboard IRQ */
            /* Handle keyboard input */
            keyboard_handler();
            break;
            
        default:
            /* Other IRQs - just acknowledge for now */
            break;
    }
    
    /* Send End of Interrupt signal to PIC */
    pic_send_eoi(irq_num);
}