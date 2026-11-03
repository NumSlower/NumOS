/* NumOS — src/drivers/serial.c — UART 16550 serial driver implementation */
#include "drivers/serial.h"
#include "kernel/io.h"

#define COM1 0x3F8

bool serial_init(void) {
    outb(COM1 + 1, 0x00);    /* Disable all interrupts */
    outb(COM1 + 3, 0x80);    /* Enable DLAB (set baud rate divisor) */
    outb(COM1 + 0, 0x01);    /* Set divisor to 1 (lo byte) 115200 baud */
    outb(COM1 + 1, 0x00);    /*                  (hi byte) */
    outb(COM1 + 3, 0x03);    /* 8 bits, no parity, one stop bit */
    outb(COM1 + 2, 0xC7);    /* Enable FIFO, clear them, with 14-byte threshold */
    outb(COM1 + 4, 0x0B);    /* IRQs enabled, RTS/DSR set */
    outb(COM1 + 4, 0x1E);    /* Set in loopback mode, test the serial chip */
    
    outb(COM1 + 0, 0xAE);    /* Test serial chip (send byte 0xAE and check if serial returns same byte) */
    if (inb(COM1 + 0) != 0xAE) {
        return false;
    }
    
    outb(COM1 + 4, 0x0F);    /* If serial is not faulty set it in normal operation mode: not-loopback with IRQs enabled and OUT#1 and OUT#2 bits enabled */
    return true;
}

void serial_putchar(char c) {
    if (c == '\n') {
        while ((inb(COM1 + 5) & 0x20) == 0);
        outb(COM1, '\r');
    }
    while ((inb(COM1 + 5) & 0x20) == 0);
    outb(COM1, c);
}

void serial_write(const char* str, size_t len) {
    for (size_t i = 0; i < len; i++) {
        serial_putchar(str[i]);
    }
}

void serial_puts(const char* str) {
    while (*str) {
        serial_putchar(*str++);
    }
}

bool serial_received(void) {
    return (inb(COM1 + 5) & 1) != 0;
}

char serial_read(void) {
    while (!serial_received());
    return inb(COM1);
}

void serial_putchar_backend(char c) {
    serial_putchar(c);
}
