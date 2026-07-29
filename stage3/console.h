#ifndef STAGE3_CONSOLE_H
#define STAGE3_CONSOLE_H

#include <stdint.h>
#include "vga/vga.h"

#define COL_DEFAULT VGA_COLOR(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK)
#define COL_OK      VGA_COLOR(VGA_COLOR_GREEN, VGA_COLOR_BLACK)
#define COL_WARN    VGA_COLOR(VGA_COLOR_YELLOW, VGA_COLOR_BLACK)
#define COL_ERR     VGA_COLOR(VGA_COLOR_RED, VGA_COLOR_BLACK)
#define COL_HDR     VGA_COLOR(VGA_COLOR_CYAN, VGA_COLOR_BLACK)
#define COL_LABEL   VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK)
#define COL_DIM     VGA_COLOR(VGA_COLOR_DARK_GREY, VGA_COLOR_BLACK)
#define COL_HI      VGA_COLOR(VGA_COLOR_LIGHT_MAGENTA, VGA_COLOR_BLACK)

void cons_init(void);
void cons_puts(const char *s);
void cons_putc(char c);
void cons_color(const char *s, uint8_t color);
void cons_hex32(uint32_t val);
void cons_hex64(uint64_t val);
void cons_dec32(uint32_t val);
void cons_set_cursor(uint8_t row, uint8_t col);
void cons_bar(uint32_t val, uint32_t max, int width);
void cons_clear_line(uint8_t row);

#endif