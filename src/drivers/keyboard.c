/* NumOS — keyboard.c — PS/2 Keyboard */
#include "drivers/keyboard.h"
#include "libs/io.h"
#include "cpu/irq.h"

static volatile char key_buffer[256];
static volatile uint8_t head = 0;
static volatile uint8_t tail = 0;

static bool shift_pressed = false;
static bool ctrl_pressed = false;
static bool caps_lock = false;

static const char scancode_std[] = {
    0, 27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static const char scancode_shift[] = {
    0, 27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '7', '8', '9', '-', '4', '5', '6', '+', '1', '2', '3', '0', '.',
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

static void keyboard_handler(registers_t* regs) {
    (void)regs;
    uint8_t scancode = inb(0x60);
    
    if (scancode == 0xE0) return; // ignore extended
    
    bool release = scancode & 0x80;
    scancode &= 0x7F;
    
    if (scancode == 0x2A || scancode == 0x36) {
        shift_pressed = !release;
    } else if (scancode == 0x1D) {
        ctrl_pressed = !release;
    } else if (scancode == 0x3A && !release) {
        caps_lock = !caps_lock;
    } else if (!release) {
        char c = 0;
        if (scancode < sizeof(scancode_std)) {
            bool use_shift = shift_pressed ^ caps_lock;
            if (scancode_std[scancode] >= 'a' && scancode_std[scancode] <= 'z') {
                c = use_shift ? scancode_shift[scancode] : scancode_std[scancode];
            } else {
                c = shift_pressed ? scancode_shift[scancode] : scancode_std[scancode];
            }
        }
        if (c) {
            uint8_t next_head = (head + 1) % 256;
            if (next_head != tail) {
                key_buffer[head] = c;
                head = next_head;
            }
        }
    }
}

void keyboard_init(void) {
    while (inb(0x64) & 1) inb(0x60); // flush
    irq_register_handler(1, keyboard_handler);
}

char keyboard_getchar(void) {
    while (!keyboard_has_input()) {
        __asm__ volatile("hlt");
    }
    char c = key_buffer[tail];
    tail = (tail + 1) % 256;
    return c;
}

bool keyboard_has_input(void) {
    return head != tail;
}
