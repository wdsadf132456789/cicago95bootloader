#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>
#include <stdarg.h>

#define CONSOLE_BLACK        0x00
#define CONSOLE_BLUE         0x01
#define CONSOLE_GREEN        0x02
#define CONSOLE_CYAN         0x03
#define CONSOLE_RED          0x04
#define CONSOLE_MAGENTA      0x05
#define CONSOLE_BROWN        0x06
#define CONSOLE_LIGHT_GREY   0x07
#define CONSOLE_DARK_GREY    0x08
#define CONSOLE_LIGHT_BLUE   0x09
#define CONSOLE_LIGHT_GREEN  0x0A
#define CONSOLE_LIGHT_CYAN   0x0B
#define CONSOLE_LIGHT_RED    0x0C
#define CONSOLE_LIGHT_MAGENTA 0x0D
#define CONSOLE_YELLOW       0x0E
#define CONSOLE_WHITE        0x0F

#define VGA_WIDTH  80
#define VGA_HEIGHT 25
#define VGA_ADDR   0xB8000

void console_init(void);
void console_putc(char c, uint8_t color);
void console_puts(const char *s, uint8_t color);
void console_clear(void);
void console_set_color(uint8_t fg, uint8_t bg);
void console_scroll(int lines);
void console_set_cursor(int x, int y);
int console_get_x(void);
int console_get_y(void);
void console_printf(const char *fmt, ...);
void console_vprintf(const char *fmt, va_list args);

#endif
