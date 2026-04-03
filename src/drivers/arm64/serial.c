#include "drivers/serial.h"
#include "kernel/arm64.h"

#define PL011_DR     0x000
#define PL011_FR     0x018
#define PL011_IBRD   0x024
#define PL011_FBRD   0x028
#define PL011_LCRH   0x02c
#define PL011_CR     0x030
#define PL011_IMSC   0x038
#define PL011_ICR    0x044

#define PL011_FR_RXFE (1u << 4)
#define PL011_FR_TXFF (1u << 5)
#define PL011_CR_UARTEN (1u << 0)
#define PL011_CR_TXE    (1u << 8)
#define PL011_CR_RXE    (1u << 9)
#define PL011_LCRH_FEN  (1u << 4)
#define PL011_LCRH_WLEN_8 (3u << 5)

static volatile uint32_t *const uart =
    (volatile uint32_t *)(uintptr_t)NUMOS_ARM64_QEMU_VIRT_UART0_BASE;
static int serial_ready = 0;

static inline void mmio_write(uint64_t offset, uint32_t value) {
    uart[offset / 4] = value;
}

static inline uint32_t mmio_read(uint64_t offset) {
    return uart[offset / 4];
}

void serial_init(void) {
    mmio_write(PL011_CR, 0);
    mmio_write(PL011_ICR, 0x7ff);
    mmio_write(PL011_IBRD, 13);
    mmio_write(PL011_FBRD, 1);
    mmio_write(PL011_LCRH, PL011_LCRH_FEN | PL011_LCRH_WLEN_8);
    mmio_write(PL011_IMSC, 0);
    mmio_write(PL011_CR, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
    serial_ready = 1;
}

void serial_putc(char c) {
    if (!serial_ready) serial_init();
    if (c == '\n') serial_putc('\r');
    while (mmio_read(PL011_FR) & PL011_FR_TXFF) {
    }
    mmio_write(PL011_DR, (uint32_t)(uint8_t)c);
}

void serial_write(const char *text) {
    if (!text) return;
    while (*text) {
        serial_putc(*text++);
    }
}

void serial_write_len(const char *text, size_t len) {
    if (!text) return;
    for (size_t i = 0; i < len; i++) {
        serial_putc(text[i]);
    }
}

int serial_try_getc(char *out) {
    if (!out) return 0;
    if (!serial_ready) serial_init();
    if (mmio_read(PL011_FR) & PL011_FR_RXFE) return 0;
    *out = (char)(mmio_read(PL011_DR) & 0xFFu);
    return 1;
}

char serial_getc(void) {
    char c = 0;
    while (!serial_try_getc(&c)) {
    }
    return c;
}
