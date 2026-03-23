/*
 * idt.c - Interrupt Descriptor Table for x86-64
 *
 * Installs handlers for:
 *   - CPU exceptions (ISRs 0-21, vectors 0-31 in the IDT)
 *   - Hardware IRQs  (IRQs 0-15,  vectors 32-47 in the IDT)
 *
 * Exception handler:
 *   Prints diagnostic information and either kills the offending user
 *   process (recoverable) or halts the kernel (unrecoverable).
 *
 * IRQ handler:
 *   Dispatches timer and keyboard events, then sends EOI to the PIC.
 *
 * The timer IRQ additionally calls scheduler_tick() to drive preemptive
 * scheduling.
 */

#include "cpu/idt.h"
#include "kernel/kernel.h"
#include "kernel/scheduler.h"
#include "drivers/keyboard.h"
#include "drivers/graphices/vga.h"
#include "drivers/pic.h"
#include "cpu/gdt.h"
#include "cpu/paging.h"
#include "drivers/timer.h"

/* =========================================================================
 * Module data
 * ======================================================================= */

#define IDT_ENTRIES 256

static struct idt_entry idt[IDT_ENTRIES]       __attribute__((aligned(16)));
static struct idt_ptr   idt_pointer            __attribute__((aligned(16)));

/* Per-vector interrupt counts for diagnostics */
static uint64_t interrupt_counts[IDT_ENTRIES] = {0};

/* Per-exception counts (exceptions 0-31) */
static uint64_t exception_counts[32]          = {0};

/* Provided in idt_flush.asm */
extern void idt_flush_asm(uint64_t);

