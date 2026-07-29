/**
 * Chicago-95 VGA Driver Interface
 * Text mode (80x25) and graphics mode (320x200 / 640x480) support
 */

#ifndef CHICAGO_VGA_H
#define CHICAGO_VGA_H

#include <stdint.h>

#define VGA_TEXT_ADDR    0xB8000
#define VGA_TEXT_COLS    80
#define VGA_TEXT_ROWS    25

#define VGA_GFX_ADDR    0xA0000
#define VGA_GFX_320_200 0x13
#define VGA_GFX_640_480 0x12
#define VGA_GFX_640_400 0x30

/* Standard VGA text mode attributes (foreground | bg << 4) */
#define VGA_COLOR_BLACK         0x00
#define VGA_COLOR_BLUE          0x01
#define VGA_COLOR_GREEN         0x02
#define VGA_COLOR_CYAN          0x03
#define VGA_COLOR_RED           0x04
#define VGA_COLOR_MAGENTA       0x05
#define VGA_COLOR_BROWN         0x06
#define VGA_COLOR_LIGHT_GREY    0x07
#define VGA_COLOR_DARK_GREY     0x08
#define VGA_COLOR_LIGHT_BLUE    0x09
#define VGA_COLOR_LIGHT_GREEN   0x0A
#define VGA_COLOR_LIGHT_CYAN    0x0B
#define VGA_COLOR_LIGHT_RED     0x0C
#define VGA_COLOR_LIGHT_MAGENTA 0x0D
#define VGA_COLOR_YELLOW        0x0E
#define VGA_COLOR_LIGHT_YELLOW  0x0E
#define VGA_COLOR_WHITE         0x0F

#define VGA_COLOR(fg, bg) ((fg) | ((bg) << 4))

/* VGA hardware cursor */
typedef struct {
    uint8_t row;
    uint8_t col;
    uint8_t visible;
    uint8_t shape_top;     /* Cursor scanline top */
    uint8_t shape_bottom;  /* Cursor scanline bottom */
} vga_cursor_t;

/* VGA state */
typedef struct {
    uint8_t *framebuffer;
    uint16_t cols;
    uint16_t rows;
    uint8_t  mode;          /* Current video mode */
    uint8_t  color;         /* Current text color attr */
    vga_cursor_t cursor;
    uint8_t  *palette;      /* 256-entry palette (for gfx modes) */
    uint32_t palette_size;
} vga_state_t;

/* Text mode functions */
void     vga_text_init(void);
void     vga_text_clear(void);
void     vga_text_clear_color(uint8_t color);
void     vga_text_put_char(char c, uint8_t color);
void     vga_text_puts(const char *s, uint8_t color);
void     vga_text_put_at(uint8_t row, uint8_t col, char c, uint8_t color);
void     vga_text_puts_at(uint8_t row, uint8_t col, const char *s, uint8_t color);
void     vga_text_scroll_up(uint8_t lines);
void     vga_text_scroll_down(uint8_t lines);
void     vga_text_draw_box(uint8_t row, uint8_t col, uint8_t width, uint8_t height, uint8_t color, uint8_t border_style);
void     vga_text_draw_hline(uint8_t row, uint8_t col, uint8_t width, uint8_t ch, uint8_t color);
void     vga_text_draw_vline(uint8_t row, uint8_t col, uint8_t height, uint8_t ch, uint8_t color);

/* Cursor functions */
void     vga_cursor_enable(uint8_t top, uint8_t bottom);
void     vga_cursor_disable(void);
void     vga_cursor_set(uint8_t row, uint8_t col);
void     vga_cursor_get(uint8_t *row, uint8_t *col);

/* Graphics mode functions */
void     vga_gfx_set_mode(uint8_t mode);
void     vga_gfx_clear(uint8_t color);
void     vga_gfx_put_pixel(uint16_t x, uint16_t y, uint8_t color);
uint8_t  vga_gfx_get_pixel(uint16_t x, uint16_t y);
void     vga_gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color);
void     vga_gfx_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t color);
void     vga_gfx_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t color);
void     vga_gfx_blit(uint16_t x, uint16_t y, const uint8_t *data, uint16_t w, uint16_t h);

/* Palette */
void     vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b);
void     vga_set_palette_default(void);

/* VGA state access */
vga_state_t *vga_get_state(void);

/* Border color (attribute register) */
void     vga_set_border_color(uint8_t color);

#endif /* CHICAGO_VGA_H */
