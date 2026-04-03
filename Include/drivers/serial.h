#ifndef NUMOS_DRIVERS_SERIAL_H
#define NUMOS_DRIVERS_SERIAL_H

#include "lib/base.h"

void serial_init(void);
void serial_putc(char c);
void serial_write(const char *text);
void serial_write_len(const char *text, size_t len);
int serial_try_getc(char *out);
char serial_getc(void);

#endif /* NUMOS_DRIVERS_SERIAL_H */
