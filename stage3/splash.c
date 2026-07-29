#include <stdint.h>
#include "console.h"

static int phase_count = 0;
static const char *spinner_chars = "|/-\\";
static int spinner_idx = 0;

void splash_banner(void) {
    cons_color("\n", COL_DEFAULT);

    cons_color("    ____ _     _            ", COL_HI);
    cons_color("   ____                           \n", COL_DIM);
    cons_color("   / ___| |__ | |__   ___  ", COL_HI);
    cons_color("  / ___| |__   ___   __ _ \n", COL_DIM);
    cons_color("  | |   | '_ \\| '_ \\ / _ \\ ", COL_HI);
    cons_color(" | |   | '_ \\ / _ \\ / _` |\n", COL_DIM);
    cons_color("  | |___| | | | |_) |  __/ ", COL_HI);
    cons_color(" | |___| | | | (_) | (_| |\n", COL_DIM);
    cons_color("   \\____|_| |_|_.__/ \\___| ", COL_HI);
    cons_color("  \\____|_| |_|\\___/ \\__, |\n", COL_DIM);
    cons_color("                       ", COL_HI);
    cons_color("                    |___/ \n", COL_DIM);

    cons_color("  Boot Services v1.0", COL_HDR);
    cons_color("  ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~\n", COL_DIM);
}

void splash_dash_item(uint8_t row, const char *label, const char *value, uint8_t val_color) {
    cons_set_cursor(row, 2);
    cons_color("[ ", COL_DIM);
    cons_putc(spinner_chars[spinner_idx & 3]);
    cons_color(" ] ", COL_DIM);
    cons_color(label, COL_LABEL);
    int len = 0;
    for (const char *p = label; *p; p++) len++;
    for (int i = len; i < 24; i++) cons_putc('.');
    cons_color(" ", COL_DIM);
    cons_color(value, val_color);
    cons_clear_line(row);
}

void splash_dash_sep(uint8_t row) {
    cons_set_cursor(row, 0);
    for (int i = 0; i < 80; i++) cons_putc('-');
}

void splash_phase(const char *phase, int done) {
    if (done) {
        spinner_idx++;
        cons_color("  [", COL_OK);
        cons_putc('0' + (phase_count % 10));
        cons_color("] ", COL_OK);
        cons_color(phase, COL_DEFAULT);
        cons_color("  ", COL_DEFAULT);
        cons_color("OK\n", COL_OK);
        phase_count++;
    }
}
