#include "console.h"
#include "kernel.h"

static uint16_t *video_mem = (uint16_t *)VGA_ADDR;
static int cursor_x = 0;
static int cursor_y = 0;
static uint8_t current_color = 0x0F;

static void update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

static void scroll(void) {
    if (cursor_y >= VGA_HEIGHT) {
        int lines = cursor_y - VGA_HEIGHT + 1;
        for (int i = 0; i < (VGA_HEIGHT - lines) * VGA_WIDTH; i++) {
            video_mem[i] = video_mem[i + lines * VGA_WIDTH];
        }
        for (int i = (VGA_HEIGHT - lines) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
            video_mem[i] = (current_color << 8) | ' ';
        }
        cursor_y = VGA_HEIGHT - 1;
    }
}

void console_init(void) {
    video_mem = (uint16_t *)VGA_ADDR;
    cursor_x = 0;
    cursor_y = 0;
    current_color = (CONSOLE_LIGHT_GREY) | (CONSOLE_BLACK << 4);
    console_clear();
}

void console_set_color(uint8_t fg, uint8_t bg) {
    current_color = fg | (bg << 4);
}

void console_clear(void) {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        video_mem[i] = (current_color << 8) | ' ';
    }
    cursor_x = 0;
    cursor_y = 0;
    update_cursor();
}

void console_set_cursor(int x, int y) {
    cursor_x = x;
    cursor_y = y;
    update_cursor();
}

int console_get_x(void) { return cursor_x; }
int console_get_y(void) { return cursor_y; }

void console_scroll(int lines) {
    for (int i = 0; i < (VGA_HEIGHT - lines) * VGA_WIDTH; i++) {
        video_mem[i] = video_mem[i + lines * VGA_WIDTH];
    }
    for (int i = (VGA_HEIGHT - lines) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++) {
        video_mem[i] = (current_color << 8) | ' ';
    }
    if (cursor_y >= lines) cursor_y -= lines;
    else cursor_y = 0;
    update_cursor();
}

static int (*redirect_hook)(char c) = 0;

void console_set_redirect(int (*hook)(char c)) {
    redirect_hook = hook;
}

void console_putc(char c, uint8_t color) {
    if (redirect_hook) {
        if (redirect_hook(c)) {
            return;
        }
    }
    switch (c) {
        case '\n':
            cursor_x = 0;
            cursor_y++;
            break;
        case '\r':
            cursor_x = 0;
            break;
        case '\t':
            cursor_x = (cursor_x + 8) & ~7;
            break;
        case '\b':
            if (cursor_x > 0) {
                cursor_x--;
                video_mem[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | ' ';
            }
            break;
        default:
            if (c >= 32) {
                video_mem[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | c;
                cursor_x++;
            }
            break;
    }
    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    scroll();
    update_cursor();
}

void console_puts(const char *s, uint8_t color) {
    while (*s) {
        console_putc(*s++, color);
    }
}

static void print_unsigned(uint64_t val, uint8_t color) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    do {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    console_puts(buf + i, color);
}

static void print_hex(uint64_t val, uint8_t color) {
    char buf[17];
    int i = 16;
    buf[i] = '\0';
    do {
        uint8_t nibble = val & 0xF;
        buf[--i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    } while (val > 0);
    console_puts(buf + i, color);
}

static void print_signed(int64_t val, uint8_t color) {
    if (val < 0) {
        console_putc('-', color);
        val = -val;
    }
    print_unsigned((uint64_t)val, color);
}

void console_vprintf(const char *fmt, va_list args) {
    uint8_t color = current_color;
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    console_puts(s, color);
                    break;
                }
                case 'd': {
                    int64_t v = __builtin_va_arg(args, int64_t);
                    print_signed(v, color);
                    break;
                }
                case 'u': {
                    uint64_t v = __builtin_va_arg(args, uint64_t);
                    print_unsigned(v, color);
                    break;
                }
                case 'x': {
                    uint64_t v = __builtin_va_arg(args, uint64_t);
                    print_hex(v, color);
                    break;
                }
                case 'c': {
                    int c = __builtin_va_arg(args, int);
                    console_putc((char)c, color);
                    break;
                }
                case '%':
                    console_putc('%', color);
                    break;
                default:
                    console_putc('%', color);
                    console_putc(*fmt, color);
                    break;
            }
        } else {
            console_putc(*fmt, color);
        }
        fmt++;
    }
}

void console_printf(const char *fmt, ...) {
    va_list args;
    __builtin_va_start(args, fmt);
    console_vprintf(fmt, args);
    __builtin_va_end(args);
}
