#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>
#include "idt.h"

#define KEY_BACKSPACE 0x0E
#define KEY_ENTER     0x1C
#define KEY_LSHIFT    0x2A
#define KEY_RSHIFT    0x36
#define KEY_LCTRL     0x1D
#define KEY_LALT      0x38
#define KEY_CAPS      0x3A
#define KEY_F1        0x3B
#define KEY_F2        0x3C
#define KEY_F3        0x3D
#define KEY_F4        0x3E
#define KEY_UP        0x48
#define KEY_DOWN      0x50
#define KEY_LEFT      0x4B
#define KEY_RIGHT     0x4D
#define KEY_HOME      0x47
#define KEY_END       0x4F
#define KEY_PGUP      0x49
#define KEY_PGDN      0x51
#define KEY_DELETE    0x53

void keyboard_init(void);
int keyboard_getchar(void);
uint8_t keyboard_getscancode(void);
void keyboard_handler(isr_frame_t *frame);

extern uint8_t key_pressed[128];
extern int shift_pressed;
extern int ctrl_pressed;
extern int alt_pressed;
extern int caps_lock;

#endif
