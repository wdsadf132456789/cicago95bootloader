#include <stdint.h>

#define VGA_BUF  ((volatile uint16_t *)0xB8000)
#define VGA_ROWS 25
#define VGA_COLS 80

#define VGA_COLOR(fg, bg) ((fg) | ((uint8_t)(bg) << 4))

#define COL_BLACK   0x00
#define COL_BLUE    0x01
#define COL_GREEN   0x02
#define COL_CYAN    0x03
#define COL_RED     0x04
#define COL_MAGENTA 0x05
#define COL_BROWN   0x06
#define COL_WHITE   0x07
#define COL_GREY    0x08
#define COL_LBLUE   0x09
#define COL_LGREEN  0x0A
#define COL_LCYAN   0x0B
#define COL_LRED    0x0C
#define COL_LMAGENTA 0x0D
#define COL_LYELLOW 0x0E
#define COL_BWHITE  0x0F

#define PLAY_X  2
#define PLAY_Y  1
#define PLAY_W  10
#define PLAY_H  20

#define INFO_X  15
#define INFO_Y  2

#define KBD_DATA 0x60
#define KBD_STAT 0x64

static uint8_t board[PLAY_W * PLAY_H];
static uint8_t crow, ccol;

static void pc(char c, uint8_t color) {
    if (c == '\n') { crow++; ccol = 0; return; }
    if (ccol >= VGA_COLS) { crow++; ccol = 0; }
    if (crow >= VGA_ROWS) return;
    VGA_BUF[crow * VGA_COLS + ccol++] = (uint16_t)color << 8 | (uint8_t)c;
}

static void ps(const char *s, uint8_t color) {
    while (*s) pc(*s++, color);
}

static void pxy(int x, int y, char c, uint8_t color) {
    if ((uint32_t)x >= VGA_COLS || (uint32_t)y >= VGA_ROWS) return;
    VGA_BUF[y * VGA_COLS + x] = (uint16_t)color << 8 | (uint8_t)c;
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile("inb %1, %0" : "=a"(v) : "dN"(port));
    return v;
}

static int kbd_hit(void) { return inb(KBD_STAT) & 0x01; }
static uint8_t kbd_get(void) { return inb(KBD_DATA); }
static void kbd_flush(void) { while (kbd_hit()) kbd_get(); }

static void delay(uint32_t n) {
    for (volatile uint32_t i = 0; i < n; i++) __asm__ volatile("pause");
}

static uint32_t rng_state;

static int rnd(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
}

