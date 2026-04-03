#include "kernel/kernel.h"
#include "kernel/arm64.h"
#include "drivers/serial.h"
#include "cpu/idt.h"
#include "cpu/fpu.h"
#include "cpu/heap.h"
#include "drivers/timer.h"

static void banner_line(const char *label, uint64_t value) {
    serial_write(label);
    print_hex(value);
    serial_putc('\n');
}

void arm64_boot_main(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (void)arg1;
    (void)arg2;
    (void)arg3;

    serial_init();
    runtime_init();
    idt_init();
    fpu_init();
    heap_init();
    timer_init(0);

    serial_write("\nNumOS ARM64 bring up\n");
    serial_write("Target: QEMU virt\n");
    banner_line("Boot arg: ", arg0);
    banner_line("Core: ", arm64_core_id());
    banner_line("EL: ", arm64_exception_level());
    banner_line("CNTFRQ: ", arm64_counter_frequency());
    banner_line("Uptime ms: ", timer_get_uptime_ms());
    serial_write("Status: serial path ready, MMU and user mode pending\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