/* Human-readable exception names for the console output */
static const char *exception_names[32] = {
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

/* =========================================================================
 * Gate installation
 * ======================================================================= */

/*
 * idt_set_gate - write one 16-byte IDT entry.
 *
 * handler    - virtual address of the assembly stub
 * selector   - code segment selector (GDT_KERNEL_CODE = 0x08)
 * type_attr  - gate type and privilege flags
 *
 * IST entry:
 *   Double Fault (8) and Machine Check (18) use IST 1 so they always
 *   have a valid stack even if the current kernel stack is corrupted.
 *   All other exceptions and IRQs use IST 0 (current stack).
 */
void idt_set_gate(int num, uint64_t handler,
                  uint16_t selector, uint8_t type_attr) {
    if (num >= IDT_ENTRIES) return;

    idt[num].offset_low  = (uint16_t)(handler & 0xFFFF);
    idt[num].offset_mid  = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[num].selector    = selector;
    idt[num].type_attr   = type_attr;
    idt[num].reserved    = 0;

    idt[num].ist = (num == EXCEPTION_DOUBLE_FAULT ||
                    num == EXCEPTION_MACHINE_CHECK) ? 1 : 0;
}

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * idt_init - clear the IDT, install exception and IRQ stubs, init the PIC,
 * load the IDTR, and enable interrupts.
 */
void idt_init(void) {
    idt_pointer.limit = (uint16_t)(sizeof(struct idt_entry) * IDT_ENTRIES - 1);
    idt_pointer.base  = (uint64_t)&idt;

    memset(&idt,              0, sizeof(idt));
    memset(interrupt_counts,  0, sizeof(interrupt_counts));
    memset(exception_counts,  0, sizeof(exception_counts));

    /* Attribute byte for exception gates: Present, DPL0, interrupt gate.
     * Breakpoint (3) also allows DPL3 so user-space INT3 is handled. */
    uint8_t exc_attr  = IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT;
    uint8_t bp_attr   = IDT_ATTR_PRESENT | IDT_ATTR_DPL3 | IDT_TYPE_INTERRUPT;

    /* ---- CPU exception handlers (ISRs 0-21) ---- */
    idt_set_gate(0,  (uint64_t)isr0,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(1,  (uint64_t)isr1,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(2,  (uint64_t)isr2,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(3,  (uint64_t)isr3,  GDT_KERNEL_CODE, bp_attr);   /* breakpoint */
    idt_set_gate(4,  (uint64_t)isr4,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(5,  (uint64_t)isr5,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(6,  (uint64_t)isr6,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(7,  (uint64_t)isr7,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(8,  (uint64_t)isr8,  GDT_KERNEL_CODE, exc_attr);  /* double fault */
    idt_set_gate(9,  (uint64_t)isr9,  GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(10, (uint64_t)isr10, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(11, (uint64_t)isr11, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(12, (uint64_t)isr12, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(13, (uint64_t)isr13, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(14, (uint64_t)isr14, GDT_KERNEL_CODE, exc_attr);  /* page fault */
    idt_set_gate(15, (uint64_t)isr15, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(16, (uint64_t)isr16, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(17, (uint64_t)isr17, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(18, (uint64_t)isr18, GDT_KERNEL_CODE, exc_attr);  /* machine check */
    idt_set_gate(19, (uint64_t)isr19, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(20, (uint64_t)isr20, GDT_KERNEL_CODE, exc_attr);
    idt_set_gate(21, (uint64_t)isr21, GDT_KERNEL_CODE, exc_attr);

    /* ---- Hardware IRQ handlers (IRQs 0-15 -> vectors 32-47) ---- */
    uint8_t irq_attr = IDT_ATTR_PRESENT | IDT_ATTR_DPL0 | IDT_TYPE_INTERRUPT;

    idt_set_gate(32, (uint64_t)irq0,  GDT_KERNEL_CODE, irq_attr);  /* Timer    */
    idt_set_gate(33, (uint64_t)irq1,  GDT_KERNEL_CODE, irq_attr);  /* Keyboard */
    idt_set_gate(34, (uint64_t)irq2,  GDT_KERNEL_CODE, irq_attr);  /* Cascade  */
    idt_set_gate(35, (uint64_t)irq3,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(36, (uint64_t)irq4,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(37, (uint64_t)irq5,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(38, (uint64_t)irq6,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(39, (uint64_t)irq7,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(40, (uint64_t)irq8,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(41, (uint64_t)irq9,  GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(42, (uint64_t)irq10, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(43, (uint64_t)irq11, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(44, (uint64_t)irq12, GDT_KERNEL_CODE, irq_attr);  /* Mouse */
    idt_set_gate(45, (uint64_t)irq13, GDT_KERNEL_CODE, irq_attr);
    idt_set_gate(46, (uint64_t)irq14, GDT_KERNEL_CODE, irq_attr);  /* Primary ATA */
    idt_set_gate(47, (uint64_t)irq15, GDT_KERNEL_CODE, irq_attr);  /* Secondary ATA */

    pic_init();
    idt_flush_asm((uint64_t)&idt_pointer);
    __asm__ volatile("sti");

    vga_writestring("IDT: Initialized with ");
    print_dec(IDT_ENTRIES);
    vga_writestring(" entries\n");
}

/* =========================================================================
 * Exception handler (called from interrupt_handlers.asm stubs)
 * ======================================================================= */

/*
 * exception_handler - C-level exception dispatcher.
 *
 * Page faults are forwarded to page_fault_handler() which may satisfy them
 * via demand-paging.  All other exceptions in a user process kill that
 * process and reschedule.  Exceptions in the kernel (idle process) halt.
 */
void exception_handler(uint32_t exception_num, uint64_t error_code) {
    /* Update statistics */
    if (exception_num < 32) exception_counts[exception_num]++;
    interrupt_counts[exception_num]++;

    __asm__ volatile("cli");

    /* Page fault: handled separately with potential demand-paging */
    if (exception_num == EXCEPTION_PAGE_FAULT) {
        uint64_t fault_addr;
        __asm__ volatile("mov %%cr2, %0" : "=r"(fault_addr));
        page_fault_handler(error_code, fault_addr);
        return;
    }

    /* Print exception information */
    vga_setcolor(vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_RED));
    vga_writestring("\n\n===== CPU EXCEPTION =====\n");

    vga_writestring((exception_num < 32) ? exception_names[exception_num]
                                         : "Unknown Exception");
    vga_writestring(" (#");
    print_dec(exception_num);
    vga_writestring(")\n");
    vga_writestring("Error Code: 0x");
    print_hex(error_code);
    vga_writestring("\n");

    /* Kill a faulting user process and reschedule */
    struct process *cur = scheduler_current();
    if (cur && cur != scheduler_get_idle()) {
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_RED, VGA_COLOR_BLACK));
        vga_writestring("Process '");
        vga_writestring(cur->name);
        vga_writestring("' killed by exception\n");
        vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
        process_mark_zombie(cur, -(int)exception_num);
        schedule();
        return;
    }

    /* Kernel-mode exception: halt */
    vga_writestring("=========================\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("System halted.\n");
    hang();
}

/* =========================================================================
 * IRQ handler (called from interrupt_handlers.asm stubs)
 * ======================================================================= */

/*
 * irq_handler - C-level hardware IRQ dispatcher.
 *
 * scheduler_tick() is called BEFORE pic_send_eoi() so that any context
 * switch inside schedule() occurs while the PIC's in-service bit is still
 * set.  The new process sends the EOI when it returns from the interrupt
 * on its own stack.
 */
void irq_handler(uint32_t irq_num) {
    if (irq_num < 16) {
        interrupt_counts[32 + irq_num]++;
    }

    switch (irq_num) {
        case 0:   /* Timer: advance tick counter, then check scheduling */
            timer_handler();
            scheduler_tick();
            break;

        case 1:   /* Keyboard: queue the character in the ring buffer */
            keyboard_handler();
            break;

        default:
            /* Unhandled IRQ: EOI is still sent below */
            break;
    }

    pic_send_eoi(irq_num);
}
