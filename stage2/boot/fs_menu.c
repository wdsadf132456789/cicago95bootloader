/**
 * Chicago-95 Filesystem Selection Menu
 * Boot-time VGA menu to choose FAT width: 1 to 128
 * Features: arrow keys, number keys, custom input, timeout
 */

#include <stdint.h>
#include "boot/fs_menu.h"
#include "boot/ring0_init.h"
#include "boot/security.h"
#include "fs/brainfs.h"

fs_selection_t fs_selection;

#define VGA_ADDR  ((volatile uint16_t *)0xB8000)
#define VGA_COLS  80
#define VGA_ROWS  25

#define C_BLACK   0
#define C_BLUE    1
#define C_GREEN   2
#define C_CYAN    3
#define C_RED     4
#define C_MAGENTA 5
#define C_BROWN   6
#define C_LGREY   7
#define C_DGREY   8
#define C_LBLUE   9
#define C_LGREEN  10
#define C_LCYAN   11
#define C_LRED    12
#define C_LMAGENTA 13
#define C_YELLOW  14
#define C_WHITE   15

static uint8_t vga_color(uint8_t fg, uint8_t bg) { return fg | (bg << 4); }
static void vga_put(int row, int col, char c, uint8_t color) {
    if (row < 0 || row >= VGA_ROWS || col < 0 || col >= VGA_COLS) return;
    VGA_ADDR[row * VGA_COLS + col] = (uint16_t)c | ((uint16_t)color << 8);
}
static void vga_puts_at(int row, int col, const char *s, uint8_t color) {
    while (*s && col < VGA_COLS) vga_put(row, col++, *s++, color);
}
static void vga_clear(void) {
    uint8_t def = vga_color(C_LGREY, C_BLACK);
    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++) VGA_ADDR[i] = (uint16_t)' ' | ((uint16_t)def << 8);
}
static void vga_draw_box(int top, int left, int width, int height, uint8_t border_color, uint8_t fill_color) {
    vga_put(top, left, '+', border_color);
    for (int c = 1; c < width - 1; c++) vga_put(top, left + c, '-', border_color);
    vga_put(top, left + width - 1, '+', border_color);
    for (int r = 1; r < height - 1; r++) {
        vga_put(top + r, left, '|', border_color);
        for (int c = 1; c < width - 1; c++) vga_put(top + r, left + c, ' ', fill_color);
        vga_put(top + r, left + width - 1, '|', border_color);
    }
    vga_put(top + height - 1, left, '+', border_color);
    for (int c = 1; c < width - 1; c++) vga_put(top + height - 1, left + c, '-', border_color);
    vga_put(top + height - 1, left + width - 1, '+', border_color);
}

static void num_to_str(int val, char *buf) {
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    char rev[8]; int ri = 0;
    while (val) { rev[ri++] = '0' + (val % 10); val /= 10; }
    int i = 0;
    while (ri > 0) buf[i++] = rev[--ri];
    buf[i] = 0;
}

static void draw_presets(int selected, uint8_t box_fill, uint8_t dim_color, uint8_t bright_color) {
    int num_presets = 10;
    const char *labels[] = { "0 ", "1 ", "2 ", "4 ", "8 ", "12", "16", "32", "64", "128" };
    const char *descs[] = { "No filesystem", "1-bit  (tiny)", "2-bit  (small)",
        "4-bit  (small)", "8-bit  (medium)", "12-bit (standard)",
        "16-bit (large)", "32-bit (default)", "64-bit (extended)",
        "128-bit (maximum)" };
    uint8_t sev_colors[] = { C_DGREY, C_LCYAN, C_YELLOW, C_LGREEN, C_LGREEN, C_LGREEN, C_LGREEN, C_WHITE, C_YELLOW, C_LRED };

    for (int i = 0; i < num_presets; i++) {
        uint8_t bg = (i == selected) ? vga_color(C_WHITE, C_BLUE) : vga_color(sev_colors[i], C_BLACK);
        vga_puts_at(8 + i, 7, labels[i], bg);
        vga_puts_at(8 + i, 11, "-", dim_color);
        uint8_t text_col = (i == selected) ? vga_color(C_WHITE, C_BLUE) : bright_color;
        vga_puts_at(8 + i, 13, descs[i], text_col);
    }
}

