/* NumOS — include/drivers/serial.h — UART 16550 serial driver header */
#ifndef NUMOS_DRIVERS_SERIAL_H
#define NUMOS_DRIVERS_SERIAL_H

#include "kernel/types.h"

bool serial_init(void);
void serial_putchar(char c);
void serial_write(const char* str, size_t len);
void serial_puts(const char* str);
char serial_read(void);
bool serial_received(void);
void serial_putchar_backend(char c);

#endif
