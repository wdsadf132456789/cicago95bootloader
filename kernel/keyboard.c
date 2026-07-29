#include "keyboard.h"
#include "idt.h"
#include "kernel.h"

uint8_t key_pressed[128] = {0};
int shift_pressed = 0;
int ctrl_pressed = 0;
int alt_pressed = 0;
int caps_lock = 0;

static uint8_t scancode_buffer[256];
static volatile int scancode_head = 0;
static volatile int scancode_tail = 0;

static const char sc1_no_shift[128] = {
    0, 27, '1','2','3','4','5','6','7','8','9','0','-','=','\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',
    0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0
};

static const char sc1_shift[128] = {
    0, 27, '!','@','#','$','%','^','&','*','(',')','_','+','\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',
    0,'*',0,' ',0,
    0,0,0,0,0,0,0,0,0,0,
    '7','8','9','-','4','5','6','+','1','2','3','0','.',
    0,0,0,0,0
};

static void keyboard_leds(void) {
    uint8_t led = 0;
    if (caps_lock) led |= 4;
    outb(0x60, 0xED);
    io_wait();
    outb(0x60, led);
}

void keyboard_init(void) {
    while (inb(0x64) & 1) inb(0x60);
    outb(0x64, 0xF0);
    io_wait();
    outb(0x60, 0x02);
    keyboard_leds();
}

static int extended_prefix = 0;

void keyboard_handler(isr_frame_t *frame) {
    (void)frame;
    uint8_t scancode = inb(0x60);

    if (scancode == 0xE0) {
        extended_prefix = 1;
        return;
    }

    if (extended_prefix) {
        extended_prefix = 0;
        /* Extended scancodes: 0xE0 prefix + actual scancode */
        if (scancode & 0x80) {
            /* Key release for extended key */
            return;
        }
        /* Map extended scancodes to our internal key codes */
        uint8_t mapped = 0;
        switch (scancode) {
            case 0x48: mapped = KEY_UP; break;
            case 0x50: mapped = KEY_DOWN; break;
            case 0x4B: mapped = KEY_LEFT; break;
            case 0x4D: mapped = KEY_RIGHT; break;
            case 0x47: mapped = KEY_HOME; break;
            case 0x4F: mapped = KEY_END; break;
            case 0x49: mapped = KEY_PGUP; break;
            case 0x51: mapped = KEY_PGDN; break;
            case 0x53: mapped = KEY_DELETE; break;
            default: return;
        }
        key_pressed[mapped] = 1;
        scancode_buffer[scancode_tail] = mapped;
        scancode_tail = (scancode_tail + 1) & 0xFF;
        return;
    }

    if (scancode & 0x80) {
        uint8_t key = scancode & 0x7F;
        if (key < 128) key_pressed[key] = 0;
        if (key == KEY_LSHIFT || key == KEY_RSHIFT) shift_pressed = 0;
        if (key == KEY_LCTRL) ctrl_pressed = 0;
        if (key == KEY_LALT) alt_pressed = 0;
    } else {
        uint8_t key = scancode;
        key_pressed[key] = 1;
        if (key == KEY_LSHIFT || key == KEY_RSHIFT) shift_pressed = 1;
        if (key == KEY_LCTRL) ctrl_pressed = 1;
        if (key == KEY_LALT) alt_pressed = 1;
        if (key == KEY_CAPS) {
            caps_lock = !caps_lock;
            keyboard_leds();
        }
        if (key < 128) {
            scancode_buffer[scancode_tail] = scancode;
            scancode_tail = (scancode_tail + 1) & 0xFF;
        }
    }
}

int keyboard_getchar(void) {
    if (scancode_head == scancode_tail) return -1;
    uint8_t sc = scancode_buffer[scancode_head];
    scancode_head = (scancode_head + 1) & 0xFF;

    /* Extended keys (arrows, delete, etc) are already mapped to their codes */
    if (sc == KEY_UP || sc == KEY_DOWN || sc == KEY_LEFT || sc == KEY_RIGHT ||
        sc == KEY_HOME || sc == KEY_END || sc == KEY_PGUP || sc == KEY_PGDN ||
        sc == KEY_DELETE) {
        return sc;
    }

    int shift = shift_pressed;
    if (caps_lock && sc >= 0x10 && sc <= 0x32) shift = !shift;
    char c = shift ? sc1_shift[sc] : sc1_no_shift[sc];
    if (ctrl_pressed && c >= 'a' && c <= 'z') c = c - 'a' + 1;
    return c;
}

uint8_t keyboard_getscancode(void) {
    if (scancode_head == scancode_tail) return 0;
    uint8_t sc = scancode_buffer[scancode_head];
    scancode_head = (scancode_head + 1) & 0xFF;
    return sc;
}
