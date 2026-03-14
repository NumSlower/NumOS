/*
 * keyboard.c - PS/2 keyboard driver
 *
 * Provides two character-reading paths:
 *
 *   keyboard_handler()        - called from IRQ 1; reads one scan code from
 *                               the hardware data port and deposits the
 *                               translated ASCII character into the ring
 *                               buffer.  Never blocks.
 *
 *   keyboard_getchar_buffered() - for syscall/userland context; blocks via
 *                                 HLT until the IRQ handler places a character
 *                                 in the buffer.  Does NOT touch hardware.
 *
 *   keyboard_getchar()        - for kernel interactive mode (before processes
 *                               run).  Drains the IRQ buffer first; if empty,
 *                               falls back to polling the hardware directly.
 *
 *   keyboard_read_line()      - reads until newline or max_size-1 characters,
 *                               echoing each character to VGA.
 *
 * Scan code set 1 is assumed (standard PS/2 in compatibility mode).
 * Arrow keys and multi-byte sequences are handled in vga_enter_scroll_mode().
 */

#include "drivers/keyboard.h"
#include "drivers/vga.h"

/* =========================================================================
 * Scan-code translation tables
 * ======================================================================= */

/* Unshifted characters indexed by scan code (0-127) */
static const char scan_code_set1[128] = {
    0,    27,  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0,    'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0,    '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/',
    0,    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* Shifted characters indexed by scan code (0-127) */
static const char shifted_chars[128] = {
    0,    27,  '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0,    'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0,    '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?',
    0,    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0
};

/* =========================================================================
 * Module state
 * ======================================================================= */

/* Modifier key state - read by scan_code_to_ascii and by callers */
uint8_t shift_pressed = 0;
uint8_t ctrl_pressed  = 0;

/* IRQ-filled ring buffer consumed by keyboard_getchar_buffered() */
static volatile char   keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile size_t buffer_head = 0;   /* write index (IRQ context)   */
static volatile size_t buffer_tail = 0;   /* read index  (consumer side) */

/* =========================================================================
 * Initialisation
 * ======================================================================= */

/*
 * keyboard_init - reset modifier state and empty the ring buffer.
 */
void keyboard_init(void) {
    buffer_head   = 0;
    buffer_tail   = 0;
    shift_pressed = 0;
    ctrl_pressed  = 0;
}

/* =========================================================================
 * Scan-code translation
 * ======================================================================= */

/*
 * keyboard_read_scan_code - poll the status port until data is available,
 * then return the raw scan code byte from the data port.
 * Blocks until a byte is ready.
 */
uint8_t keyboard_read_scan_code(void) {
    while (!(inb(KEYBOARD_STATUS_PORT) & 1)) { /* wait for OBF bit */ }
    return inb(KEYBOARD_DATA_PORT);
}

/*
 * scan_code_to_ascii - translate a scan code byte to an ASCII character.
 *
 * Key-release codes (>= 0x80): update modifier state and return 0.
 * Key-press codes  (<  0x80): update modifiers or translate via table.
 *
 * Returns 0 for non-printable or modifier-only events.
 */
char scan_code_to_ascii(uint8_t scan_code) {
    /* Key-release event */
    if (scan_code >= KEY_RELEASE_OFFSET) {
        uint8_t key = scan_code - KEY_RELEASE_OFFSET;
        if (key == KEY_LSHIFT || key == KEY_RSHIFT) shift_pressed = 0;
        else if (key == KEY_LCTRL)                  ctrl_pressed  = 0;
        return 0;
    }

    /* Modifier key press */
    if (scan_code == KEY_LSHIFT || scan_code == KEY_RSHIFT) {
        shift_pressed = 1;
        return 0;
    }
    if (scan_code == KEY_LCTRL) {
        ctrl_pressed = 1;
        return 0;
    }

    return shift_pressed ? shifted_chars[scan_code]
                         : scan_code_set1[scan_code];
}

/* =========================================================================
 * IRQ handler
 * ======================================================================= */

/*
 * keyboard_handler - called from IRQ 1 context.
 *
 * Reads exactly one scan code directly from the hardware port (non-blocking,
 * the data is guaranteed present because the IRQ fired), translates it, and
 * places the character in the ring buffer.
 * Drops the character silently if the buffer is full.
 */
void keyboard_handler(void) {
    uint8_t scan_code = inb(KEYBOARD_DATA_PORT);
    char    ascii     = scan_code_to_ascii(scan_code);

    if (ascii != 0) {
        size_t next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
        if (next_head != buffer_tail) {
            keyboard_buffer[buffer_head] = ascii;
            buffer_head = next_head;
        }
    }
}

/* =========================================================================
 * Consumer-side reads
 * ======================================================================= */

/*
 * keyboard_getchar_buffered - block until a character arrives in the ring
 * buffer (placed there by keyboard_handler() via the IRQ).
 *
 * Uses "sti; hlt; cli" so the CPU sleeps between IRQs instead of spinning.
 * Safe to call from syscall context.  Must NOT be called from IRQ context.
 */
char keyboard_getchar_buffered(void) {
    while (buffer_head == buffer_tail) {
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    __asm__ volatile("sti" ::: "memory");

    char c      = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/*
 * keyboard_getchar - for kernel interactive use (pre-scheduler or
 * interactive-mode loops).
 *
 * Drains the IRQ buffer first to avoid losing characters that arrived via
 * interrupt.  If the buffer is empty, falls back to polling the hardware.
 */
char keyboard_getchar(void) {
    /* Drain IRQ buffer first */
    if (buffer_head != buffer_tail) {
        char c      = keyboard_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
        return c;
    }

    /* Direct hardware poll */
    uint8_t scan_code;
    char    ascii = 0;
    while (!ascii) {
        scan_code = keyboard_read_scan_code();
        ascii     = scan_code_to_ascii(scan_code);
    }
    return ascii;
}

/*
 * keyboard_read_line - read characters into buffer until newline or max_size-1
 * chars.  Handles backspace with VGA cursor feedback.  Null-terminates the
 * result.
 */
void keyboard_read_line(char *buffer, size_t max_size) {
    size_t pos = 0;
    char   c;

    while (pos < max_size - 1) {
        c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            vga_putchar('\n');
            return;
        }

        if (c == '\b') {
            if (pos > 0) { pos--; vga_putchar('\b'); }
            continue;
        }

        if (c >= 32 && c <= 126) {
            buffer[pos++] = c;
            vga_putchar(c);
        }
    }

    buffer[max_size - 1] = '\0';
}