static const uint8_t pieces[7][4][16] = {
    {{0,0,0,0,1,1,1,1,0,0,0,0,0,0,0,0},
     {0,0,1,0,0,0,1,0,0,0,1,0,0,0,1,0},
     {0,0,0,0,0,0,0,0,1,1,1,1,0,0,0,0},
     {0,1,0,0,0,1,0,0,0,1,0,0,0,1,0,0}},
    {{0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
     {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
     {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0},
     {0,0,0,0,0,1,1,0,0,1,1,0,0,0,0,0}},
    {{0,0,0,0,0,1,0,0,1,1,1,0,0,0,0,0},
     {0,0,0,0,0,1,0,0,0,1,1,0,0,1,0,0},
     {0,0,0,0,0,0,0,0,1,1,1,0,0,1,0,0},
     {0,0,0,0,0,1,0,0,1,1,0,0,0,1,0,0}},
    {{0,0,0,0,0,1,1,0,1,1,0,0,0,0,0,0},
     {0,0,0,0,1,0,0,0,1,1,0,0,0,1,0,0},
     {0,0,0,0,0,0,0,0,0,1,1,0,1,1,0,0},
     {0,0,0,0,0,1,0,0,0,1,1,0,0,0,1,0}},
    {{0,0,0,0,1,1,0,0,0,1,1,0,0,0,0,0},
     {0,0,0,0,0,0,1,0,0,1,1,0,0,1,0,0},
     {0,0,0,0,0,0,0,0,1,1,0,0,0,1,1,0},
     {0,0,0,0,0,1,0,0,1,1,0,0,1,0,0,0}},
    {{0,0,0,0,1,0,0,0,1,1,1,0,0,0,0,0},
     {0,0,0,0,0,1,1,0,0,1,0,0,0,1,0,0},
     {0,0,0,0,0,0,0,0,1,1,1,0,0,0,1,0},
     {0,0,0,0,0,1,0,0,0,1,0,0,1,1,0,0}},
    {{0,0,0,0,0,0,1,0,1,1,1,0,0,0,0,0},
     {0,0,0,0,0,1,0,0,0,1,0,0,0,1,1,0},
     {0,0,0,0,0,0,0,0,1,1,1,0,1,0,0,0},
     {0,0,0,0,1,1,0,0,0,1,0,0,0,1,0,0}},
};

static const uint8_t pcolors[7] = {
    COL_LCYAN, COL_LYELLOW, COL_LMAGENTA, COL_LGREEN, COL_LRED, COL_LBLUE, COL_LRED
};

static int piece_at(int px, int py, int piece, int rot) {
    const uint8_t *cells = pieces[piece][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cells[r * 4 + c]) {
                int bx = px + c, by = py + r;
                if ((uint32_t)bx >= PLAY_W || (uint32_t)by >= PLAY_H) return -1;
                if (by >= 0 && board[by * PLAY_W + bx]) return -1;
            }
    return 0;
}

static void piece_write(int px, int py, int piece, int rot, int val) {
    const uint8_t *cells = pieces[piece][rot];
    uint8_t color = val ? pcolors[piece] : 0;
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cells[r * 4 + c]) {
                int bx = px + c, by = py + r;
                if ((uint32_t)by < PLAY_H && (uint32_t)bx < PLAY_W)
                    board[by * PLAY_W + bx] = color;
            }
}

static int clear_lines(void) {
    int cleared = 0;
    for (int y = PLAY_H - 1; y >= 0; y--) {
        int full = 1;
        for (int x = 0; x < PLAY_W; x++)
            if (!board[y * PLAY_W + x]) { full = 0; break; }
        if (full) {
            cleared++;
            for (int yy = y; yy > 0; yy--)
                for (int x = 0; x < PLAY_W; x++)
                    board[yy * PLAY_W + x] = board[(yy-1) * PLAY_W + x];
            for (int x = 0; x < PLAY_W; x++)
                board[0] = 0;
            y++;
        }
    }
    return cleared;
}

static void draw_border(void) {
    for (int y = 0; y < VGA_ROWS; y++)
        for (int x = 0; x < VGA_COLS; x++)
            VGA_BUF[y * VGA_COLS + x] = (uint16_t)0x00 << 8 | ' ';
    for (int y = 0; y <= PLAY_H; y++) {
        pxy(PLAY_X - 1, PLAY_Y + y, 0xB3, VGA_COLOR(COL_WHITE, COL_BLACK));
        pxy(PLAY_X + PLAY_W, PLAY_Y + y, 0xB3, VGA_COLOR(COL_WHITE, COL_BLACK));
    }
    for (int x = 0; x < PLAY_W; x++) {
        pxy(PLAY_X + x, PLAY_Y - 1, 0xCD, VGA_COLOR(COL_WHITE, COL_BLACK));
        pxy(PLAY_X + x, PLAY_Y + PLAY_H, 0xCD, VGA_COLOR(COL_WHITE, COL_BLACK));
    }
    pxy(PLAY_X - 1, PLAY_Y - 1, 0xC9, VGA_COLOR(COL_WHITE, COL_BLACK));
    pxy(PLAY_X + PLAY_W, PLAY_Y - 1, 0xBB, VGA_COLOR(COL_WHITE, COL_BLACK));
    pxy(PLAY_X - 1, PLAY_Y + PLAY_H, 0xC8, VGA_COLOR(COL_WHITE, COL_BLACK));
    pxy(PLAY_X + PLAY_W, PLAY_Y + PLAY_H, 0xBC, VGA_COLOR(COL_WHITE, COL_BLACK));
}

static void draw_board(void) {
    for (int y = 0; y < PLAY_H; y++)
        for (int x = 0; x < PLAY_W; x++) {
            uint8_t c = board[y * PLAY_W + x];
            if (c)
                pxy(PLAY_X + x, PLAY_Y + y, 0xDB, VGA_COLOR(c, c));
            else
                pxy(PLAY_X + x, PLAY_Y + y, ' ', VGA_COLOR(COL_BLACK, COL_BLACK));
        }
}

static void draw_preview(int piece, int px, int py, uint8_t color) {
    const uint8_t *cells = pieces[piece][0];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cells[r * 4 + c])
                pxy(px + c, py + r, 0xDB, VGA_COLOR(color, color));
            else
                pxy(px + c, py + r, ' ', VGA_COLOR(COL_BLACK, COL_BLACK));
}

