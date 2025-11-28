#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "lib/base.h"

/* Keyboard ports */
#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_STATUS_PORT 0x64

/* Keyboard scan codes */
#define KEY_ENTER 0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_LSHIFT 0x2A
#define KEY_RSHIFT 0x36
#define KEY_LCTRL 0x1D

/* Key release offset */
#define KEY_RELEASE_OFFSET 0x80

/* Buffer size */
#define KEYBOARD_BUFFER_SIZE 256

/* Keyboard state flags */
extern uint8_t shift_pressed;
extern uint8_t ctrl_pressed;

/* Keyboard functions */
void keyboard_init(void);
uint8_t keyboard_read_scan_code(void);
char scan_code_to_ascii(uint8_t scan_code);
void keyboard_handler(void);
char keyboard_getchar(void);
void keyboard_read_line(char *buffer, size_t max_size);

#endif /* KEYBOARD_H */