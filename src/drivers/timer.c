#include "drivers/timer.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "lib/base.h"
#include "kernel/kernel.h"

/* Timer variables */
static volatile uint64_t timer_ticks = 0;
static uint32_t timer_frequency = TIMER_FREQ_100HZ;
static timer_callback_t user_callback = NULL;

/* Statistics */
static struct timer_stats stats = {0};

void timer_init(uint32_t frequency) {
    timer_frequency = frequency;
    stats.frequency = frequency;
    
    /* Calculate divisor for PIT */
    uint32_t divisor = PIT_FREQUENCY / frequency;
    
    /* Ensure divisor is within valid range */
    if (divisor < 1) divisor = 1;
    if (divisor > 65535) divisor = 65535;
    
    /* Send command to PIT */
    outb(PIT_COMMAND, PIT_SELECT_CHANNEL0 | PIT_ACCESS_BOTH | PIT_MODE_2 | PIT_BINARY);
    
    /* Send divisor */
    outb(PIT_CHANNEL0_DATA, divisor & 0xFF);        /* Low byte */
    outb(PIT_CHANNEL0_DATA, (divisor >> 8) & 0xFF); /* High byte */
    
    /* Reset counters */
    timer_ticks = 0;
    stats.ticks = 0;
    stats.seconds = 0;
    stats.uptime_ms = 0;
    
    vga_writestring("Timer initialized at ");
    print_dec(frequency);
    vga_writestring(" Hz\n");
}

void timer_handler(void) {
    timer_ticks++;
    stats.ticks++;
    
    /* Update uptime statistics */
    stats.uptime_ms = (timer_ticks * 1000) / timer_frequency;
    stats.seconds = stats.uptime_ms / 1000;
    
    /* Call user callback if registered */
    if (user_callback) {
        user_callback();
    }
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t timer_get_uptime_seconds(void) {
    return stats.seconds;
}

uint64_t timer_get_uptime_ms(void) {
    return stats.uptime_ms;
}

void timer_sleep(uint32_t ms) {
    uint64_t target_ticks = timer_ticks + (ms * timer_frequency) / 1000;
    
    while (timer_ticks < target_ticks) {
        __asm__ volatile("hlt"); /* Wait for interrupt */
    }
}

void timer_register_callback(timer_callback_t callback) {
    user_callback = callback;
}

void timer_unregister_callback(void) {
    user_callback = NULL;
}

struct timer_stats timer_get_stats(void) {
    return stats;
}

void timer_set_frequency(uint32_t frequency) {
    if (frequency < 1 || frequency > PIT_FREQUENCY) {
        return; /* Invalid frequency */
    }
    
    timer_init(frequency);
}

void timer_delay_us(uint32_t microseconds) {
    /* Use PIT for precise microsecond delays */
    uint32_t ticks = (PIT_FREQUENCY * microseconds) / 1000000;
    
    /* Set up PIT channel 2 for delay */
    outb(PIT_COMMAND, PIT_SELECT_CHANNEL2 | PIT_ACCESS_BOTH | PIT_MODE_0 | PIT_BINARY);
    outb(PIT_CHANNEL2_DATA, ticks & 0xFF);
    outb(PIT_CHANNEL2_DATA, (ticks >> 8) & 0xFF);
    
    /* Wait for countdown to complete */
    while (ticks-- > 0) {
        __asm__ volatile("nop");
    }
}

void timer_delay_ms(uint32_t milliseconds) {
    timer_sleep(milliseconds);
}

uint64_t timer_benchmark_start(void) {
    return timer_ticks;
}

uint64_t timer_benchmark_end(uint64_t start_ticks) {
    uint64_t end_ticks = timer_ticks;
    uint64_t elapsed_ticks = end_ticks - start_ticks;
    
    /* Convert to milliseconds */
    return (elapsed_ticks * 1000) / timer_frequency;
}