/**
 * Chicago-95 Nano Editor
 * Bare-metal text editor for ring-0, using VGA text + PS/2 keyboard
 */

#include <stdint.h>
#include <string.h>
#include "shell/nano.h"
#include "boot/ring0_init.h"
#include "boot/security.h"
#include "fs/brainfs.h"
#include "fs/brainvfs.h"

/* ======================================================================== */
/* VGA Helpers                                                              */
/* ======================================================================== */

#define NANO_VGA_ADDR  ((volatile uint16_t *)0xB8000)
#define NANO_VGA_COLS  80
#define NANO_VGA_ROWS  25

#define NANO_COLOR_DEFAULT   0x07   /* grey on black */
#define NANO_COLOR_STATUS    0x70   /* black on grey */
#define NANO_COLOR_STATUS_HI 0x74   /* red on grey */
#define NANO_COLOR_TITLE     0x1F   /* white on blue */
#define NANO_COLOR_LINE_NUM  0x08   /* dark grey */
#define NANO_COLOR_SELECT    0x30   /* black on cyan */
#define NANO_COLOR_HELP      0x1F   /* white on blue */
#define NANO_COLOR_ERROR     0x4F   /* white on red */

static void nano_vga_put(int row, int col, char c, uint8_t color) {
    if (row < 0 || row >= NANO_VGA_ROWS || col < 0 || col >= NANO_VGA_COLS) return;
    NANO_VGA_ADDR[row * NANO_VGA_COLS + col] = (uint16_t)c | ((uint16_t)color << 8);
}

static void nano_vga_puts(int row, int col, const char *s, uint8_t color) {
    while (*s && col < NANO_VGA_COLS) {
        nano_vga_put(row, col++, *s++, color);
    }
}

static void nano_vga_clear(void) {
    for (int i = 0; i < NANO_VGA_ROWS * NANO_VGA_COLS; i++) {
        NANO_VGA_ADDR[i] = (uint16_t)' ' | ((uint16_t)NANO_COLOR_DEFAULT << 8);
    }
}

static void nano_vga_hex(int row, int col, uint32_t val, uint8_t color) {
    const char hex[] = "0123456789abcdef";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 9; i >= 2; i--) {
        buf[i] = hex[val & 0xF];
        val >>= 4;
    }
    buf[10] = 0;
    nano_vga_puts(row, col, buf, color);
}

/* ======================================================================== */
/* Number to string                                                         */
/* ======================================================================== */

static void nano_itoa(uint32_t val, char *buf) {
    char rev[12];
    int ri = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (val) { rev[ri++] = '0' + (val % 10); val /= 10; }
    int i = 0;
    while (ri > 0) buf[i++] = rev[--ri];
    buf[i] = 0;
}

/* ======================================================================== */
/* String helpers                                                           */
/* ======================================================================== */