static void draw_ghost(int px, int py, int piece, int rot) {
    int gy = py;
    while (piece_at(px, gy + 1, piece, rot) == 0) gy++;
    const uint8_t *cells = pieces[piece][rot];
    for (int r = 0; r < 4; r++)
        for (int c = 0; c < 4; c++)
            if (cells[r * 4 + c]) {
                int bx = px + c, by = gy + r;
                if ((uint32_t)by < PLAY_H && (uint32_t)bx < PLAY_W)
                    pxy(PLAY_X + bx, PLAY_Y + by, 0xDB, VGA_COLOR(COL_GREY, COL_GREY));
            }
}

static void beep(uint32_t freq, uint32_t ms) {
    uint32_t div = 1193182 / freq;
    __asm__ volatile("outb %%al, %%dx" : : "a"((uint8_t)0xB6), "d"((uint16_t)0x43));
    __asm__ volatile("outb %%al, %%dx" : : "a"((uint8_t)(div & 0xFF)), "d"((uint16_t)0x42));
    __asm__ volatile("outb %%al, %%dx" : : "a"((uint8_t)(div >> 8)), "d"((uint16_t)0x42));
    uint8_t tmp;
    __asm__ volatile("inb %%dx, %0" : "=a"(tmp) : "d"((uint16_t)0x61));
    __asm__ volatile("outb %%al, %%dx" : : "a"(tmp | 0x03), "d"((uint16_t)0x61));
    delay(ms * 8000);
    __asm__ volatile("inb %%dx, %0" : "=a"(tmp) : "d"((uint16_t)0x61));
    __asm__ volatile("outb %%al, %%dx" : : "a"(tmp & 0xFC), "d"((uint16_t)0x61));
}

static void play_melody(void) {
    static const uint16_t n[] = { 523, 659, 784, 1047, 784, 659, 523 };
    for (int i = 0; i < 7; i++) beep(n[i], 60);
}

static void show_score(int x, int y, uint32_t score) {
    char buf[12]; int i = 11; buf[i] = 0;
    uint32_t v = score;
    if (v == 0) buf[--i] = '0';
    else while (v > 0) { buf[--i] = '0' + (v % 10); v /= 10; }
    int si = 0;
    while (buf[i]) { pxy(x + si, y, buf[i], VGA_COLOR(COL_LYELLOW, COL_BLACK)); i++; si++; }
    while (si < 8) { pxy(x + si, y, ' ', VGA_COLOR(COL_BLACK, COL_BLACK)); si++; }
}

