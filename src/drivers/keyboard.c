/*
 * keyboard.c - PS/2 keyboard driver
 *
 * Arrow key fix
 * =============
 * Arrow keys send a two-byte scan code: 0xE0 (extended prefix) followed by
 * the actual key code (0x48=up, 0x50=down, 0x4B=left, 0x4D=right).  Each
 * byte triggers a SEPARATE IRQ.
 *
 * The old code called keyboard_read_scan_code() directly inside
 * vga_enter_scroll_mode().  But IRQ1 fires for the 0xE0 prefix byte and
 * keyboard_handler() consumes it from the hardware port before
 * keyboard_read_scan_code() can see it.  Result: scroll mode got the second
 * byte (0x48/0x50) out of context and ignored it.
 *
 * Fix: keyboard_handler() keeps a one-flag state machine.  When it sees
 * 0xE0, it sets extended_key_pending and returns.  On the next IRQ it reads
 * the real key code, maps it to KEY_SPECIAL_UP / KEY_SPECIAL_DOWN etc., and
 * puts that special char into the ring buffer.  vga_enter_scroll_mode() now
 * reads from the ring buffer (via keyboard_getchar_buffered) instead of
 * polling hardware, so it gets the correctly assembled virtual key code.
 */

#include "drivers/keyboard.h"
#include "kernel/kernel.h"

/* =========================================================================
 * Scan-code translation tables
 * ======================================================================= */

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

uint8_t shift_pressed = 0;
uint8_t ctrl_pressed  = 0;

/*
 * extended_key_pending: set when 0xE0 is received; cleared on the next IRQ
 * when the real extended key code arrives.
 */
static volatile uint8_t extended_key_pending = 0;
static volatile uint8_t special_up_pressed = 0;
static volatile uint8_t special_down_pressed = 0;
static volatile uint8_t special_left_pressed = 0;
static volatile uint8_t special_right_pressed = 0;

/* IRQ-filled ring buffer */
static volatile char   keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static volatile size_t buffer_head = 0;
static volatile size_t buffer_tail = 0;

/* =========================================================================
 * Helper: push one char into the ring buffer (called from IRQ context)
 * ======================================================================= */
static void buffer_push(char c) {
    size_t next = (buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != buffer_tail) {
        keyboard_buffer[buffer_head] = c;
        buffer_head = next;
    }
}

static void buffer_drop_char(char target) {
    size_t read = buffer_tail;
    size_t write = buffer_tail;
    while (read != buffer_head) {
        char c = keyboard_buffer[read];
        read = (read + 1) % KEYBOARD_BUFFER_SIZE;
        if (c == target) continue;
        keyboard_buffer[write] = c;
        write = (write + 1) % KEYBOARD_BUFFER_SIZE;
    }
    buffer_head = write;
}

static uint8_t *special_pressed_slot(uint8_t scan_code) {
    switch (scan_code) {
        case 0x48: return (uint8_t *)&special_up_pressed;
        case 0x50: return (uint8_t *)&special_down_pressed;
        case 0x4B: return (uint8_t *)&special_left_pressed;
        case 0x4D: return (uint8_t *)&special_right_pressed;
        default:   return NULL;
    }
}

static char special_char_for_scan(uint8_t scan_code) {
    switch (scan_code) {
        case 0x48: return KEY_SPECIAL_UP;
        case 0x50: return KEY_SPECIAL_DOWN;
        case 0x4B: return KEY_SPECIAL_LEFT;
        case 0x4D: return KEY_SPECIAL_RIGHT;
        default:   return 0;
    }
}

/* =========================================================================
 * Initialisation
 * ======================================================================= */

static int ps2_wait_input_clear(void) {
    for (int i = 0; i < 100000; i++)
        if (!(inb(KEYBOARD_STATUS_PORT) & 0x02)) return 1;
    return 0;
}

