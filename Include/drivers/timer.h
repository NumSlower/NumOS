#ifndef TIMER_H
#define TIMER_H

#include "lib/base.h"

#define NUMOS_TIMER_PERIODIC 0x01u

struct numos_calendar_time {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
    uint8_t  valid;
    uint64_t uptime_ms;
};

struct numos_timer_info {
    int32_t  id;
    uint32_t flags;
    uint64_t deadline_ms;
    uint64_t period_ms;
    uint64_t remaining_ms;
};

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

/* Function prototypes */
void timer_init(uint32_t frequency);
void timer_handler(void);
uint64_t timer_get_uptime_ms(void);
void timer_refresh_wall_clock(void);
int  timer_get_wall_clock(struct numos_calendar_time *out);
int  timer_create_object(int owner_pid, uint64_t delay_ms,
                         uint64_t period_ms, uint32_t flags);
int  timer_prepare_wait_object(int owner_pid, int timer_id,
                               uint64_t *wake_ms);
int  timer_complete_wait_object(int owner_pid, int timer_id);
int  timer_get_object_info(int owner_pid, int timer_id,
                           struct numos_timer_info *out);
int  timer_cancel_object(int owner_pid, int timer_id);

#endif /* TIMER_H */
