#include "drivers/keyboard.h"
#include "drivers/vga.h"

// Keyboard state
uint8_t shift_pressed = 0;
uint8_t ctrl_pressed = 0;

// Keyboard buffer - filled by IRQ handler, consumed by readers
static volatile char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile size_t buffer_head = 0;  // written by IRQ
static volatile size_t buffer_tail = 0;  // read by consumers

// Scan code to ASCII mapping
static char scan_code_set1[128] = {
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

// Shifted characters
static char shifted_chars[128] = {
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

void keyboard_init(void) {
    buffer_head = 0;
    buffer_tail = 0;
    shift_pressed = 0;
    ctrl_pressed = 0;
}

uint8_t keyboard_read_scan_code(void) {
    while (!(inb(KEYBOARD_STATUS_PORT) & 1)) {
        // Wait for keyboard data
    }
    return inb(KEYBOARD_DATA_PORT);
}

char scan_code_to_ascii(uint8_t scan_code) {
    if (scan_code >= 128) {
        uint8_t key = scan_code - KEY_RELEASE_OFFSET;
        if (key == KEY_LSHIFT || key == KEY_RSHIFT) shift_pressed = 0;
        else if (key == KEY_LCTRL) ctrl_pressed = 0;
        return 0;
    }

    if (scan_code == KEY_LSHIFT || scan_code == KEY_RSHIFT) {
        shift_pressed = 1;
        return 0;
    } else if (scan_code == KEY_LCTRL) {
        ctrl_pressed = 1;
        return 0;
    }

    return shift_pressed ? shifted_chars[scan_code] : scan_code_set1[scan_code];
}

/*
 * keyboard_handler - called from IRQ1.
 * Reads ONE scan code from the hardware port and places the ASCII
 * character in the ring buffer. Never called from syscall context.
 */
void keyboard_handler(void) {
    uint8_t scan_code = inb(KEYBOARD_DATA_PORT);  // read directly, don't block
    char ascii = scan_code_to_ascii(scan_code);

    if (ascii != 0) {
        size_t next_head = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
        if (next_head != buffer_tail) {
            keyboard_buffer[buffer_head] = ascii;
            buffer_head = next_head;
        }
    }
}

/*
 * keyboard_getchar_buffered - for use from syscall/userland context.
 * Blocks by HLT-looping until the IRQ handler places a char in the buffer.
 * Does NOT call keyboard_handler() or touch the hardware port itself.
 */
char keyboard_getchar_buffered(void) {
    while (buffer_head == buffer_tail) {
        __asm__ volatile("sti; hlt; cli" ::: "memory");
    }
    __asm__ volatile("sti" ::: "memory");

    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

/*
 * keyboard_getchar - for kernel interactive mode only.
 * Polls the hardware directly (used before a process is running,
 * e.g. in vga_enter_scroll_mode and the kmain interactive loop).
 * This is safe because those paths call keyboard_read_scan_code()
 * which also polls hardware - they never race with the IRQ in practice
 * since the kernel disables interrupts in those paths or the IRQ
 * has already deposited the char.
 *
 * NOTE: This still drains from the IRQ buffer first to avoid
 * losing chars that arrived via IRQ before we got here.
 */
char keyboard_getchar(void) {
    // Drain from IRQ buffer first
    if (buffer_head != buffer_tail) {
        char c = keyboard_buffer[buffer_tail];
        buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
        return c;
    }

    // Fall back to direct hardware poll
    uint8_t scan_code;
    char ascii = 0;
    while (!ascii) {
        scan_code = keyboard_read_scan_code();
        ascii = scan_code_to_ascii(scan_code);
    }
    return ascii;
}

void keyboard_read_line(char *buffer, size_t max_size) {
    size_t pos = 0;
    char c;

    while (pos < max_size - 1) {
        c = keyboard_getchar();

        if (c == '\n' || c == '\r') {
            buffer[pos] = '\0';
            vga_putchar('\n');
            break;
        } else if (c == '\b') {
            if (pos > 0) { pos--; vga_putchar('\b'); }
        } else if (c >= 32 && c <= 126) {
            buffer[pos++] = c;
            vga_putchar(c);
        }
    }

    if (pos >= max_size - 1) buffer[max_size - 1] = '\0';
<<<<<<< HEAD
}
=======
}
>>>>>>> 34c67df (Updating NumOS)
