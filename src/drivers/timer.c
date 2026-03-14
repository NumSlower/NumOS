/*
 * timer.c - PIT (Programmable Interval Timer) driver
 *
 * Configures PIT channel 0 (IRQ 0) to fire at the requested frequency.
 * Every IRQ increments timer_ticks; uptime values are derived from that.
 *
 * Exported functions:
 *   timer_init()            - configure PIT and reset counters
 *   timer_handler()         - called from IRQ 0; updates ticks and uptime
 *   timer_get_ticks()       - raw tick count since init
 *   timer_get_uptime_ms()   - milliseconds since init
 *   timer_get_uptime_seconds() - seconds since init
 *   timer_sleep()           - busy-wait for at least ms milliseconds
 *   timer_delay_ms()        - alias for timer_sleep
 *   timer_register_callback()  - install a per-tick user callback
 *   timer_unregister_callback() - remove the user callback
 *   timer_get_stats()       - snapshot of timer statistics
 *   timer_set_frequency()   - reinitialise PIT at a new frequency
 *   timer_benchmark_start() - record a start tick
 *   timer_benchmark_end()   - return elapsed milliseconds since start
 */

#include "drivers/timer.h"
#include "drivers/vga.h"
#include "drivers/pic.h"
#include "lib/base.h"
#include "kernel/kernel.h"

/* =========================================================================
 * Module state
 * ======================================================================= */

static volatile uint64_t timer_ticks     = 0;               /* ticks since init */
static uint32_t          timer_frequency = TIMER_FREQ_100HZ; /* current Hz       */
static timer_callback_t  user_callback   = NULL;             /* optional hook    */
static struct timer_stats stats          = {0};              /* exported stats   */

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * timer_init - configure PIT channel 0 in rate-generator mode.
 *
 * divisor = PIT_FREQUENCY / frequency
 * The PIT decrements a 16-bit counter at 1.193182 MHz and fires IRQ 0
 * each time it reaches zero.
 */
void timer_init(uint32_t frequency) {
    timer_frequency   = frequency;
    stats.frequency   = frequency;

    uint32_t divisor = PIT_FREQUENCY / frequency;
    if (divisor < 1)     divisor = 1;
    if (divisor > 65535) divisor = 65535;

    /* Command: channel 0, access lo+hi bytes, mode 2 (rate generator), binary */
    outb(PIT_COMMAND,
         PIT_SELECT_CHANNEL0 | PIT_ACCESS_BOTH | PIT_MODE_2 | PIT_BINARY);

    outb(PIT_CHANNEL0_DATA, (uint8_t)(divisor & 0xFF));         /* low byte  */
    outb(PIT_CHANNEL0_DATA, (uint8_t)((divisor >> 8) & 0xFF));  /* high byte */

    /* Reset all counters */
    timer_ticks        = 0;
    stats.ticks        = 0;
    stats.seconds      = 0;
    stats.uptime_ms    = 0;

    vga_writestring("Timer initialized at ");
    print_dec(frequency);
    vga_writestring(" Hz\n");
}

/* =========================================================================
 * IRQ handler
 * ======================================================================= */

/*
 * timer_handler - invoked on every timer IRQ (before the scheduler tick).
 * Advances the tick counter, recomputes uptime, and calls the user callback.
 */
void timer_handler(void) {
    timer_ticks++;
    stats.ticks++;

    stats.uptime_ms = (timer_ticks * 1000) / timer_frequency;
    stats.seconds   = stats.uptime_ms / 1000;

    if (user_callback) {
        user_callback();
    }
}

/* =========================================================================
 * Time accessors
 * ======================================================================= */

uint64_t timer_get_ticks(void)           { return timer_ticks;        }
uint64_t timer_get_uptime_seconds(void)  { return stats.seconds;      }
uint64_t timer_get_uptime_ms(void)       { return stats.uptime_ms;    }

/* =========================================================================
 * Delay / sleep
 * ======================================================================= */

/*
 * timer_sleep - busy-wait by spinning on timer_ticks until ms milliseconds
 * have elapsed.  HLT keeps the CPU in a low-power state between ticks.
 */
void timer_sleep(uint32_t ms) {
    uint64_t target = timer_ticks + ((uint64_t)ms * timer_frequency) / 1000;
    while (timer_ticks < target) {
        __asm__ volatile("hlt");
    }
}

/*
 * timer_delay_ms - alias for timer_sleep; provided for driver convenience.
 */
void timer_delay_ms(uint32_t milliseconds) {
    timer_sleep(milliseconds);
}

/*
 * timer_delay_us - busy-spin for approximately microseconds microseconds.
 *
 * This is a calibrated busy loop rather than a PIT-channel-2 measurement.
 * Each iteration executes a NOP; the loop count is scaled from the PIT
 * frequency as a rough approximation.  Not suitable for high-precision work,
 * but accurate enough for hardware reset and initialization sequences.
 */
void timer_delay_us(uint32_t microseconds) {
    /* Approximate iterations per microsecond based on PIT base frequency.
     * PIT_FREQUENCY = 1193182 Hz; one tick ~ 0.838 us.
     * A conservative per-iteration cost of ~10 clock cycles at ~1 GHz
     * gives ~100 iterations per microsecond. Adjust ITERS_PER_US for
     * your target clock speed if tighter tolerance is needed. */
    const uint32_t ITERS_PER_US = 100;
    volatile uint32_t count = microseconds * ITERS_PER_US;
    while (count--) {
        __asm__ volatile("nop");
    }
}

/* =========================================================================
 * Callback registration
 * ======================================================================= */

/*
 * timer_register_callback - install a function called on every timer IRQ.
 * Only one callback is supported; a second call replaces the first.
 */
void timer_register_callback(timer_callback_t callback) {
    user_callback = callback;
}

/*
 * timer_unregister_callback - remove the current per-tick callback.
 */
void timer_unregister_callback(void) {
    user_callback = NULL;
}

/* =========================================================================
 * Statistics and configuration
 * ======================================================================= */

/*
 * timer_get_stats - return a snapshot of the current timer statistics.
 */
struct timer_stats timer_get_stats(void) {
    return stats;
}

/*
 * timer_set_frequency - reinitialise the PIT at a new frequency.
 * frequency must be between 1 Hz and PIT_FREQUENCY Hz.
 */
void timer_set_frequency(uint32_t frequency) {
    if (frequency < 1 || frequency > PIT_FREQUENCY) return;
    timer_init(frequency);
}

/* =========================================================================
 * Benchmarking helpers
 * ======================================================================= */

/*
 * timer_benchmark_start - return the current tick count as a start marker.
 */
uint64_t timer_benchmark_start(void) {
    return timer_ticks;
}

/*
 * timer_benchmark_end - return elapsed milliseconds since start_ticks.
 */
uint64_t timer_benchmark_end(uint64_t start_ticks) {
    uint64_t elapsed = timer_ticks - start_ticks;
    return (elapsed * 1000) / timer_frequency;
}