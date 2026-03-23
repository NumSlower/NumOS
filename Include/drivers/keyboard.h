#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "lib/base.h"

/* Keyboard ports */
#define KEYBOARD_DATA_PORT   0x60
#define KEYBOARD_STATUS_PORT 0x64

/* Standard scan codes */
#define KEY_ENTER     0x1C
#define KEY_BACKSPACE 0x0E
#define KEY_SPACE     0x39
#define KEY_LSHIFT    0x2A
#define KEY_RSHIFT    0x36
#define KEY_LCTRL     0x1D

/* Key release offset */
#define KEY_RELEASE_OFFSET 0x80

/* Buffer size */
#define KEYBOARD_BUFFER_SIZE 4096

/*
 * Special virtual key codes placed in the ring buffer by keyboard_handler()
 * when the 0xE0 extended-key prefix is seen.  These values are chosen in the
 * non-printable ASCII range so they cannot be confused with real characters.
 */
#define KEY_SPECIAL_UP    '\x01'   /* ↑ Arrow Up    */
#define KEY_SPECIAL_DOWN  '\x02'   /* ↓ Arrow Down  */
#define KEY_SPECIAL_LEFT  '\x03'   /* ← Arrow Left  */
#define KEY_SPECIAL_RIGHT '\x04'   /* → Arrow Right */

/* Keyboard state flags */
extern uint8_t shift_pressed;
extern uint8_t ctrl_pressed;

/* Public API */
void keyboard_init(void);
char scan_code_to_ascii(uint8_t scan_code);
void keyboard_handler(void);

char keyboard_getchar(void);           /* kernel interactive use (buffered) */
char keyboard_getchar_buffered(void);  /* syscall/scroll use (waits on IRQ) */
int keyboard_try_getchar(char *out);   /* non-blocking; returns 1 on char */
void keyboard_flush_buffer(void);      /* drop buffered key repeats */
void keyboard_discard_pending(char target);
int keyboard_is_special_pressed(char target);

#endif /* KEYBOARD_H */
