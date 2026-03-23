#include "drivers/rtc.h"
#include "kernel/kernel.h"

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71
#define CMOS_NMI_DISABLE  0x80

#define RTC_REG_SECONDS   0x00
#define RTC_REG_MINUTES   0x02
#define RTC_REG_HOURS     0x04
#define RTC_REG_WEEKDAY   0x06
#define RTC_REG_DAY       0x07
#define RTC_REG_MONTH     0x08
#define RTC_REG_YEAR      0x09
#define RTC_REG_STATUS_A  0x0A
#define RTC_REG_STATUS_B  0x0B
#define RTC_REG_CENTURY   0x32

static uint8_t rtc_read_reg(uint8_t reg) {
    outb(CMOS_ADDRESS_PORT, (uint8_t)(CMOS_NMI_DISABLE | reg));
    return inb(CMOS_DATA_PORT);
}

static int rtc_update_in_progress(void) {
    return (rtc_read_reg(RTC_REG_STATUS_A) & 0x80) != 0;
}

static uint8_t rtc_decode_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

static void rtc_read_raw(struct rtc_time *out, uint8_t *status_b,
                         uint8_t *century) {
    out->second  = rtc_read_reg(RTC_REG_SECONDS);
    out->minute  = rtc_read_reg(RTC_REG_MINUTES);
    out->hour    = rtc_read_reg(RTC_REG_HOURS);
    out->weekday = rtc_read_reg(RTC_REG_WEEKDAY);
    out->day     = rtc_read_reg(RTC_REG_DAY);
    out->month   = rtc_read_reg(RTC_REG_MONTH);
    out->year    = rtc_read_reg(RTC_REG_YEAR);
    if (status_b) *status_b = rtc_read_reg(RTC_REG_STATUS_B);
    if (century)  *century  = rtc_read_reg(RTC_REG_CENTURY);
}

static int rtc_same_sample(const struct rtc_time *a, const struct rtc_time *b) {
    return a->second  == b->second  &&
           a->minute  == b->minute  &&
           a->hour    == b->hour    &&
           a->weekday == b->weekday &&
           a->day     == b->day     &&
           a->month   == b->month   &&
           a->year    == b->year;
}

int rtc_read_time(struct rtc_time *out) {
    if (!out) return -1;

    struct rtc_time first;
    struct rtc_time second;
    uint8_t status_b = 0;
    uint8_t century  = 0;
    uint8_t status_b_second = 0;
    uint8_t century_second  = 0;

    for (int attempt = 0; attempt < 5; attempt++) {
        while (rtc_update_in_progress()) { }
        rtc_read_raw(&first, &status_b, &century);
        while (rtc_update_in_progress()) { }
        rtc_read_raw(&second, &status_b_second, &century_second);

        if (!rtc_same_sample(&first, &second) ||
            status_b != status_b_second || century != century_second) {
            continue;
        }

        int binary_mode = (status_b & 0x04) != 0;
        int hour_24     = (status_b & 0x02) != 0;

        if (!binary_mode) {
            first.second  = rtc_decode_bcd(first.second);
            first.minute  = rtc_decode_bcd(first.minute);
            first.weekday = rtc_decode_bcd(first.weekday);
            first.day     = rtc_decode_bcd(first.day);
            first.month   = rtc_decode_bcd(first.month);
            first.year    = rtc_decode_bcd((uint8_t)first.year);
            century       = rtc_decode_bcd(century);

            uint8_t pm = first.hour & 0x80;
            first.hour = rtc_decode_bcd((uint8_t)(first.hour & 0x7F));
            if (!hour_24 && pm) first.hour = (uint8_t)((first.hour % 12) + 12);
            if (!hour_24 && !pm && first.hour == 12) first.hour = 0;
        } else if (!hour_24) {
            uint8_t pm = first.hour & 0x80;
            first.hour &= 0x7F;
            if (pm) first.hour = (uint8_t)((first.hour % 12) + 12);
            if (!pm && first.hour == 12) first.hour = 0;
        }

        if (century == 0) {
            first.year = (first.year < 70)
                       ? (uint16_t)(2000 + first.year)
                       : (uint16_t)(1900 + first.year);
        } else {
            first.year = (uint16_t)(century * 100u + first.year);
        }

        *out = first;
        return 0;
    }

    return -1;
}
