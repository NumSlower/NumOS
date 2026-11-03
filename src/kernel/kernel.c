/* NumOS — kernel.c — Kernel Entry */
#include "kernel/types.h"
#include "kernel/kprintf.h"
#include "kernel/string.h"
#include "kernel/multiboot2.h"
#include "kernel/gdt.h"
#include "kernel/paging.h"
#include "kernel/idt.h"
#include "kernel/isr.h"
#include "kernel/pic.h"
#include "kernel/irq.h"
#include "kernel/nmi.h"
#include "kernel/pmm.h"
#include "kernel/vmm.h"
#include "kernel/heap.h"
#include "kernel/panic.h"
#include "kernel/spinlock.h"
#include "kernel/kdebug.h"
#include "cpu/cpuid.h"
#include "cpu/sse.h"
#include "drivers/serial.h"
#include "drivers/vga.h"
#include "drivers/bga.h"
#include "drivers/keyboard.h"
#include "drivers/pit.h"
#include "drivers/acpi.h"
#include "drivers/apic.h"
#include "drivers/ioapic.h"
#include "kernel/io.h"

static void print_ok(const char* name) {
    vga_set_color(WHITE, BLACK);
    kprintf("Initializing %s", name);
    int len = strlen(name) + 13; // "Initializing "
    for (int i = len; i < 40; i++) kprintf(".");
    kprintf(" ");
    vga_set_color(LIGHT_GREEN, BLACK);
    kprintf("[OK]\n");
    vga_set_color(WHITE, BLACK);
}

static void print_fail(const char* name) {
    vga_set_color(WHITE, BLACK);
    kprintf("Initializing %s", name);
    int len = strlen(name) + 13;
    for (int i = len; i < 40; i++) kprintf(".");
    kprintf(" ");
    vga_set_color(LIGHT_RED, BLACK);
    kprintf("[FAIL]\n");
    vga_set_color(WHITE, BLACK);
}

static void print_skip(const char* name) {
    vga_set_color(WHITE, BLACK);
    kprintf("Initializing %s", name);
    int len = strlen(name) + 13;
    for (int i = len; i < 40; i++) kprintf(".");
    kprintf(" ");
    vga_set_color(YELLOW, BLACK);
    kprintf("[SKIP]\n");
    vga_set_color(WHITE, BLACK);
}

