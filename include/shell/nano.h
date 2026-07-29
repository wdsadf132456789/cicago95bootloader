#ifndef CHICAGO95_NANO_H
#define CHICAGO95_NANO_H

#include <stdint.h>
#include "shell/fish_shell.h"

#define NANO_MAX_LINES      256
#define NANO_MAX_LINE_LEN   200
#define NANO_MAX_TEXT       (NANO_MAX_LINES * NANO_MAX_LINE_LEN)
#define NANO_TAB_SIZE       4

typedef struct {
    char     lines[NANO_MAX_LINES][NANO_MAX_LINE_LEN];
    uint32_t line_len[NANO_MAX_LINES];
    uint32_t num_lines;
    uint32_t cursor_row;
    uint32_t cursor_col;
    uint32_t view_top;
    uint32_t view_left;
    uint32_t modified;
    char     filename[64];
    int      want_exit;
    int      show_help;
    uint32_t status_msg_timer;
} nano_state_t;

void nano_editor(const char *filename);

#endif
