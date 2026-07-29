#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>
#include <stdarg.h>

void serial_init(void);
void serial_putc(char c);
void serial_puts(const char *s);
void serial_print_hex(uint64_t val);
void serial_print_dec(uint64_t val);
void serial_printf(const char *fmt, ...);
void serial_vprintf(const char *fmt, va_list args);

#endif