static int ps2_wait_output_full(void) {
    for (int i = 0; i < 100000; i++)
        if (inb(KEYBOARD_STATUS_PORT) & 0x01) return 1;
    return 0;
}

static void ps2_flush_output(void) {
    for (int i = 0; i < 64 && (inb(KEYBOARD_STATUS_PORT) & 0x01); i++) {
        (void)inb(KEYBOARD_DATA_PORT);
    }
}

static int ps2_write_cmd(uint8_t cmd) {
    if (!ps2_wait_input_clear()) return 0;
    outb(KEYBOARD_STATUS_PORT, cmd);
    return 1;
}

static int ps2_write_data(uint8_t data) {
    if (!ps2_wait_input_clear()) return 0;
    outb(KEYBOARD_DATA_PORT, data);
    return 1;
}

static int ps2_read_data(uint8_t *out) {
    if (!ps2_wait_output_full()) return 0;
    *out = inb(KEYBOARD_DATA_PORT);
    return 1;
}

static void ps2_send_kbd_cmd(uint8_t cmd) {
    uint8_t resp;
    if (!ps2_write_data(cmd)) return;
    if (ps2_read_data(&resp) && resp == 0xFA) return; /* ACK */
    ps2_flush_output();
}

void keyboard_init(void) {
    buffer_head          = 0;
    buffer_tail          = 0;
    shift_pressed        = 0;
    ctrl_pressed         = 0;
    extended_key_pending = 0;
    special_up_pressed   = 0;
    special_down_pressed = 0;
    special_left_pressed = 0;
    special_right_pressed = 0;

    /* Disable both PS/2 ports during controller programming. */
    (void)ps2_write_cmd(0xAD); /* disable keyboard */
    (void)ps2_write_cmd(0xA7); /* disable mouse    */

    ps2_flush_output();

    /* Read controller configuration byte. */
    uint8_t cfg = 0;
    int have_cfg = 0;
    if (ps2_write_cmd(0x20) && ps2_read_data(&cfg)) have_cfg = 1;

    /* Configuration byte bits:
     *   bit 0: IRQ1 enable
     *   bit 4: keyboard clock disable (clear to enable)
     *   bit 6: translation (set to translate set 2 -> set 1) */
    if (!have_cfg) cfg = 0;
    cfg |= 0x01;                 /* enable IRQ1 */
    cfg &= (uint8_t)~0x10;       /* enable keyboard clock */
    cfg |= 0x40;                 /* enable translation */

    if (ps2_write_cmd(0x60)) {
        (void)ps2_write_data(cfg);
    }

    ps2_flush_output();

    /* Re-enable keyboard port after configuration. */
    (void)ps2_write_cmd(0xAE);

    /* Keyboard: defaults and scanning on. */
    ps2_send_kbd_cmd(0xF6); /* set defaults */
    ps2_send_kbd_cmd(0xF4); /* enable scanning */
}

/* =========================================================================
 * Scan-code translation
 * ======================================================================= */

char scan_code_to_ascii(uint8_t scan_code) {
    if (scan_code >= KEY_RELEASE_OFFSET) {
        uint8_t key = scan_code - KEY_RELEASE_OFFSET;
        if (key == KEY_LSHIFT || key == KEY_RSHIFT) shift_pressed = 0;
        else if (key == KEY_LCTRL)                  ctrl_pressed  = 0;
        return 0;
    }
    if (scan_code == KEY_LSHIFT || scan_code == KEY_RSHIFT) { shift_pressed = 1; return 0; }
    if (scan_code == KEY_LCTRL)                              { ctrl_pressed  = 1; return 0; }
    return shift_pressed ? shifted_chars[scan_code] : scan_code_set1[scan_code];
}