/* Read a 1-3 digit number from keyboard, returns 0 on timeout/esc, sets *out on enter */
static int read_custom_input(int *out) {
    char buf[4] = {0};
    int len = 0;

    vga_puts_at(20, 7, "                                        ", vga_color(C_BLACK, C_BLACK));
    vga_puts_at(20, 7, "Type width (1-128): ", vga_color(C_YELLOW, C_BLACK));

    uint64_t start_tsc = ring0_rdtsc();

    while (1) {
        if (ring0_rdtsc() - start_tsc > 10000000000ULL) return 0; /* 10s timeout */

        int key = kbd_getchar();
        if (key < 0) continue;
        start_tsc = ring0_rdtsc(); /* reset on keypress */

        if (key == 27) return 0; /* esc = cancel */

        if (key == '\n' || key == '\r') {
            if (len == 0) return 0;
            buf[len] = 0;
            int val = 0;
            for (int i = 0; i < len; i++) val = val * 10 + (buf[i] - '0');
            if (val < 1 || val > 128) {
                vga_puts_at(20, 7, "                                        ", vga_color(C_BLACK, C_BLACK));
                vga_puts_at(20, 7, "Invalid! Must be 1-128. Try again: ", vga_color(C_LRED, C_BLACK));
                len = 0;
                continue;
            }
            *out = val;
            return 1;
        }

        if (key == 8 && len > 0) { /* backspace */
            len--;
            buf[len] = 0;
            /* redraw input area */
            vga_puts_at(20, 27, "   ", vga_color(C_BLACK, C_BLACK));
            if (len > 0) {
                vga_puts_at(20, 27, buf, vga_color(C_WHITE, C_BLACK));
            }
            continue;
        }

        if (key >= '0' && key <= '9' && len < 3) {
            buf[len++] = (char)key;
            vga_put(20, 27 + len - 1, (char)key, vga_color(C_WHITE, C_BLACK));
        }
    }
}

