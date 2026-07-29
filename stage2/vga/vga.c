/**
 * Chicago-95 VGA Driver
 * Text mode (80x25) with hardware cursor, scroll, box drawing
 * Graphics mode 13h (320x200) with pixel/rect/line/circle primitives
 */

#include "vga/vga.h"

static vga_state_t g_vga = {0};

/* ---- Hardware I/O helpers ---- */

static inline void vga_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t vga_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ---- VGA register access (for cursor, etc.) ---- */

static void vga_reg_write(uint8_t reg, uint8_t val) {
    vga_outb(0x3D4, reg);
    vga_outb(0x3D5, val);
}

static uint8_t vga_reg_read(uint8_t reg) {
    vga_outb(0x3D4, reg);
    return vga_inb(0x3D5);
}

static void vga_set_start_address(uint16_t addr) {
    vga_reg_write(0x0C, (addr >> 8) & 0xFF);
    vga_reg_write(0x0D, addr & 0xFF);
}

/* ---- Text Mode ---- */

void vga_text_init(void) {
    g_vga.framebuffer = (uint8_t *)VGA_TEXT_ADDR;
    g_vga.cols = VGA_TEXT_COLS;
    g_vga.rows = VGA_TEXT_ROWS;
    g_vga.mode = 0x03;  /* 80x25 text */
    g_vga.color = VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    g_vga.cursor.row = 0;
    g_vga.cursor.col = 0;
    g_vga.cursor.visible = 1;
    g_vga.cursor.shape_top = 13;
    g_vga.cursor.shape_bottom = 15;

    vga_text_clear();
    vga_cursor_enable(13, 15);
}