/* =========================================================================
 * IRQ handler
 *
 * Two-state machine for extended (0xE0-prefixed) scan codes:
 *
 *   State 0 (normal):
 *     0xE0   → set extended_key_pending, return (wait for next IRQ)
 *     other  → translate normally, push into ring buffer
 *
 *   State 1 (extended_key_pending):
 *     0x48   → KEY_SPECIAL_UP   pushed into ring buffer
 *     0x50   → KEY_SPECIAL_DOWN
 *     0x4B   → KEY_SPECIAL_LEFT
 *     0x4D   → KEY_SPECIAL_RIGHT
 *     other  → ignored (release codes, etc.)
 *     always → clear extended_key_pending
 * ======================================================================= */
void keyboard_handler(void) {
    uint8_t scan_code = inb(KEYBOARD_DATA_PORT);

    /* Extended-key prefix: remember and wait for the actual key code */
    if (scan_code == 0xE0) {
        extended_key_pending = 1;
        return;
    }

    if (extended_key_pending) {
        extended_key_pending = 0;
        uint8_t code = (scan_code & 0x80) ? (scan_code - 0x80) : scan_code;
        uint8_t *pressed = special_pressed_slot(code);
        if (pressed) {
            if (scan_code & 0x80) {
                *pressed = 0;
            } else if (!*pressed) {
                *pressed = 1;
                char c = special_char_for_scan(code);
                if (c) buffer_push(c);
            }
        }
        return;
    }

    if (scan_code & 0x80) {
        uint8_t key = scan_code - KEY_RELEASE_OFFSET;
        if (key == KEY_BACKSPACE) buffer_drop_char('\b');
        else if (key == KEY_SPACE) buffer_drop_char(' ');
        (void)scan_code_to_ascii(scan_code);
        return;
    }

    /* Normal scan code */
    char ascii = scan_code_to_ascii(scan_code);
    if (ascii) buffer_push(ascii);
}

/* =========================================================================
 * Consumer-side reads
 * ======================================================================= */

/*
 * keyboard_getchar_buffered — block until a character arrives in the ring
 * buffer.  Used by syscall/scroll contexts; must NOT be called from IRQ.
 */
char keyboard_getchar_buffered(void) {
    while (buffer_head == buffer_tail)
        __asm__ volatile("sti; hlt; cli" ::: "memory");

    char c      = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;

    __asm__ volatile("sti" ::: "memory");
    return c;
}

/*
 * keyboard_getchar — kernel interactive use.
 * Blocks on the IRQ-driven ring buffer.
 *
 * This avoids races between direct port polling and the IRQ handler.
 * It also ensures extended (0xE0-prefixed) keys are assembled correctly.
 */
char keyboard_getchar(void) {
    return keyboard_getchar_buffered();
}

int keyboard_try_getchar(char *out) {
    if (!out) return 0;
    __asm__ volatile("cli");
    if (buffer_head == buffer_tail) {
        __asm__ volatile("sti");
        return 0;
    }
    char c = keyboard_buffer[buffer_tail];
    buffer_tail = (buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    __asm__ volatile("sti");
    *out = c;
    return 1;
}

void keyboard_flush_buffer(void) {
    __asm__ volatile("cli");
    buffer_tail = buffer_head;
    extended_key_pending = 0;
    __asm__ volatile("sti");
}

void keyboard_discard_pending(char target) {
    __asm__ volatile("cli");
    buffer_drop_char(target);
    __asm__ volatile("sti");
}

int keyboard_is_special_pressed(char target) {
    int pressed = 0;
    __asm__ volatile("cli");
    switch (target) {
        case KEY_SPECIAL_UP:    pressed = special_up_pressed; break;
        case KEY_SPECIAL_DOWN:  pressed = special_down_pressed; break;
        case KEY_SPECIAL_LEFT:  pressed = special_left_pressed; break;
        case KEY_SPECIAL_RIGHT: pressed = special_right_pressed; break;
        default:                pressed = 0; break;
    }
    __asm__ volatile("sti");
    return pressed;
}

/*
 * keyboard_read_line — read up to max_size-1 chars, echoing to VGA.
 */
