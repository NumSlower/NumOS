#include "kernel.h"
#include "vga.h"
#include "keyboard.h"
#include "gdt.h"
#include "idt.h"
#include "paging.h"
#include "memory.h"

uint64_t placement_address;
extern char _kernel_end;

/* Timer interrupt handler */
static volatile uint64_t timer_ticks = 0;

void timer_handler(struct registers *regs) {
    (void)regs; /* Suppress unused parameter warning */
    timer_ticks++;
}

/* Keyboard interrupt handler */
void keyboard_interrupt_handler(struct registers *regs) {
    (void)regs; /* Suppress unused parameter warning */
    keyboard_handler();
}

void timer_init(void) {
    /* Register timer interrupt handler */
    register_interrupt_handler(IRQ_TIMER, &timer_handler);
    
    /* Set timer frequency */
    uint32_t divisor = 1193180 / 100; /* 100 Hz */
    
    /* Send command byte */
    outb(0x43, 0x36);
    
    /* Send frequency divisor */
    outb(0x40, divisor & 0xFF);
    outb(0x40, divisor >> 8);
    
    /* Enable timer IRQ */
    irq_enable(0);
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

void timer_wait(uint64_t ticks) {
    uint64_t target = timer_ticks + ticks;
    while (timer_ticks < target) {
        __asm__ volatile("hlt");
    }
}

void interrupts_enable(void) {
    __asm__ volatile("sti");
}

void interrupts_disable(void) {
    __asm__ volatile("cli");
}

int interrupts_enabled(void) {
    uint64_t flags;
    __asm__ volatile("pushf; pop %0" : "=r" (flags));
    return (flags & (1 << 9)) ? 1 : 0;
}

void print_system_info(void) {
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("NumOS System Information\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("========================\n");
    vga_writestring("Architecture: x86_64\n");
    vga_writestring("Version: 1.0 Enhanced\n");
    vga_writestring("Features: GDT, IDT, Paging, Memory Allocator\n");
    vga_writestring("Timer Frequency: 100 Hz\n");
    
    /* Print timer ticks */
    vga_writestring("System Uptime: ");
    /* Simple tick display - could be improved */
    uint64_t seconds = timer_ticks / 100;
    if (seconds < 10) vga_putchar('0');
    /* This is a simplified display - a proper itoa function would be better */
    vga_putchar('0' + (seconds % 10));
    vga_writestring(" seconds\n");
    
    vga_writestring("Interrupts: ");
    vga_writestring(interrupts_enabled() ? "Enabled" : "Disabled");
    vga_writestring("\n\n");
}

void print_memory_info(void) {
    memory_stats_t stats;
    memory_get_stats(&stats);
    
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREEN, VGA_COLOR_BLACK));
    vga_writestring("Memory Information\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("==================\n");
    
    /* Simplified memory display */
    vga_writestring("Total Physical Memory: ");
    uint64_t total_mb = stats.total_physical / (1024 * 1024);
    if (total_mb > 0) {
        /* Simple MB display */
        vga_putchar('0' + (total_mb / 100) % 10);
        vga_putchar('0' + (total_mb / 10) % 10);
        vga_putchar('0' + total_mb % 10);
    } else {
        vga_putchar('0');
    }
    vga_writestring(" MB\n");
    
    vga_writestring("Available Memory: ");
    uint64_t avail_mb = stats.available_physical / (1024 * 1024);
    if (avail_mb > 0) {
        vga_putchar('0' + (avail_mb / 100) % 10);
        vga_putchar('0' + (avail_mb / 10) % 10);
        vga_putchar('0' + avail_mb % 10);
    } else {
        vga_putchar('0');
    }
    vga_writestring(" MB\n");
    
    vga_writestring("Heap Size: ");
    uint64_t heap_mb = stats.heap_size / (1024 * 1024);
    if (heap_mb > 0) {
        vga_putchar('0' + (heap_mb / 10) % 10);
        vga_putchar('0' + heap_mb % 10);
    } else {
        vga_putchar('0');
    }
    vga_writestring(" MB\n");
    
    vga_writestring("Heap Used: ");
    uint64_t used_mb = stats.heap_used / (1024 * 1024);
    if (used_mb > 0) {
        vga_putchar('0' + (used_mb / 10) % 10);
        vga_putchar('0' + used_mb % 10);
    } else {
        vga_putchar('0');
    }
    vga_writestring(" MB\n");
    
    vga_writestring("Allocations: ");
    uint32_t allocs = stats.allocation_count;
    if (allocs >= 1000) {
        vga_putchar('0' + (allocs / 1000) % 10);
        allocs %= 1000;
    }
    if (allocs >= 100) {
        vga_putchar('0' + (allocs / 100) % 10);
        allocs %= 100;
    }
    if (allocs >= 10) {
        vga_putchar('0' + (allocs / 10) % 10);
        allocs %= 10;
    }
    vga_putchar('0' + allocs);
    vga_writestring("\n\n");
}

void system_init(void) {
    /* Initialize core system components */
    paging_init();        /* Initialize paging system */
    vga_writestring("Paging initialized.\n");
    placement_address = (uint64_t)&_kernel_end;
    memory_init();        /* Initialize memory allocator */
    vga_writestring("Memory initialized.\n");
    timer_init();         /* Initialize system timer */
    vga_writestring("Timer initialized.\n");
    
    /* Register keyboard interrupt handler */
    register_interrupt_handler(IRQ_KEYBOARD, &keyboard_interrupt_handler);
    irq_enable(1);        /* Enable keyboard IRQ */
    vga_writestring("Keyboard IRQ enabled.\n");
}

void kernel_init(void) {
    /* Initialize VGA text mode */
    vga_init();
    vga_writestring("VGA initialized.\n");
    
    /* Initialize system components */
    system_init();
    vga_writestring("System initialized.\n");
    
    /* Initialize keyboard */
    keyboard_init();
    vga_writestring("Keyboard initialized.\n");
    
    /* Display welcome message */
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_CYAN, VGA_COLOR_BLACK));
    vga_writestring("Welcome to NumOS Enhanced Edition\n");
    vga_setcolor(vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK));
    vga_writestring("64-bit Operating System with GDT, IDT, Paging, and Memory Management\n");
    vga_writestring("Type 'help' for available commands.\n\n");
    
    /* Display system information */
    print_system_info();
}

void kernel_main(void) {
    /* Initialize core system components */
    gdt_init();           /* Initialize Global Descriptor Table */
    idt_init();           /* Initialize Interrupt Descriptor Table */

    /* Initialize kernel subsystems */
    kernel_init();
    
    /* Command buffer */
    char command_buffer[256];
    
    /* Main command loop */
    while (1) {
        /* Print prompt */
        print_prompt();
        
        /* Read command from user */
        keyboard_read_line(command_buffer, sizeof(command_buffer));
        
        /* Process the command */
        process_command(command_buffer);
    }
}