void stage7_entry(void) {
    kbd_flush();

    for (int i = 0; i < PLAY_W * PLAY_H; i++) board[i] = 0;

    __asm__ volatile("rdtsc" : "=a"(rng_state) : : "edx");

    int next_piece = rnd() % 7;
    int score = 0, level = 1, lines = 0;
    int game_over = 0;

    draw_border();
    crow = INFO_Y; ccol = INFO_X;
    ps("CHICAGO-95\n", VGA_COLOR(COL_LCYAN, COL_BLACK));
    ps(" TETRIS\n", VGA_COLOR(COL_LMAGENTA, COL_BLACK));
    crow++; ccol = INFO_X;
    ps("Score:\n", VGA_COLOR(COL_WHITE, COL_BLACK));
    ps("Next:\n", VGA_COLOR(COL_WHITE, COL_BLACK));
    crow = INFO_Y + 12; ccol = INFO_X;
    ps("Controls:\n", VGA_COLOR(COL_GREY, COL_BLACK));
    ps(" <- -> move\n", VGA_COLOR(COL_GREY, COL_BLACK));
    ps(" UP   rotate\n", VGA_COLOR(COL_GREY, COL_BLACK));
    ps(" DOWN soft drop\n", VGA_COLOR(COL_GREY, COL_BLACK));
    ps(" SPACE hard drop\n", VGA_COLOR(COL_GREY, COL_BLACK));
    ps(" P    pause\n", VGA_COLOR(COL_GREY, COL_BLACK));
    ps(" ESC  menu\n", VGA_COLOR(COL_GREY, COL_BLACK));

    play_melody();

    while (!game_over) {
        int cur_piece = next_piece;
        int cur_rot = 0;
        int px = PLAY_W / 2 - 2;
        int py = 0;
        int drop_timer = 0;
        int drop_int = 30 - level * 2;
        if (drop_int < 3) drop_int = 3;
        int paused = 0;

        next_piece = rnd() % 7;

        if (piece_at(px, py, cur_piece, cur_rot)) {
            game_over = 1;
            break;
        }

        while (1) {
            if (kbd_hit()) {
                uint8_t sc = kbd_get();
                if (!(sc & 0x80)) {
                    if (sc == 0x4B && !paused) {
                        if (!piece_at(px - 1, py, cur_piece, cur_rot)) px--;
                    }
                    if (sc == 0x4D && !paused) {
                        if (!piece_at(px + 1, py, cur_piece, cur_rot)) px++;
                    }
                    if (sc == 0x48 && !paused) {
                        int nr = (cur_rot + 1) & 3;
                        if (!piece_at(px, py, cur_piece, nr)) cur_rot = nr;
                    }
                    if (sc == 0x50 && !paused) {
                        if (!piece_at(px, py + 1, cur_piece, cur_rot)) {
                            py++;
                            score++;
                        }
                    }
                    if (sc == 0x39 && !paused) {
                        while (!piece_at(px, py + 1, cur_piece, cur_rot)) {
                            py++;
                            score += 2;
                        }
                        goto place;
                    }
                    if (sc == 0x19) {
                        paused = !paused;
                    }
                    if (sc == 0x01) {
                        for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
                            VGA_BUF[i] = (uint16_t)(0x07 << 8) | ' ';
                        return;
                    }
                }
            }

            if (paused) { delay(20000); continue; }

            drop_timer++;
            if (drop_timer >= drop_int) {
                drop_timer = 0;
                if (!piece_at(px, py + 1, cur_piece, cur_rot))
                    py++;
                else
                    goto place;
            }

            draw_board();
            draw_ghost(px, py, cur_piece, cur_rot);
            piece_write(px, py, cur_piece, cur_rot, 1);
            draw_board();
            piece_write(px, py, cur_piece, cur_rot, 0);
            draw_preview(next_piece, INFO_X, INFO_Y + 3, pcolors[next_piece]);
            show_score(INFO_X, INFO_Y + 1, score);
            delay(15000);
        }

place:
        piece_write(px, py, cur_piece, cur_rot, 1);
        int cl = clear_lines();
        if (cl > 0) {
            lines += cl;
            int pts = cl == 1 ? 100 : cl == 2 ? 300 : cl == 3 ? 500 : 800;
            score += pts * level;
            level = lines / 10 + 1;
            beep(880, 40);
            draw_board();
            delay(30000);
        }
    }

    /* Game over */
    for (int i = 0; i < 4; i++) {
        beep(440, 100);
        delay(50000);
        beep(330, 100);
        delay(50000);
    }

    int cy = PLAY_Y + PLAY_H / 2 - 1;
    static const char *msg1 = "GAME OVER";
    for (int i = 0; msg1[i]; i++)
        pxy(PLAY_X + 1 + i, cy, msg1[i], VGA_COLOR(COL_BWHITE, COL_BLACK));
    show_score(PLAY_X + 1, cy + 1, score);

    for (int i = 0; i < 300; i++) {
        if (kbd_hit()) { kbd_get(); break; }
        delay(100000);
    }

    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
        VGA_BUF[i] = (uint16_t)(0x07 << 8) | ' ';
}
