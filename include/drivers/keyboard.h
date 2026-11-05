/* NumOS — keyboard.h — PS/2 Keyboard */
#ifndef NUMOS_KEYBOARD_H
#define NUMOS_KEYBOARD_H

#include "libs/types.h"

void keyboard_init(void);
char keyboard_getchar(void);
bool keyboard_has_input(void);

#endif
