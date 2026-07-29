#ifndef STAGE3_KBD_H
#define STAGE3_KBD_H

#include <stdint.h>

void kbd_init(void);
int  kbd_is_key(void);
uint8_t kbd_get_scancode(void);
char kbd_scancode_to_ascii(uint8_t sc);
char kbd_wait_key(void);
void kbd_flush(void);

#endif