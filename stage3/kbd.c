#include <stdint.h>
#include "kbd.h"
#include "console.h"

#define KBD_DATA  0x60
#define KBD_STAT  0x64
#define KBD_CMD   0x64

#define STAT_OBF  0x01

static int kbd_initialized = 0;

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

void kbd_init(void) {
    while (inb(KBD_STAT) & 0x02);
    __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xAD) : "memory");
    inb(KBD_DATA);
    __asm__ volatile("outb %0, $0x64" : : "a"((uint8_t)0xAE) : "memory");
    kbd_initialized = 1;
}

int kbd_is_key(void) {
    return inb(KBD_STAT) & STAT_OBF;
}

uint8_t kbd_get_scancode(void) {
    return inb(KBD_DATA);
}

static const char scancode_ascii[] = {
    0, 0, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', 0,
    0, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', 0,
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' '
};

char kbd_scancode_to_ascii(uint8_t sc) {
    if (sc < sizeof(scancode_ascii))
        return scancode_ascii[sc];
    return 0;
}

char kbd_wait_key(void) {
    while (1) {
        while (!kbd_is_key());
        uint8_t sc = kbd_get_scancode();
        if (sc & 0x80) continue;
        char c = kbd_scancode_to_ascii(sc);
        if (c) {
            cons_putc(c);
            return c;
        }
    }
}

void kbd_flush(void) {
    while (kbd_is_key()) kbd_get_scancode();
}