void kernel_main(uint32_t magic, uint64_t mb2_info_addr) {
    vga_init();
    serial_init();
    kprintf_register_putchar(serial_putchar_backend);
    kprintf_register_putchar(vga_putchar_backend);
    
    vga_set_color(LIGHT_CYAN, BLACK);
    kprintf("NumOS v0.1 - 64-bit Kernel\n");
    kprintf("===========================\n");
    vga_set_color(WHITE, BLACK);
    
#ifdef KDEBUG
    vga_set_color(YELLOW, BLACK);
    kprintf("*** DEBUG BUILD ***\n");
    vga_set_color(WHITE, BLACK);
#endif
    
    if (magic != 0x36D76289) {
        kpanic("Invalid Multiboot2 magic!");
    }
    
    mb2_parse(mb2_info_addr);
    print_ok("Multiboot2 Info");
    kdebug("Multiboot2 info at 0x%lx", mb2_info_addr);
    {
        uint64_t total_mem = mb2_get_total_memory();
        kdebug("Total usable RAM: %lu MB (%lu KB)", total_mem / (1024*1024), total_mem / 1024);
    }
    print_ok("Serial");
    kdebug("COM1 @ 0x3F8, 115200 baud, 8N1");
    
    gdt_init();
    print_ok("GDT");
    kdebug("GDT: 7 entries (null, kcode64, kdata, udata, ucode64, tss[2])");
    print_ok("TSS");
    kdebug("TSS: 3 IST stacks (4 KB each)");
    
    paging_init();
    print_ok("Paging");
    kdebug("Paging: EFER.NXE enabled, CR0.WP enabled");
    kdebug("Boot identity map: 4 GB (2 MB huge pages)");
    
    idt_init();
    exceptions_init();
    print_ok("IDT");
    kdebug("IDT: 256 entries loaded");
    print_ok("Exceptions");
    kdebug("Exceptions: vectors 0-31 registered (IST: NMI=1, DF=2, MC=3)");
    
    pic_init();
    print_ok("PIC");
    kdebug("PIC: remapped IRQ 0-15 -> vectors 32-47");
    
    irq_init();
    print_ok("IRQs");
    kdebug("IRQ: 16 hardware interrupt lines ready");
    
    cpuid_init();
    print_ok("CPU Features");
    kdebug("CPUID max leaf: basic=0x%x", 0); /* cpuid_init already prints vendor/brand */
    kdebug("Features: APIC=%s MSR=%s SSE=%s SSE2=%s FPU=%s TSC=%s PAE=%s NX=%s",
           cpuid_has_apic() ? "yes" : "no",
           cpuid_has_msr() ? "yes" : "no",
           cpuid_has_sse() ? "yes" : "no",
           cpuid_has_sse2() ? "yes" : "no",
           cpuid_has_fpu() ? "yes" : "no",
           cpuid_has_tsc() ? "yes" : "no",
           cpuid_has_pae() ? "yes" : "no",
           cpuid_has_nx() ? "yes" : "no");
    kdebug("Features: x2APIC=%s 1GB-pages=%s long-mode=%s",
           cpuid_has_x2apic() ? "yes" : "no",
           cpuid_has_1gb_pages() ? "yes" : "no",
           cpuid_has_long_mode() ? "yes" : "no");
    
    sse_init();
    print_ok("FPU/SSE");
    kdebug("FPU: x87 initialized (fninit)");
    kdebug("SSE: CR0.EM=0 CR0.MP=1 CR0.NE=1 CR4.OSFXSR=1 CR4.OSXMMEXCPT=1");
    kdebug("MXCSR: 0x1F80 (default mask)");
    
    nmi_init();
    print_ok("NMI");
    kdebug("NMI: unmasked (port 0x70 bit 7 cleared)");
    
    void* rsdp = mb2_get_rsdp();
    if (rsdp) {
        acpi_init(rsdp);
        print_ok("ACPI Tables");
        kdebug("RSDP at %p", rsdp);
        kdebug("LAPIC base: 0x%lx", acpi_get_lapic_base());
        kdebug("I/O APIC base: 0x%lx (ID %u, GSI base %u)",
               acpi_get_ioapic_base(), acpi_get_ioapic_id(), acpi_get_ioapic_gsi_base());
        kdebug("CPU count (MADT): %u", acpi_get_cpu_count());
        {
            uint8_t iso_count = 0;
            struct acpi_iso* isos = acpi_get_isos(&iso_count);
            kdebug("Interrupt source overrides: %u", iso_count);
            for (uint8_t i = 0; i < iso_count; i++) {
                kdebug("  ISO[%u]: bus=%u src=%u -> GSI %u (flags 0x%x)",
                       i, isos[i].bus, isos[i].source, isos[i].gsi, isos[i].flags);
            }
        }
    } else {
        print_fail("ACPI Tables");
        kdebug("RSDP: not found in Multiboot2 tags");
    }
    
    if (apic_init()) {
        print_ok("APIC");
        kdebug("Local APIC enabled at 0x%lx, ID=%u", acpi_get_lapic_base(), apic_get_id());
        kdebug("SVR=0x1FF (vector 0xFF), TPR=0, LVT_ERROR=0xFE");
    } else {
        print_fail("APIC");
        kdebug("APIC: not available or no base address");
    }
    
    ioapic_init();
    print_ok("I/O APIC");
    kdebug("I/O APIC: all entries masked on init");
    
    pit_init(1000);
    print_ok("PIT");
    kdebug("PIT: channel 0, rate generator mode, 1000 Hz");
    kdebug("PIT: divisor=%u (base 1193182 Hz)", 1193182 / 1000);
    
    pmm_init(mb2_get_mmap());
    print_ok("PMM");
    {
        uint64_t free_frames = pmm_get_free_count();
        uint64_t total_frames = pmm_get_total_count();
        kdebug("PMM: %lu / %lu frames free (%lu MB / %lu MB)",
               free_frames, total_frames,
               (free_frames * 4096) / (1024*1024),
               (total_frames * 4096) / (1024*1024));
    }
    
    vmm_init();
    print_ok("VMM");
    {
        uint64_t cr3;
        __asm__ volatile("mov %%cr3, %0" : "=r"(cr3));
        kdebug("VMM: PML4 at 0x%lx (from CR3)", cr3);
    }
    
    heap_init(0x400000, 0x100000);
    print_ok("Heap");
    
    keyboard_init();
    print_ok("Keyboard");
    kdebug("PS/2 keyboard: IRQ 1 registered, buffer flushed");
    
    print_ok("VGA");
    kdebug("VGA: text mode 80x25, buffer at 0xB8000");
    
    if (bga_init()) {
        print_ok("BGA");
        kdebug("Bochs Graphics Adapter detected");
    } else {
        print_skip("BGA");
        kdebug("BGA: not available (not a Bochs/VirtualBox display)");
    }
    
    __asm__ volatile("sti");
    kdebug("Interrupts enabled (STI)");
    
    kprintf("\n");
    vga_set_color(LIGHT_GREEN, BLACK);
    kprintf("Hello World!\n");
    
    while (1) {
        __asm__ volatile("hlt");
    }
}