static uint32_t nano_strlen(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static void nano_strcpy(char *dst, const char *src, uint32_t max) {
    uint32_t i;
    for (i = 0; i < max - 1 && src[i]; i++) dst[i] = src[i];
    dst[i] = 0;
}

/* ======================================================================== */
/* State                                                                    */
/* ======================================================================== */

static nano_state_t nano;

/* ======================================================================== */
/* File I/O                                                                 */
/* ======================================================================== */

static void nano_load_file(const char *filename) {
    /* Initialize empty buffer */
    nano.num_lines = 1;
    nano.line_len[0] = 0;
    nano.lines[0][0] = 0;
    nano.cursor_row = 0;
    nano.cursor_col = 0;
    nano.view_top = 0;
    nano.view_left = 0;
    nano.modified = 0;
    nano.want_exit = 0;
    nano.show_help = 0;
    nano.status_msg_timer = 0;

    nano_strcpy(nano.filename, filename, 64);

    /* Try to read the file from BrainFS */
    int fd = vfs_open(filename, FD_FLAG_READ);
    if (fd < 0) return;

    /* Read entire file into temp buffer */
    char tmpbuf[NANO_MAX_TEXT];
    uint32_t total = 0;
    uint8_t sector[512];
    while (total < NANO_MAX_TEXT - 512) {
        uint32_t nread = 0;
        int rc = vfs_read((uint32_t)fd, sector, 512, &nread);
        if (rc != 0 || nread == 0) break;
        for (uint32_t i = 0; i < nread && total < NANO_MAX_TEXT - 1; i++) {
            tmpbuf[total++] = (char)sector[i];
        }
    }
    tmpbuf[total] = 0;
    vfs_close((uint32_t)fd);

    if (total == 0) return;

    /* Split into lines */
    nano.num_lines = 0;
    uint32_t line_start = 0;
    for (uint32_t i = 0; i <= total; i++) {
        if (i == total || tmpbuf[i] == '\n') {
            if (nano.num_lines >= NANO_MAX_LINES) break;
            uint32_t len = i - line_start;
            if (len >= NANO_MAX_LINE_LEN) len = NANO_MAX_LINE_LEN - 1;
            for (uint32_t j = 0; j < len; j++) {
                nano.lines[nano.num_lines][j] = tmpbuf[line_start + j];
            }
            nano.lines[nano.num_lines][len] = 0;
            nano.line_len[nano.num_lines] = len;
            nano.num_lines++;
            line_start = i + 1;
        }
    }

    if (nano.num_lines == 0) {
        nano.num_lines = 1;
        nano.line_len[0] = 0;
        nano.lines[0][0] = 0;
    }
}

static int nano_save_file(void) {
    if (nano.filename[0] == 0) return -1;

    /* Build file content */
    char tmpbuf[NANO_MAX_TEXT];
    uint32_t total = 0;

    for (uint32_t l = 0; l < nano.num_lines; l++) {
        for (uint32_t c = 0; c < nano.line_len[l] && total < NANO_MAX_TEXT - 2; c++) {
            tmpbuf[total++] = nano.lines[l][c];
        }
        if (l < nano.num_lines - 1 && total < NANO_MAX_TEXT - 1) {
            tmpbuf[total++] = '\n';
        }
    }
    tmpbuf[total] = 0;

    /* Write via BrainFS */
    int fd = vfs_open(nano.filename, FD_FLAG_WRITE | FD_FLAG_TRUNCATE);
    if (fd < 0) return -1;

    /* Write in sectors */
    uint32_t written = 0;
    while (written < total) {
        uint32_t chunk = total - written;
        if (chunk > 512) chunk = 512;
        int rc = vfs_write((uint32_t)fd, &tmpbuf[written], chunk);
        if (rc != 0) break;
        written += chunk;
    }
    vfs_close((uint32_t)fd);

    nano.modified = 0;
    return 0;
}

/* ======================================================================== */
/* Cursor helpers                                                           */
/* ======================================================================== */

static void nano_clamp_cursor(void) {
    if (nano.cursor_row >= nano.num_lines) nano.cursor_row = nano.num_lines - 1;
    if (nano.cursor_col > nano.line_len[nano.cursor_row]) {
        nano.cursor_col = nano.line_len[nano.cursor_row];
    }
}

static void nano_scroll_into_view(void) {
    uint32_t editor_rows = NANO_VGA_ROWS - 2; /* rows 0-22 */
    uint32_t editor_cols = NANO_VGA_COLS - 5;  /* 5 chars for line numbers */

    if (nano.cursor_row < nano.view_top) {
        nano.view_top = nano.cursor_row;
    }
    if (nano.cursor_row >= nano.view_top + editor_rows) {
        nano.view_top = nano.cursor_row - editor_rows + 1;
    }
    if (nano.cursor_col < nano.view_left) {
        nano.view_left = nano.cursor_col;
    }
    if (nano.cursor_col >= nano.view_left + editor_cols) {
        nano.view_left = nano.cursor_col - editor_cols + 1;
    }
}

/* ======================================================================== */
/* Display                                                                  */
/* ======================================================================== */

static void nano_draw_status_bar(void) {
    int row = NANO_VGA_ROWS - 2;

    /* Title bar */
    for (int c = 0; c < NANO_VGA_COLS; c++) {
        nano_vga_put(row, c, ' ', NANO_COLOR_TITLE);
    }

    /* File name */
    nano_vga_puts(row, 1, " Chicago-95 Nano ", NANO_COLOR_TITLE);

    /* Modified indicator */
    if (nano.modified) {
        nano_vga_puts(row, 20, "[Modified]", NANO_COLOR_STATUS_HI);
    }

    /* Right side: line:col */
    char pos_buf[32];
    char num1[12], num2[12];
    nano_itoa(nano.cursor_row + 1, num1);
    nano_itoa(nano.cursor_col + 1, num2);
    uint32_t pi = 0;
    for (uint32_t i = 0; num1[i]; i++) pos_buf[pi++] = num1[i];
    pos_buf[pi++] = ':';
    for (uint32_t i = 0; num2[i]; i++) pos_buf[pi++] = num2[i];
    pos_buf[pi] = 0;

    int plen = (int)nano_strlen(pos_buf);
    nano_vga_puts(row, NANO_VGA_COLS - plen - 1, pos_buf, NANO_COLOR_TITLE);

    /* Bottom hint bar */
    row = NANO_VGA_ROWS - 1;
    for (int c = 0; c < NANO_VGA_COLS; c++) {
        nano_vga_put(row, c, ' ', NANO_COLOR_STATUS);
    }
    nano_vga_puts(row, 1, "^G", NANO_COLOR_STATUS_HI);
    nano_vga_puts(row, 3, "Help", NANO_COLOR_STATUS);
    nano_vga_puts(row, 8, "^O", NANO_COLOR_STATUS_HI);
    nano_vga_puts(row, 10, "Write", NANO_COLOR_STATUS);
    nano_vga_puts(row, 16, "^X", NANO_COLOR_STATUS_HI);
    nano_vga_puts(row, 18, "Exit", NANO_COLOR_STATUS);

    if (nano.status_msg_timer > 0) {
        nano_vga_puts(row, 30, "Wrote lines", NANO_COLOR_STATUS);
        nano.status_msg_timer--;
    }
}

static void nano_draw_editor(void) {
    uint32_t editor_rows = NANO_VGA_ROWS - 2;
    uint32_t gutter_width = 5; /* "nnn: " */

    for (uint32_t r = 0; r < editor_rows; r++) {
        uint32_t file_row = nano.view_top + r;

        /* Line number gutter */
        char gutter[6];
        if (file_row < nano.num_lines) {
            char num[8];
            nano_itoa(file_row + 1, num);
            uint32_t glen = nano_strlen(num);
            uint32_t gi = 0;
            /* Right-align in 4 chars */
            for (gi = 0; gi < 4 - glen; gi++) gutter[gi] = ' ';
            for (uint32_t j = 0; j < glen; j++) gutter[gi++] = num[j];
            gutter[gi++] = ':';
            gutter[gi] = 0;
        } else {
            nano_vga_puts(r, 0, "    ~", NANO_COLOR_LINE_NUM);
            /* Clear rest of line */
            for (int c = gutter_width; c < NANO_VGA_COLS; c++) {
                nano_vga_put(r, c, ' ', NANO_COLOR_DEFAULT);
            }
            continue;
        }

        nano_vga_puts(r, 0, gutter, NANO_COLOR_LINE_NUM);

        /* Clear rest of line */
        for (int c = gutter_width; c < NANO_VGA_COLS; c++) {
            nano_vga_put(r, c, ' ', NANO_COLOR_DEFAULT);
        }

        /* Line content */
        const char *line = nano.lines[file_row];
        uint32_t len = nano.line_len[file_row];

        for (uint32_t c = 0; c < len && (c - nano.view_left + gutter_width) < (uint32_t)NANO_VGA_COLS; c++) {
            int screen_col = (int)(c - nano.view_left) + (int)gutter_width;
            if (screen_col >= (int)gutter_width) {
                char ch = line[c];
                if (ch == '\t') {
                    /* Show tab as spaces */
                    for (int t = 0; t < NANO_TAB_SIZE && screen_col + t < NANO_VGA_COLS; t++) {
                        nano_vga_put(r, screen_col + t, ' ', NANO_COLOR_DEFAULT);
                    }
                } else {
                    nano_vga_put(r, screen_col, ch, NANO_COLOR_DEFAULT);
                }
            }
        }

        /* Highlight current row in gutter */
        if (file_row == nano.cursor_row) {
            for (int c = 0; c < (int)gutter_width; c++) {
                /* Reverse-video the gutter for current line */
                nano_vga_put(r, c, gutter[c], NANO_COLOR_SELECT);
            }
        }
    }
}

static void nano_draw_help(void) {
    nano_vga_clear();

    int row = 0;
    nano_vga_puts(row++, 0, "  Chicago-95 Nano Help", NANO_COLOR_TITLE);
    row++;

    const char *help_lines[] = {
        "  Movement:",
        "    Arrow keys, Home, End, PgUp, PgDn",
        "    Ctrl+A = Home    Ctrl+E = End",
        "",
        "  Editing:",
        "    Type to insert   Backspace = delete left",
        "    Delete = delete char    Tab = insert tab",
        "    Enter = new line",
        "",
        "  File Operations:",
        "    Ctrl+O = Save (Write Out)",
        "    Ctrl+X = Exit",
        "",
        "  Other:",
        "    Ctrl+G = Toggle this help",
        "    Ctrl+C = Show cursor position",
        "",
        "  Press any key to return to editor...",
        ""
    };
    uint32_t nhelp = sizeof(help_lines) / sizeof(help_lines[0]);
    for (uint32_t i = 0; i < nhelp && row < NANO_VGA_ROWS; i++) {
        nano_vga_puts(row++, 0, help_lines[i], NANO_COLOR_HELP);
    }
}

/* ======================================================================== */
/* Line editing operations                                                  */
/* ======================================================================== */

static void nano_insert_char(char c) {
    if (nano.cursor_row >= NANO_MAX_LINES) return;
    if (nano.line_len[nano.cursor_row] >= NANO_MAX_LINE_LEN - 1) return;

    uint32_t len = nano.line_len[nano.cursor_row];
    /* Shift right */
    for (uint32_t i = len; i > nano.cursor_col; i--) {
        nano.lines[nano.cursor_row][i] = nano.lines[nano.cursor_row][i - 1];
    }
    nano.lines[nano.cursor_row][nano.cursor_col] = c;
    nano.line_len[nano.cursor_row]++;
    nano.lines[nano.cursor_row][nano.line_len[nano.cursor_row]] = 0;
    nano.cursor_col++;
    nano.modified = 1;
}

static void nano_insert_tab(void) {
    for (int i = 0; i < NANO_TAB_SIZE; i++) {
        nano_insert_char(' ');
    }
}

static void nano_delete_char(void) {
    /* Delete character before cursor (backspace) */
    if (nano.cursor_col == 0 && nano.cursor_row == 0) return;

    if (nano.cursor_col == 0) {
        /* Merge with previous line */
        uint32_t prev = nano.cursor_row - 1;
        uint32_t prev_len = nano.line_len[prev];
        uint32_t cur_len = nano.line_len[nano.cursor_row];

        if (prev_len + cur_len < NANO_MAX_LINE_LEN) {
            for (uint32_t i = 0; i < cur_len; i++) {
                nano.lines[prev][prev_len + i] = nano.lines[nano.cursor_row][i];
            }
            nano.lines[prev][prev_len + cur_len] = 0;
            nano.line_len[prev] = prev_len + cur_len;

            /* Remove current line */
            for (uint32_t i = nano.cursor_row; i < nano.num_lines - 1; i++) {
                for (uint32_t j = 0; j < nano.line_len[i + 1]; j++) {
                    nano.lines[i][j] = nano.lines[i + 1][j];
                }
                nano.lines[i][nano.line_len[i + 1]] = 0;
                nano.line_len[i] = nano.line_len[i + 1];
            }
            nano.num_lines--;
        }
        nano.cursor_row = prev;
        nano.cursor_col = prev_len;
    } else {
        /* Delete char before cursor in same line */
        uint32_t len = nano.line_len[nano.cursor_row];
        for (uint32_t i = nano.cursor_col; i < len; i++) {
            nano.lines[nano.cursor_row][i - 1] = nano.lines[nano.cursor_row][i];
        }
        nano.line_len[nano.cursor_row]--;
        nano.lines[nano.cursor_row][nano.line_len[nano.cursor_row]] = 0;
        nano.cursor_col--;
    }
    nano.modified = 1;
}

static void nano_delete_forward(void) {
    /* Delete character at cursor (Delete key) */
    uint32_t len = nano.line_len[nano.cursor_row];
    if (nano.cursor_col >= len) {
        /* Merge with next line */
        if (nano.cursor_row + 1 < nano.num_lines) {
            uint32_t next_len = nano.line_len[nano.cursor_row + 1];
            if (len + next_len < NANO_MAX_LINE_LEN) {
                for (uint32_t i = 0; i < next_len; i++) {
                    nano.lines[nano.cursor_row][len + i] = nano.lines[nano.cursor_row + 1][i];
                }
                nano.lines[nano.cursor_row][len + next_len] = 0;
                nano.line_len[nano.cursor_row] = len + next_len;

                for (uint32_t i = nano.cursor_row + 1; i < nano.num_lines - 1; i++) {
                    for (uint32_t j = 0; j < nano.line_len[i + 1]; j++) {
                        nano.lines[i][j] = nano.lines[i + 1][j];
                    }
                    nano.lines[i][nano.line_len[i + 1]] = 0;
                    nano.line_len[i] = nano.line_len[i + 1];
                }
                nano.num_lines--;
                nano.modified = 1;
            }
        }
        return;
    }

    for (uint32_t i = nano.cursor_col; i < len - 1; i++) {
        nano.lines[nano.cursor_row][i] = nano.lines[nano.cursor_row][i + 1];
    }
    nano.line_len[nano.cursor_row]--;
    nano.lines[nano.cursor_row][nano.line_len[nano.cursor_row]] = 0;
    nano.modified = 1;
}

static void nano_insert_newline(void) {
    if (nano.num_lines >= NANO_MAX_LINES) return;

    /* Split current line at cursor */
    uint32_t cur_len = nano.line_len[nano.cursor_row];
    uint32_t split_len = cur_len - nano.cursor_col;

    /* Move lines down */
    for (uint32_t i = nano.num_lines; i > nano.cursor_row + 1; i--) {
        for (uint32_t j = 0; j < nano.line_len[i - 1]; j++) {
            nano.lines[i][j] = nano.lines[i - 1][j];
        }
        nano.lines[i][nano.line_len[i - 1]] = 0;
        nano.line_len[i] = nano.line_len[i - 1];
    }

    /* Copy right half of current line to next line */
    uint32_t next = nano.cursor_row + 1;
    for (uint32_t i = 0; i < split_len; i++) {
        nano.lines[next][i] = nano.lines[nano.cursor_row][nano.cursor_col + i];
    }
    nano.lines[next][split_len] = 0;
    nano.line_len[next] = split_len;

    /* Truncate current line */
    nano.lines[nano.cursor_row][nano.cursor_col] = 0;
    nano.line_len[nano.cursor_row] = nano.cursor_col;
    nano.num_lines++;

    nano.cursor_row++;
    nano.cursor_col = 0;
    nano.modified = 1;
}

/* ======================================================================== */
/* Key handling                                                             */
/* ======================================================================== */

static void nano_handle_key(int key) {
    if (nano.show_help) {
        nano.show_help = 0;
        return;
    }

    /* Ctrl+X = exit */
    if (key == 24) {
        nano.want_exit = 1;
        return;
    }

    /* Ctrl+O = save */
    if (key == 15) {
        int rc = nano_save_file();
        if (rc == 0) {
            nano.status_msg_timer = 3;
        }
        return;
    }

    /* Ctrl+G = help */
    if (key == 7) {
        nano.show_help = 1;
        return;
    }

    /* Ctrl+C = show position */
    if (key == 3) {
        /* Just flash the status bar - it already shows position */
        return;
    }

    /* Ctrl+A = home */
    if (key == 1) {
        nano.cursor_col = 0;
        return;
    }

    /* Ctrl+E = end */
    if (key == 5) {
        nano.cursor_col = nano.line_len[nano.cursor_row];
        return;
    }

    /* Ctrl+U = page up */
    if (key == 21) {
        if (nano.cursor_row >= 24) nano.cursor_row -= 24;
        else nano.cursor_row = 0;
        nano_clamp_cursor();
        return;
    }

    /* Ctrl+D = page down */
    if (key == 4) {
        nano.cursor_row += 24;
        nano_clamp_cursor();
        return;
    }

    /* Ctrl+K = delete to end of line */
    if (key == 11) {
        nano.line_len[nano.cursor_row] = nano.cursor_col;
        nano.lines[nano.cursor_row][nano.cursor_col] = 0;
        nano.modified = 1;
        return;
    }

    /* Ctrl+J = join with next line */
    if (key == 10) {
        if (nano.cursor_row + 1 < nano.num_lines) {
            uint32_t cur_len = nano.line_len[nano.cursor_row];
            uint32_t next_len = nano.line_len[nano.cursor_row + 1];
            if (cur_len + next_len < NANO_MAX_LINE_LEN) {
                for (uint32_t i = 0; i < next_len; i++) {
                    nano.lines[nano.cursor_row][cur_len + i] =
                        nano.lines[nano.cursor_row + 1][i];
                }
                nano.lines[nano.cursor_row][cur_len + next_len] = 0;
                nano.line_len[nano.cursor_row] = cur_len + next_len;

                for (uint32_t i = nano.cursor_row + 1; i < nano.num_lines - 1; i++) {
                    for (uint32_t j = 0; j < nano.line_len[i + 1]; j++) {
                        nano.lines[i][j] = nano.lines[i + 1][j];
                    }
                    nano.lines[i][nano.line_len[i + 1]] = 0;
                    nano.line_len[i] = nano.line_len[i + 1];
                }
                nano.num_lines--;
                nano.modified = 1;
            }
        }
        return;
    }

    /* Escape sequences (PS/2 extended keys) */
    if (key == KEY_UP) {
        if (nano.cursor_row > 0) nano.cursor_row--;
        nano_clamp_cursor();
        return;
    }
    if (key == KEY_DOWN) {
        if (nano.cursor_row < nano.num_lines - 1) nano.cursor_row++;
        nano_clamp_cursor();
        return;
    }
    if (key == KEY_LEFT) {
        if (nano.cursor_col > 0) nano.cursor_col--;
        else if (nano.cursor_row > 0) {
            nano.cursor_row--;
            nano.cursor_col = nano.line_len[nano.cursor_row];
        }
        return;
    }
    if (key == KEY_RIGHT) {
        if (nano.cursor_col < nano.line_len[nano.cursor_row]) nano.cursor_col++;
        else if (nano.cursor_row + 1 < nano.num_lines) {
            nano.cursor_row++;
            nano.cursor_col = 0;
        }
        return;
    }
    if (key == KEY_HOME) { nano.cursor_col = 0; return; }
    if (key == KEY_END) {
        nano.cursor_col = nano.line_len[nano.cursor_row];
        return;
    }
    if (key == KEY_PGUP) {
        if (nano.cursor_row >= 24) nano.cursor_row -= 24;
        else nano.cursor_row = 0;
        nano_clamp_cursor();
        return;
    }
    if (key == KEY_PGDN) {
        nano.cursor_row += 24;
        nano_clamp_cursor();
        return;
    }

    /* Enter */
    if (key == '\n' || key == '\r') {
        nano_insert_newline();
        return;
    }

    /* Backspace */
    if (key == '\b' || key == 0x7F) {
        nano_delete_char();
        return;
    }

    /* Delete */
    if (key == KEY_DELETE) {
        nano_delete_forward();
        return;
    }

    /* Tab */
    if (key == '\t') {
        nano_insert_tab();
        return;
    }

    /* Printable ASCII */
    if (key >= 32 && key < 127) {
        nano_insert_char((char)key);
        return;
    }
}

/* ======================================================================== */
/* Main editor loop                                                         */
/* ======================================================================== */

void nano_editor(const char *filename) {
    nano_vga_clear();
    nano_load_file(filename);

    /* Draw title bar at top */
    nano_vga_puts(0, 0, " Chicago-95 Nano  ", NANO_COLOR_TITLE);
    {
        char fn_buf[80];
        fn_buf[0] = ' ';
        uint32_t fi = 1;
        for (uint32_t i = 0; filename[i] && fi < 78; i++) fn_buf[fi++] = filename[i];
        fn_buf[fi] = 0;
        nano_vga_puts(0, 18, fn_buf, NANO_COLOR_TITLE);
    }

    /* Editor area: rows 0 to NANO_VGA_ROWS-3 (22 rows) */
    /* Status bar: row NANO_VGA_ROWS-2 (23) */
    /* Hint bar:   row NANO_VGA_ROWS-1 (24) */

    nano_draw_editor();
    nano_draw_status_bar();

    while (!nano.want_exit) {
        /* Draw cursor position indicator */
        {
            int crow = NANO_VGA_ROWS - 2;
            /* Update position display on right side of status bar */
            char num1[12], num2[12];
            nano_itoa(nano.cursor_row + 1, num1);
            nano_itoa(nano.cursor_col + 1, num2);
            char pos[32];
            uint32_t pi = 0;
            for (uint32_t i = 0; num1[i]; i++) pos[pi++] = num1[i];
            pos[pi++] = ',';
            for (uint32_t i = 0; num2[i]; i++) pos[pi++] = num2[i];
            pos[pi] = 0;
            /* Clear right area */
            for (int c = 55; c < NANO_VGA_COLS; c++) {
                nano_vga_put(crow, c, ' ', NANO_COLOR_TITLE);
            }
            nano_vga_puts(crow, NANO_VGA_COLS - nano_strlen(pos) - 2, pos, NANO_COLOR_TITLE);
        }

        nano_scroll_into_view();

        if (nano.show_help) {
            nano_draw_help();
        } else {
            nano_draw_editor();
            nano_draw_status_bar();
        }

        /* Wait for key */
        int key = fish_read_key();
        if (key != KEY_NONE) {
            nano_handle_key(key);
        }
    }

    /* Return to fish shell display */
    nano_vga_clear();
}
