#include "drivers/timer.h"
#include "kernel/arm64.h"

static uint64_t timer_frequency = 0;

void timer_init(uint32_t frequency) {
    (void)frequency;
    timer_frequency = arm64_counter_frequency();
}

void timer_handler(void) {
}

uint64_t timer_get_uptime_ms(void) {
    if (timer_frequency == 0) timer_frequency = arm64_counter_frequency();
    if (timer_frequency == 0) return 0;
    return (arm64_counter_value() * 1000ULL) / timer_frequency;
}

void timer_refresh_wall_clock(void) {
}

int timer_get_wall_clock(struct numos_calendar_time *out) {
    if (!out) return -1;
    out->year = 0;
    out->month = 0;
    out->day = 0;
    out->hour = 0;
    out->minute = 0;
    out->second = 0;
    out->weekday = 0;
    out->valid = 0;
    out->uptime_ms = timer_get_uptime_ms();
    return 0;
}

int timer_create_object(int owner_pid, uint64_t delay_ms,
                        uint64_t period_ms, uint32_t flags) {
    (void)owner_pid;
    (void)delay_ms;
    (void)period_ms;
    (void)flags;
    return -1;
}

int timer_prepare_wait_object(int owner_pid, int timer_id, uint64_t *wake_ms) {
    (void)owner_pid;
    (void)timer_id;
    (void)wake_ms;
    return -1;
}

int timer_complete_wait_object(int owner_pid, int timer_id) {
    (void)owner_pid;
    (void)timer_id;
    return -1;
}

int timer_get_object_info(int owner_pid, int timer_id,
                          struct numos_timer_info *out) {
    (void)owner_pid;
    (void)timer_id;
    (void)out;
    return -1;
}

int timer_cancel_object(int owner_pid, int timer_id) {
    (void)owner_pid;
    (void)timer_id;
    return -1;
}
