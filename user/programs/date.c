#include "libc.h"

static void append_char(char *buf, size_t cap, size_t *len, char ch) {
    if (!buf || !len || cap == 0) return;
    if (*len + 1 >= cap) return;
    buf[*len] = ch;
    (*len)++;
    buf[*len] = '\0';
}

static void append_str(char *buf, size_t cap, size_t *len, const char *str) {
    if (!str) return;
    while (*str) {
        append_char(buf, cap, len, *str);
        str++;
    }
}

static void append_dec_fixed(char *buf, size_t cap, size_t *len,
                             uint64_t value, int width) {
    char tmp[20];
    for (int i = width - 1; i >= 0; i--) {
        tmp[i] = (char)('0' + (value % 10));
        value /= 10;
    }
    for (int i = 0; i < width; i++) {
        append_char(buf, cap, len, tmp[i]);
    }
}

static void append_dec(char *buf, size_t cap, size_t *len, uint64_t value) {
    char tmp[21];
    int pos = 20;
    tmp[20] = '\0';

    if (value == 0) {
        append_char(buf, cap, len, '0');
        return;
    }

    while (value > 0 && pos > 0) {
        tmp[--pos] = (char)('0' + (value % 10));
        value /= 10;
    }

    append_str(buf, cap, len, &tmp[pos]);
}

int main(void) {
    struct numos_calendar_time now;
    if (sys_time_read(&now) != 0 || !now.valid) {
        puts("date: rtc time unavailable");
        return 1;
    }

    char line[96];
    size_t len = 0;
    line[0] = '\0';

    append_dec_fixed(line, sizeof(line), &len, now.year, 4);
    append_char(line, sizeof(line), &len, '-');
    append_dec_fixed(line, sizeof(line), &len, now.month, 2);
    append_char(line, sizeof(line), &len, '-');
    append_dec_fixed(line, sizeof(line), &len, now.day, 2);
    append_char(line, sizeof(line), &len, ' ');
    append_dec_fixed(line, sizeof(line), &len, now.hour, 2);
    append_char(line, sizeof(line), &len, ':');
    append_dec_fixed(line, sizeof(line), &len, now.minute, 2);
    append_char(line, sizeof(line), &len, ':');
    append_dec_fixed(line, sizeof(line), &len, now.second, 2);
    append_str(line, sizeof(line), &len, " uptime_ms=");
    append_dec(line, sizeof(line), &len, now.uptime_ms);

    puts(line);
    return 0;
}
