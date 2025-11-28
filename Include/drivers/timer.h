#ifndef TIMER_H
#define TIMER_H

#include "lib/base.h"

/* PIT (Programmable Interval Timer) ports */
#define PIT_CHANNEL0_DATA   0x40
#define PIT_CHANNEL1_DATA   0x41
#define PIT_CHANNEL2_DATA   0x42
#define PIT_COMMAND         0x43

/* PIT frequency (approximately 1.193182 MHz) */
#define PIT_FREQUENCY       1193182

/* Timer frequencies */
#define TIMER_FREQ_1000HZ   1000  // 1ms ticks
#define TIMER_FREQ_100HZ    100   // 10ms ticks
#define TIMER_FREQ_50HZ     50    // 20ms ticks
#define TIMER_FREQ_18HZ     18    // ~55ms ticks (default)

/* PIT command register bits */
#define PIT_SELECT_CHANNEL0 0x00
#define PIT_SELECT_CHANNEL1 0x40
#define PIT_SELECT_CHANNEL2 0x80
#define PIT_ACCESS_LATCH    0x00
#define PIT_ACCESS_LOW      0x10
#define PIT_ACCESS_HIGH     0x20
#define PIT_ACCESS_BOTH     0x30
#define PIT_MODE_0          0x00  // Interrupt on terminal count
#define PIT_MODE_1          0x02  // Hardware retriggerable one-shot
#define PIT_MODE_2          0x04  // Rate generator
#define PIT_MODE_3          0x06  // Square wave generator
#define PIT_MODE_4          0x08  // Software triggered strobe
#define PIT_MODE_5          0x0A  // Hardware triggered strobe
#define PIT_BINARY          0x00
#define PIT_BCD             0x01

/* Timer callback function type */
typedef void (*timer_callback_t)(void);

/* Timer statistics */
struct timer_stats {
    uint64_t ticks;
    uint64_t seconds;
    uint32_t frequency;
    uint64_t uptime_ms;
};

/* Function prototypes */
void timer_init(uint32_t frequency);
void timer_handler(void);
uint64_t timer_get_ticks(void);
uint64_t timer_get_uptime_seconds(void);
uint64_t timer_get_uptime_ms(void);
void timer_sleep(uint32_t ms);
void timer_register_callback(timer_callback_t callback);
void timer_unregister_callback(void);
struct timer_stats timer_get_stats(void);
void timer_set_frequency(uint32_t frequency);

/* Delay functions */
void timer_delay_us(uint32_t microseconds);
void timer_delay_ms(uint32_t milliseconds);

/* Timer benchmarking */
uint64_t timer_benchmark_start(void);
uint64_t timer_benchmark_end(uint64_t start_ticks);

#endif /* TIMER_H */