/*
 * timer.c - PIT (Programmable Interval Timer) driver
 *
 * Configures PIT channel 0 (IRQ 0) to fire at the requested frequency.
 * Every IRQ increments timer_ticks; uptime values are derived from that.
 *
 * Exported functions:
 *   timer_init()            - configure PIT and reset counters
 *   timer_handler()         - called from IRQ 0; updates ticks and uptime
 *   timer_get_uptime_ms()   - milliseconds since init
 */

#include "drivers/timer.h"
#include "drivers/network.h"
#include "drivers/rtc.h"
#include "drivers/graphices/vga.h"
#include "kernel/kernel.h"

#define NUMOS_MAX_TIMER_OBJECTS 32

struct timer_object {
    int      used;
    int      owner_pid;
    int32_t  id;
    uint32_t flags;
    uint64_t deadline_ms;
    uint64_t period_ms;
};

/* =========================================================================
 * Module state
 * ======================================================================= */

struct timer_stats {
    uint64_t ticks;
    uint64_t seconds;
    uint32_t frequency;
    uint64_t uptime_ms;
};

static volatile uint64_t timer_ticks     = 0;               /* ticks since init */
static uint32_t          timer_frequency = TIMER_FREQ_100HZ; /* current Hz       */
static struct timer_stats stats          = {0};              /* exported stats   */
static struct numos_calendar_time wall_clock = {0};
static uint64_t wall_clock_refresh_ms = 0;
static int32_t next_timer_id = 1;
static struct timer_object timer_objects[NUMOS_MAX_TIMER_OBJECTS];

static struct timer_object *timer_find_slot(int owner_pid, int timer_id) {
    for (int i = 0; i < NUMOS_MAX_TIMER_OBJECTS; i++) {
        if (!timer_objects[i].used) continue;
        if (timer_objects[i].owner_pid != owner_pid) continue;
        if (timer_objects[i].id != timer_id) continue;
        return &timer_objects[i];
    }
    return NULL;
}

static struct timer_object *timer_alloc_slot(void) {
    for (int i = 0; i < NUMOS_MAX_TIMER_OBJECTS; i++) {
        if (!timer_objects[i].used) return &timer_objects[i];
    }
    return NULL;
}

static uint64_t timer_compute_remaining(uint64_t deadline_ms, uint64_t now) {
    return (deadline_ms > now) ? (deadline_ms - now) : 0;
}

static void timer_fill_info(const struct timer_object *obj,
                            struct numos_timer_info *out) {
    if (!obj || !out) return;
    uint64_t now = timer_get_uptime_ms();
    out->id = obj->id;
    out->flags = obj->flags;
    out->deadline_ms = obj->deadline_ms;
    out->period_ms = obj->period_ms;
    out->remaining_ms = timer_compute_remaining(obj->deadline_ms, now);
}

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
    memset(timer_objects, 0, sizeof(timer_objects));
    memset(&wall_clock, 0, sizeof(wall_clock));
    wall_clock_refresh_ms = 0;
    next_timer_id = 1;
    timer_refresh_wall_clock();

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
    net_poll();

}

/* =========================================================================
 * Time accessors
 * ======================================================================= */

uint64_t timer_get_uptime_ms(void)       { return stats.uptime_ms;    }

void timer_refresh_wall_clock(void) {
    struct rtc_time rtc_now;
    if (rtc_read_time(&rtc_now) != 0) return;

    wall_clock.year = rtc_now.year;
    wall_clock.month = rtc_now.month;
    wall_clock.day = rtc_now.day;
    wall_clock.hour = rtc_now.hour;
    wall_clock.minute = rtc_now.minute;
    wall_clock.second = rtc_now.second;
    wall_clock.weekday = rtc_now.weekday;
    wall_clock.valid = 1;
    wall_clock.uptime_ms = timer_get_uptime_ms();
    wall_clock_refresh_ms = wall_clock.uptime_ms;
}

int timer_get_wall_clock(struct numos_calendar_time *out) {
    if (!out) return -1;

    uint64_t now = timer_get_uptime_ms();
    if (!wall_clock.valid ||
        now >= wall_clock_refresh_ms + 1000) {
        timer_refresh_wall_clock();
    }

    if (!wall_clock.valid) return -1;

    *out = wall_clock;
    out->uptime_ms = now;
    return 0;
}

int timer_create_object(int owner_pid, uint64_t delay_ms,
                        uint64_t period_ms, uint32_t flags) {
    if (owner_pid <= 0) return -1;
    if (flags & ~NUMOS_TIMER_PERIODIC) return -1;
    if ((flags & NUMOS_TIMER_PERIODIC) && period_ms == 0) return -1;
    if (!(flags & NUMOS_TIMER_PERIODIC)) period_ms = 0;

    struct timer_object *slot = timer_alloc_slot();
    if (!slot) return -1;

    uint64_t now = timer_get_uptime_ms();
    uint64_t first_deadline = now + delay_ms;
    if ((flags & NUMOS_TIMER_PERIODIC) && delay_ms == 0) {
        first_deadline = now + period_ms;
    }

    memset(slot, 0, sizeof(*slot));
    slot->used = 1;
    slot->owner_pid = owner_pid;
    slot->id = next_timer_id++;
    if (next_timer_id <= 0) next_timer_id = 1;
    slot->flags = flags;
    slot->deadline_ms = first_deadline;
    slot->period_ms = period_ms;
    return slot->id;
}

int timer_prepare_wait_object(int owner_pid, int timer_id, uint64_t *wake_ms) {
    if (!wake_ms) return -1;
    struct timer_object *slot = timer_find_slot(owner_pid, timer_id);
    if (!slot) return -1;
    *wake_ms = slot->deadline_ms;
    return 0;
}

int timer_complete_wait_object(int owner_pid, int timer_id) {
    struct timer_object *slot = timer_find_slot(owner_pid, timer_id);
    if (!slot) return -1;

    uint64_t now = timer_get_uptime_ms();
    if (slot->flags & NUMOS_TIMER_PERIODIC) {
        while (slot->deadline_ms <= now) {
            slot->deadline_ms += slot->period_ms;
        }
    } else {
        memset(slot, 0, sizeof(*slot));
    }

    return 0;
}

int timer_get_object_info(int owner_pid, int timer_id,
                          struct numos_timer_info *out) {
    if (!out) return -1;
    struct timer_object *slot = timer_find_slot(owner_pid, timer_id);
    if (!slot) return -1;
    timer_fill_info(slot, out);
    return 0;
}

int timer_cancel_object(int owner_pid, int timer_id) {
    struct timer_object *slot = timer_find_slot(owner_pid, timer_id);
    if (!slot) return -1;
    memset(slot, 0, sizeof(*slot));
    return 0;
}