void vga_text_clear(void) {
    vga_text_clear_color(VGA_COLOR(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}

void vga_text_clear_color(uint8_t color) {
    uint16_t *vga16 = (uint16_t *)VGA_TEXT_ADDR;
    uint16_t blank = (' ') | ((uint16_t)color << 8);
    for (uint32_t i = 0; i < VGA_TEXT_COLS * VGA_TEXT_ROWS; i++)
        vga16[i] = blank;
    g_vga.cursor.row = 0;
    g_vga.cursor.col = 0;
    vga_cursor_set(0, 0);
}

void vga_text_put_char(char c, uint8_t color) {
    uint16_t *vga16 = (uint16_t *)VGA_TEXT_ADDR;

    if (c == '\n') {
        g_vga.cursor.col = 0;
        g_vga.cursor.row++;
    } else if (c == '\r') {
        g_vga.cursor.col = 0;
    } else if (c == '\t') {
        g_vga.cursor.col = (g_vga.cursor.col + 8) & ~7;
    } else if (c == '\b') {
        if (g_vga.cursor.col > 0) {
            g_vga.cursor.col--;
            uint32_t idx = g_vga.cursor.row * VGA_TEXT_COLS + g_vga.cursor.col;
            vga16[idx] = (' ') | ((uint16_t)color << 8);
        }
    } else {
        uint32_t idx = g_vga.cursor.row * VGA_TEXT_COLS + g_vga.cursor.col;
        vga16[idx] = (uint8_t)c | ((uint16_t)color << 8);
        g_vga.cursor.col++;
    }

    if (g_vga.cursor.col >= VGA_TEXT_COLS) {
        g_vga.cursor.col = 0;
        g_vga.cursor.row++;
    }
    if (g_vga.cursor.row >= VGA_TEXT_ROWS) {
        vga_text_scroll_up(1);
        g_vga.cursor.row = VGA_TEXT_ROWS - 1;
    }
    vga_cursor_set(g_vga.cursor.row, g_vga.cursor.col);
}

void vga_text_puts(const char *s, uint8_t color) {
    while (*s) vga_text_put_char(*s++, color);
}

void vga_text_put_at(uint8_t row, uint8_t col, char c, uint8_t color) {
    uint16_t *vga16 = (uint16_t *)VGA_TEXT_ADDR;
    if (row < VGA_TEXT_ROWS && col < VGA_TEXT_COLS) {
        uint32_t idx = row * VGA_TEXT_COLS + col;
        vga16[idx] = (uint8_t)c | ((uint16_t)color << 8);
    }
}

void vga_text_puts_at(uint8_t row, uint8_t col, const char *s, uint8_t color) {
    uint16_t *vga16 = (uint16_t *)VGA_TEXT_ADDR;
    while (*s && col < VGA_TEXT_COLS) {
        uint32_t idx = row * VGA_TEXT_COLS + col;
        vga16[idx] = (uint8_t)*s | ((uint16_t)color << 8);
        col++;
        s++;
    }
}

void vga_text_scroll_up(uint8_t lines) {
    uint16_t *vga16 = (uint16_t *)VGA_TEXT_ADDR;
    uint16_t blank = (' ') | ((uint16_t)g_vga.color << 8);

    for (uint8_t l = 0; l < lines; l++) {
        /* Move rows up */
        for (uint32_t i = 0; i < (VGA_TEXT_ROWS - 1) * VGA_TEXT_COLS; i++)
            vga16[i] = vga16[i + VGA_TEXT_COLS];
        /* Clear bottom row */
        for (uint32_t i = (VGA_TEXT_ROWS - 1) * VGA_TEXT_COLS;
             i < VGA_TEXT_ROWS * VGA_TEXT_COLS; i++)
            vga16[i] = blank;
    }
}

void vga_text_scroll_down(uint8_t lines) {
    uint16_t *vga16 = (uint16_t *)VGA_TEXT_ADDR;
    uint16_t blank = (' ') | ((uint16_t)g_vga.color << 8);

    for (uint8_t l = 0; l < lines; l++) {
        /* Move rows down */
        for (int32_t i = VGA_TEXT_ROWS * VGA_TEXT_COLS - 1; i >= (int32_t)VGA_TEXT_COLS; i--)
            vga16[i] = vga16[i - VGA_TEXT_COLS];
        /* Clear top row */
        for (uint32_t i = 0; i < VGA_TEXT_COLS; i++)
            vga16[i] = blank;
    }
}

void vga_text_draw_box(uint8_t row, uint8_t col, uint8_t width, uint8_t height,
                       uint8_t color, uint8_t border_style) {
    /* Box drawing characters (CP437) */
    const char tl = (border_style == 1) ? '+' : '\xDA';
    const char tr = (border_style == 1) ? '+' : '\xBF';
    const char bl = (border_style == 1) ? '+' : '\xC0';
    const char br = (border_style == 1) ? '+' : '\xD9';
    const char h  = (border_style == 1) ? '-' : '\xC4';
    const char v  = (border_style == 1) ? '|' : '\xB3';

    /* Top border */
    vga_text_put_at(row, col, tl, color);
    for (uint8_t i = 1; i < width - 1; i++)
        vga_text_put_at(row, col + i, h, color);
    vga_text_put_at(row, col + width - 1, tr, color);

    /* Sides */
    for (uint8_t r = 1; r < height - 1; r++) {
        vga_text_put_at(row + r, col, v, color);
        for (uint8_t i = 1; i < width - 1; i++)
            vga_text_put_at(row + r, col + i, ' ', color);
        vga_text_put_at(row + r, col + width - 1, v, color);
    }

    /* Bottom border */
    vga_text_put_at(row + height - 1, col, bl, color);
    for (uint8_t i = 1; i < width - 1; i++)
        vga_text_put_at(row + height - 1, col + i, h, color);
    vga_text_put_at(row + height - 1, col + width - 1, br, color);
}

void vga_text_draw_hline(uint8_t row, uint8_t col, uint8_t width, uint8_t ch, uint8_t color) {
    for (uint8_t i = 0; i < width; i++)
        vga_text_put_at(row, col + i, ch, color);
}

void vga_text_draw_vline(uint8_t row, uint8_t col, uint8_t height, uint8_t ch, uint8_t color) {
    for (uint8_t i = 0; i < height; i++)
        vga_text_put_at(row + i, col, ch, color);
}

/* ---- Cursor ---- */

void vga_cursor_enable(uint8_t top, uint8_t bottom) {
    g_vga.cursor.visible = 1;
    g_vga.cursor.shape_top = top;
    g_vga.cursor.shape_bottom = bottom;
    vga_reg_write(0x0A, (vga_reg_read(0x0A) & 0xC0) | top);
    vga_reg_write(0x0B, (vga_reg_read(0x0B) & 0xE0) | bottom);
}

void vga_cursor_disable(void) {
    g_vga.cursor.visible = 0;
    vga_reg_write(0x0A, 0x20);
}

void vga_cursor_set(uint8_t row, uint8_t col) {
    g_vga.cursor.row = row;
    g_vga.cursor.col = col;
    uint16_t pos = row * VGA_TEXT_COLS + col;
    vga_reg_write(0x0E, (pos >> 8) & 0xFF);
    vga_reg_write(0x0F, pos & 0xFF);
}

void vga_cursor_get(uint8_t *row, uint8_t *col) {
    *row = g_vga.cursor.row;
    *col = g_vga.cursor.col;
}

/* ---- Graphics Mode (Mode 13h: 320x200x256) ---- */

void vga_gfx_set_mode(uint8_t mode) {
    g_vga.mode = mode;
    g_vga.framebuffer = (uint8_t *)VGA_GFX_ADDR;

    /* Set VGA mode via INT 10h BIOS call */
    asm volatile(
        "int $0x10"
        : : "a"(0x0000 | mode)
    );
}

void vga_gfx_clear(uint8_t color) {
    uint8_t *fb = (uint8_t *)VGA_GFX_ADDR;
    /* 320*200 = 64000 bytes for mode 13h */
    for (uint32_t i = 0; i < 64000; i++) fb[i] = color;
}

void vga_gfx_put_pixel(uint16_t x, uint16_t y, uint8_t color) {
    uint8_t *fb = (uint8_t *)VGA_GFX_ADDR;
    if (x < 320 && y < 200)
        fb[y * 320 + x] = color;
}

uint8_t vga_gfx_get_pixel(uint16_t x, uint16_t y) {
    uint8_t *fb = (uint8_t *)VGA_GFX_ADDR;
    if (x < 320 && y < 200)
        return fb[y * 320 + x];
    return 0;
}

void vga_gfx_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint8_t color) {
    uint8_t *fb = (uint8_t *)VGA_GFX_ADDR;
    for (uint16_t row = y; row < y + h && row < 200; row++) {
        for (uint16_t col = x; col < x + w && col < 320; col++) {
            fb[row * 320 + col] = color;
        }
    }
}

void vga_gfx_draw_line(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1, uint8_t color) {
    int16_t dx = (x1 > x0) ? (x1 - x0) : (x0 - x1);
    int16_t dy = (y1 > y0) ? (y1 - y0) : (y0 - y1);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    while (1) {
        vga_gfx_put_pixel(x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int16_t e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        if (e2 < dx)  { err += dx; y0 += sy; }
    }
}

void vga_gfx_draw_circle(uint16_t cx, uint16_t cy, uint16_t r, uint8_t color) {
    int16_t x = r, y = 0;
    int16_t err = 0;

    while (x >= y) {
        vga_gfx_put_pixel(cx + x, cy + y, color);
        vga_gfx_put_pixel(cx + y, cy + x, color);
        vga_gfx_put_pixel(cx - y, cy + x, color);
        vga_gfx_put_pixel(cx - x, cy + y, color);
        vga_gfx_put_pixel(cx - x, cy - y, color);
        vga_gfx_put_pixel(cx - y, cy - x, color);
        vga_gfx_put_pixel(cx + y, cy - x, color);
        vga_gfx_put_pixel(cx + x, cy - y, color);

        y++;
        if (err <= 0) { err += 2*y + 1; }
        if (err > 0)  { x--; err -= 2*x + 1; }
    }
}

void vga_gfx_blit(uint16_t x, uint16_t y, const uint8_t *data, uint16_t w, uint16_t h) {
    uint8_t *fb = (uint8_t *)VGA_GFX_ADDR;
    for (uint16_t row = 0; row < h; row++) {
        if (y + row >= 200) break;
        for (uint16_t col = 0; col < w; col++) {
            if (x + col >= 320) break;
            fb[(y + row) * 320 + (x + col)] = data[row * w + col];
        }
    }
}

/* ---- Palette ---- */

void vga_set_palette(uint8_t index, uint8_t r, uint8_t g, uint8_t b) {
    vga_outb(0x3C8, index);
    vga_outb(0x3C9, r >> 2);
    vga_outb(0x3C9, g >> 2);
    vga_outb(0x3C9, b >> 2);
}

void vga_set_palette_default(void) {
    /* Standard 16-color palette for modes 04h-06h */
    static const uint8_t palette16[16][3] = {
        {0x00, 0x00, 0x00}, /* Black */
        {0x00, 0x00, 0xAA}, /* Blue */
        {0x00, 0xAA, 0x00}, /* Green */
        {0x00, 0xAA, 0xAA}, /* Cyan */
        {0xAA, 0x00, 0x00}, /* Red */
        {0xAA, 0x00, 0xAA}, /* Magenta */
        {0xAA, 0x55, 0x00}, /* Brown */
        {0xAA, 0xAA, 0xAA}, /* Light Grey */
        {0x55, 0x55, 0x55}, /* Dark Grey */
        {0x55, 0x55, 0xFF}, /* Light Blue */
        {0x55, 0xFF, 0x55}, /* Light Green */
        {0x55, 0xFF, 0xFF}, /* Light Cyan */
        {0xFF, 0x55, 0x55}, /* Light Red */
        {0xFF, 0x55, 0xFF}, /* Light Magenta */
        {0xFF, 0xFF, 0x55}, /* Yellow */
        {0xFF, 0xFF, 0xFF}, /* White */
    };
    for (int i = 0; i < 16; i++)
        vga_set_palette(i, palette16[i][0], palette16[i][1], palette16[i][2]);
}

/* ---- Border ---- */

void vga_set_border_color(uint8_t color) {
    vga_outb(0x3C0, 0x31);
    vga_outb(0x3C0, color);
}

/* ---- State access ---- */

vga_state_t *vga_get_state(void) {
    return &g_vga;
}