uint8_t fs_menu_show(void) {
    vga_clear();

    uint8_t title_color = vga_color(C_WHITE, C_BLUE);
    uint8_t label_color = vga_color(C_LCYAN, C_BLACK);
    uint8_t dim_color = vga_color(C_DGREY, C_BLACK);
    uint8_t bright_color = vga_color(C_WHITE, C_BLACK);
    uint8_t help_color = vga_color(C_YELLOW, C_BLACK);
    uint8_t box_border = vga_color(C_CYAN, C_BLACK);
    uint8_t box_fill = vga_color(C_LGREY, C_BLACK);
    uint8_t info_color = vga_color(C_LCYAN, C_BLACK);

    /* Title bar */
    vga_draw_box(1, 5, 70, 3, box_border, C_BLUE);
    vga_puts_at(2, 7, "Chicago-95 BrainFS  -  FAT Width (1-128)", title_color);

    /* Preset list */
    vga_draw_box(5, 5, 52, 13, box_border, box_fill);
    vga_puts_at(6, 7, "Pick a preset or type a custom width:", label_color);

    int selected = 7; /* default: 32-bit */
    draw_presets(selected, box_fill, dim_color, bright_color);

    /* Details panel */
    vga_draw_box(5, 57, 18, 13, box_border, box_fill);
    vga_puts_at(6, 59, "Details:", info_color);
    vga_puts_at(7, 59, "Width:  32-bit", vga_color(C_WHITE, C_BLACK));

    /* Help line */
    vga_puts_at(19, 7, "[Up/Down] Select  [Enter] Confirm  [C] Custom input", help_color);
    vga_puts_at(20, 7, "[Esc] Default (32-bit). Timeout: 5s.", dim_color);

    /* Input row for custom mode */
    vga_draw_box(19, 5, 70, 2, box_border, box_fill);

    uint64_t start_tsc = ring0_rdtsc();
    uint64_t timeout = FS_MENU_TIMEOUT_TICKS;
    int custom_width = 0; /* 0 = using preset list */

    while (1) {
        if (ring0_rdtsc() - start_tsc >= timeout) break;

        int key = kbd_getchar();
        if (key < 0) continue;

        start_tsc = ring0_rdtsc();
        timeout = 10000000000ULL; /* extend timeout on keypress */

        if (key == '\n' || key == '\r') break;
        if (key == 27) { selected = 7; custom_width = 0; break; }

        /* Custom input mode */
        if (key == 'c' || key == 'C') {
            int w = 0;
            if (read_custom_input(&w)) {
                custom_width = w;
                break;
            }
            /* cancelled — redraw */
            vga_draw_box(19, 5, 70, 2, box_border, box_fill);
            vga_puts_at(20, 7, "[Up/Down] Select  [C] Custom  [Enter] Confirm", help_color);
            start_tsc = ring0_rdtsc();
            timeout = FS_MENU_TIMEOUT_TICKS;
            continue;
        }

        /* Arrow keys */
        if (key == 27) {
            int next = kbd_getchar();
            if (next == '[') {
                int code = kbd_getchar();
                if (code == 'A' && selected > 0) { selected--; draw_presets(selected, box_fill, dim_color, bright_color); }
                if (code == 'B' && selected < 9) { selected++; draw_presets(selected, box_fill, dim_color, bright_color); }
            }
            /* Update details panel */
            const char *widths[] = { "0", "1", "2", "4", "8", "12", "16", "32", "64", "128" };
            vga_puts_at(7, 59, "                ", box_fill);
            vga_puts_at(7, 59, "Width:  ", info_color);
            vga_puts_at(7, 67, widths[selected], vga_color(C_WHITE, C_BLACK));
            vga_puts_at(7, 70, "-bit", info_color);
            continue;
        }

        /* Number keys 0-9 for quick preset select */
        if (key >= '0' && key <= '9') {
            int num = key - '0';
            const char *widths[] = { "0", "1", "2", "4", "8", "12", "16", "32", "64", "128" };
            int map[] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 };
            if (num <= 9) {
                selected = map[num];
                draw_presets(selected, box_fill, dim_color, bright_color);
                vga_puts_at(7, 59, "                ", box_fill);
                vga_puts_at(7, 59, "Width:  ", info_color);
                vga_puts_at(7, 67, widths[selected], vga_color(C_WHITE, C_BLACK));
                vga_puts_at(7, 70, "-bit", info_color);
            }
        }
    }

    /* Flash selection */
    uint8_t flash_color = vga_color(C_WHITE, C_GREEN);
    if (custom_width == 0) {
        const char *widths[] = { "0", "1", "2", "4", "8", "12", "16", "32", "64", "128" };
        const char *descs[] = { "No filesystem", "1-bit  (tiny)", "2-bit  (small)",
            "4-bit  (small)", "8-bit  (medium)", "12-bit (standard)",
            "16-bit (large)", "32-bit (default)", "64-bit (extended)",
            "128-bit (maximum)" };
        vga_puts_at(8 + selected, 7, widths[selected], flash_color);
        vga_puts_at(8 + selected, 13, descs[selected], flash_color);
    } else {
        vga_puts_at(20, 7, "                                        ", vga_color(C_BLACK, C_BLACK));
        vga_puts_at(20, 7, "Custom width: ", vga_color(C_LGREEN, C_BLACK));
        char cbuf[4]; num_to_str(custom_width, cbuf);
        vga_puts_at(20, 21, cbuf, flash_color);
        vga_puts_at(20, 24, "-bit", vga_color(C_LGREEN, C_BLACK));
    }

    uint64_t flash_start = ring0_rdtsc();
    while (ring0_rdtsc() - flash_start < 400000000ULL) {}

    if (custom_width > 0) return (uint8_t)custom_width;

    /* Preset map: index 0=0, 1=1, 2=2, 3=4, 4=8, 5=12, 6=16, 7=32, 8=64, 9=128 */
    uint8_t preset_map[] = { 0, 1, 2, 4, 8, 12, 16, 32, 64, 128 };
    return preset_map[selected];
}

int fs_menu_format(uint8_t drive, uint8_t fat_width) {
    if (fat_width == 0) return 0;
    return brainfs_format(drive, fat_width, 2048 * 1024);
}
