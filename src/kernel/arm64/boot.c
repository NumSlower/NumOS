#include "kernel/kernel.h"
#include "kernel/arm64.h"
#include "drivers/serial.h"
#include "cpu/idt.h"
#include "cpu/fpu.h"
#include "cpu/heap.h"
#include "drivers/timer.h"

static void __attribute__((no_stack_protector))
serial_write_hex64(uint64_t value) {
    static const char digits[] = "0123456789ABCDEF";
    serial_write("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(digits[(value >> shift) & 0xFu]);
    }
}

static void __attribute__((no_stack_protector))
serial_write_dec64(uint64_t value) {
    char buffer[21];
    int pos = 20;
    buffer[20] = '\0';

    if (value == 0) {
        serial_putc('0');
        return;
    }

    while (value > 0 && pos > 0) {
        buffer[--pos] = (char)('0' + (value % 10));
        value /= 10;
    }

    serial_write(&buffer[pos]);
}

static void __attribute__((no_stack_protector))
banner_hex_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_hex64(value);
    serial_putc('\n');
}

static void __attribute__((no_stack_protector))
banner_dec_line(const char *label, uint64_t value) {
    serial_write(label);
    serial_write_dec64(value);
    serial_putc('\n');
}

void arm64_boot_main(uint64_t arg0, uint64_t arg1, uint64_t arg2, uint64_t arg3) {
    (void)arg1;
    (void)arg2;
    (void)arg3;

    serial_init();
    serial_write("[1] serial ok\n");
    runtime_init();
    serial_write("[2] runtime ok\n");
    idt_init();
    serial_write("[3] vectors ok\n");
    fpu_init();
    serial_write("[4] fpu ok\n");
    heap_init();
    serial_write("[5] heap ok\n");
    timer_init(0);
    serial_write("[6] timer ok\n");

    serial_write("\nNumOS ARM64 bring up\n");
    serial_write("Target: QEMU virt\n");
    banner_hex_line("Boot arg: ", arg0);
    banner_hex_line("Core: ", arm64_core_id());
    banner_hex_line("EL: ", arm64_exception_level());
    banner_hex_line("CNTFRQ: ", arm64_counter_frequency());
    banner_dec_line("Uptime ms: ", timer_get_uptime_ms());
    serial_write("Status: serial path ready, MMU and user mode pending\n");

    for (;;) {
        __asm__ volatile("wfe");
    }
}
