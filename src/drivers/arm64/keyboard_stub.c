#include "drivers/keyboard.h"

uint8_t shift_pressed = 0;
uint8_t ctrl_pressed = 0;

void keyboard_init(void) {
}

char scan_code_to_ascii(uint8_t scan_code) {
    (void)scan_code;
    return 0;
}

void keyboard_handler(void) {
}

char keyboard_getchar(void) {
    return 0;
}

char keyboard_getchar_buffered(void) {
    return 0;
}

int keyboard_try_getchar(char *out) {
    (void)out;
    return 0;
}

void keyboard_flush_buffer(void) {
}

void keyboard_discard_pending(char target) {
    (void)target;
}

int keyboard_is_special_pressed(char target) {
    (void)target;
    return 0;
}
