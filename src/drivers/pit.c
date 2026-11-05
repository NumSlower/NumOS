/* NumOS — pit.c — Programmable Interval Timer (PIT) */
#include "drivers/pit.h"
#include "libs/io.h"
#include "cpu/irq.h"
#include "libs/kprintf.h"

#define PIT_CHANNEL0 0x40
#define PIT_CMD      0x43
#define PIT_BASE_FREQ 1193182

static volatile uint64_t pit_ticks = 0;

static void pit_handler(registers_t* regs) {
    (void)regs;
    pit_ticks++;
}

void pit_init(uint32_t freq_hz) {
    uint32_t divisor = PIT_BASE_FREQ / freq_hz;
    if (divisor > 65535) divisor = 65535;
    if (divisor < 1) divisor = 1;

    outb(PIT_CMD, 0x36);
    outb(PIT_CHANNEL0, (uint8_t)(divisor & 0xFF));
    outb(PIT_CHANNEL0, (uint8_t)((divisor >> 8) & 0xFF));

    irq_register_handler(0, pit_handler);
}

uint64_t pit_get_ticks(void) {
    return pit_ticks;
}

void pit_sleep_ms(uint64_t ms) {
    uint64_t start = pit_ticks;
    while (pit_ticks < start + ms) {
        // busy wait
        __asm__ volatile("pause");
    }
}
