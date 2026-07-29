#include "console.h"

static uint16_t *vga_buf = (uint16_t *)VGA_TEXT_ADDR;
static uint8_t vga_row = 0;
static uint8_t vga_col = 0;

void cons_init(void) {
    vga_row = 0;
    vga_col = 0;
}

void cons_set_cursor(uint8_t row, uint8_t col) {
    if (row < VGA_TEXT_ROWS) vga_row = row;
    if (col < VGA_TEXT_COLS) vga_col = col;
}

void cons_clear_line(uint8_t row) {
    if (row >= VGA_TEXT_ROWS) return;
    uint16_t blank = (uint16_t)COL_DEFAULT << 8 | ' ';
    for (int c = 0; c < VGA_TEXT_COLS; c++)
        vga_buf[row * VGA_TEXT_COLS + c] = blank;
}

void cons_putc(char c) {
    uint8_t color = COL_DEFAULT;
    if (c == '\n') {
        vga_row++;
        vga_col = 0;
        if (vga_row >= VGA_TEXT_ROWS) {
            for (int r = 1; r < VGA_TEXT_ROWS; r++)
                for (int cc = 0; cc < VGA_TEXT_COLS; cc++)
                    vga_buf[(r-1)*VGA_TEXT_COLS+cc] = vga_buf[r*VGA_TEXT_COLS+cc];
            for (int cc = 0; cc < VGA_TEXT_COLS; cc++)
                vga_buf[(VGA_TEXT_ROWS-1)*VGA_TEXT_COLS+cc] = (uint16_t)COL_DEFAULT << 8 | ' ';
            vga_row = VGA_TEXT_ROWS - 1;
        }
        return;
    }
    if (vga_col >= VGA_TEXT_COLS) { vga_row++; vga_col = 0; }
    if (vga_row >= VGA_TEXT_ROWS) vga_row = VGA_TEXT_ROWS - 1;
    vga_buf[vga_row * VGA_TEXT_COLS + vga_col++] = (uint16_t)color << 8 | (uint8_t)c;
}

void cons_puts(const char *s) {
    while (*s) cons_putc(*s++);
}

void cons_color(const char *s, uint8_t color) {
    while (*s) {
        char c = *s++;
        if (c == '\n') {
            vga_row++;
            vga_col = 0;
            if (vga_row >= VGA_TEXT_ROWS) {
                for (int r = 1; r < VGA_TEXT_ROWS; r++)
                    for (int cc = 0; cc < VGA_TEXT_COLS; cc++)
                        vga_buf[(r-1)*VGA_TEXT_COLS+cc] = vga_buf[r*VGA_TEXT_COLS+cc];
                for (int cc = 0; cc < VGA_TEXT_COLS; cc++)
                    vga_buf[(VGA_TEXT_ROWS-1)*VGA_TEXT_COLS+cc] = (uint16_t)color << 8 | ' ';
                vga_row = VGA_TEXT_ROWS - 1;
            }
            continue;
        }
        if (vga_col >= VGA_TEXT_COLS) { vga_row++; vga_col = 0; }
        if (vga_row >= VGA_TEXT_ROWS) vga_row = VGA_TEXT_ROWS - 1;
        vga_buf[vga_row * VGA_TEXT_COLS + vga_col++] = (uint16_t)color << 8 | (uint8_t)c;
    }
}

void cons_hex32(uint32_t val) {
    for (int i = 7; i >= 0; i--) {
        uint8_t nib = (val >> (i * 4)) & 0xF;
        cons_putc(nib < 10 ? '0' + nib : 'A' + nib - 10);
    }
}

void cons_hex64(uint64_t val) {
    cons_hex32((uint32_t)(val >> 32));
    cons_hex32((uint32_t)val);
}

void cons_dec32(uint32_t val) {
    char buf[12];
    int i = 11;
    buf[i] = 0;
    if (val == 0) buf[--i] = '0';
    else while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
    cons_puts(&buf[i]);
}

void cons_bar(uint32_t val, uint32_t max, int width) {
    if (width < 2) width = 2;
    if (max == 0) max = 1;
    int filled = (int)((uint64_t)val * width / max);
    if (filled > width) filled = width;
    cons_putc('[');
    for (int i = 0; i < width; i++) {
        if (i < filled) cons_putc('#');
        else cons_putc('.');
    }
    cons_putc(']');
}
