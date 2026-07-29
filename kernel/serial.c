#include "serial.h"
#include "kernel.h"

#define COM1 0x3F8

void serial_init(void) {
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x80);
    outb(COM1 + 0, 0x03);
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03);
    outb(COM1 + 2, 0xC7);
    outb(COM1 + 4, 0x0B);
}

void serial_putc(char c) {
    while (!(inb(COM1 + 5) & 0x20));
    outb(COM1, c);
}

void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n') serial_putc('\r');
        serial_putc(*s++);
    }
}

void serial_print_hex(uint64_t val) {
    serial_puts("0x");
    for (int i = 15; i >= 0; i--) {
        uint8_t nibble = (val >> (i * 4)) & 0xF;
        serial_putc(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
    }
}

void serial_print_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    do {
        buf[--i] = '0' + (val % 10);
        val /= 10;
    } while (val > 0);
    serial_puts(buf + i);
}

static void serial_print_unsigned(uint64_t val) {
    serial_print_dec(val);
}

static void serial_print_signed(int64_t val) {
    if (val < 0) {
        serial_putc('-');
        val = -val;
    }
    serial_print_unsigned((uint64_t)val);
}

static void serial_print_hex_val(uint64_t val) {
    char buf[17];
    int i = 16;
    buf[i] = '\0';
    do {
        uint8_t nibble = val & 0xF;
        buf[--i] = nibble < 10 ? '0' + nibble : 'a' + nibble - 10;
        val >>= 4;
    } while (val > 0);
    serial_puts(buf + i);
}

void serial_vprintf(const char *fmt, va_list args) {
    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    serial_puts(s);
                    break;
                }
                case 'd': {
                    int64_t v = __builtin_va_arg(args, int64_t);
                    serial_print_signed(v);
                    break;
                }
                case 'u': {
                    uint64_t v = __builtin_va_arg(args, uint64_t);
                    serial_print_unsigned(v);
                    break;
                }
                case 'x': {
                    uint64_t v = __builtin_va_arg(args, uint64_t);
                    serial_print_hex_val(v);
                    break;
                }
                case 'c': {
                    int c = __builtin_va_arg(args, int);
                    serial_putc((char)c);
                    break;
                }
                case '%':
                    serial_putc('%');
                    break;
                default:
                    serial_putc('%');
                    serial_putc(*fmt);
                    break;
            }
        } else {
            serial_putc(*fmt);
        }
        fmt++;
    }
}

void serial_printf(const char *fmt, ...) {
    va_list args;
    __builtin_va_start(args, fmt);
    serial_vprintf(fmt, args);
    __builtin_va_end(args);
}
