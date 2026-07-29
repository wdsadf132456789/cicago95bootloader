#include <stdint.h>

#define VGA_BUF  ((volatile uint16_t *)0xB8000)
#define VGA_ROWS 25
#define VGA_COLS 80

#define COL_BLACK   0x00
#define COL_WHITE   0x07
#define COL_GREEN   0x02
#define COL_LGREEN  0x0A
#define COL_LYELLOW 0x0E
#define COL_LRED    0x0C
#define COL_LCYAN   0x0B
#define COL_GREY    0x08

#define FIELD_X  2
#define FIELD_Y  1
#define FIELD_W  30
#define FIELD_H  20

#define MAX_SNAKE 200

#define KBD_DATA 0x60
#define KBD_STAT 0x64

static uint8_t crow, ccol;
static uint16_t rng_state;

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

static int rnd(void) {
    rng_state = rng_state * 1103515245 + 12345;
    return (rng_state >> 16) & 0x7FFF;
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

typedef struct { int x, y; } point_t;

void stage8_entry(void) {
    kbd_flush();

    __asm__ volatile("rdtsc" : "=a"(rng_state) : : "edx");

restart:
    point_t snake[MAX_SNAKE];
    int snake_len = 4;
    int dir = 1;
    int new_dir = 1;
    int score = 0;
    int game_over = 0;
    int fx, fy;

    snake[0].x = FIELD_W / 2;
    snake[0].y = FIELD_H / 2;
    snake[1].x = snake[0].x - 1; snake[1].y = snake[0].y;
    snake[2].x = snake[0].x - 2; snake[2].y = snake[0].y;
    snake[3].x = snake[0].x - 3; snake[3].y = snake[0].y;

    do {
        fx = rnd() % FIELD_W;
        fy = rnd() % FIELD_H;
        int ok = 1;
        for (int i = 0; i < snake_len; i++)
            if (snake[i].x == fx && snake[i].y == fy) { ok = 0; break; }
        if (ok) break;
    } while (1);

    for (int y = 0; y < VGA_ROWS; y++)
        for (int x = 0; x < VGA_COLS; x++)
            VGA_BUF[y * VGA_COLS + x] = (uint16_t)(COL_BLACK << 4) | ' ';

    for (int y = 0; y <= FIELD_H; y++) {
        pxy(FIELD_X - 1, FIELD_Y + y, 0xB3, COL_WHITE);
        pxy(FIELD_X + FIELD_W, FIELD_Y + y, 0xB3, COL_WHITE);
    }
    for (int x = 0; x < FIELD_W; x++) {
        pxy(FIELD_X + x, FIELD_Y - 1, 0xCD, COL_WHITE);
        pxy(FIELD_X + x, FIELD_Y + FIELD_H, 0xCD, COL_WHITE);
    }
    pxy(FIELD_X - 1, FIELD_Y - 1, 0xC9, COL_WHITE);
    pxy(FIELD_X + FIELD_W, FIELD_Y - 1, 0xBB, COL_WHITE);
    pxy(FIELD_X - 1, FIELD_Y + FIELD_H, 0xC8, COL_WHITE);
    pxy(FIELD_X + FIELD_W, FIELD_Y + FIELD_H, 0xBC, COL_WHITE);

    crow = FIELD_Y; ccol = FIELD_X + FIELD_W + 4;
    ps("CHICAGO-95", COL_LCYAN);
    crow++; ccol = FIELD_X + FIELD_W + 4;
    ps("  SNAKE\n", COL_LGREEN);
    crow++; ccol = FIELD_X + FIELD_W + 4;
    ps("Score:", COL_WHITE);
    crow++; ccol = FIELD_X + FIELD_W + 7;
    crow += 3; ccol = FIELD_X + FIELD_W + 4;
    ps("Controls:", COL_GREY);
    crow++; ccol = FIELD_X + FIELD_W + 4;
    ps("arrows move", COL_GREY);
    crow++; ccol = FIELD_X + FIELD_W + 4;
    ps("P    pause", COL_GREY);
    crow++; ccol = FIELD_X + FIELD_W + 4;
    ps("ESC  menu", COL_GREY);
    crow++; ccol = FIELD_X + FIELD_W + 4;
    ps("SPC  restart", COL_GREY);

    int speed = 5;
    int move_timer = 0;
    int paused = 0;

    beep(523, 40);

    while (!game_over) {
        while (kbd_hit()) {
            uint8_t sc = kbd_get();
            if (sc & 0x80) continue;
            if (sc == 0x4B && dir != 1 && !paused) new_dir = 3;
            if (sc == 0x4D && dir != 3 && !paused) new_dir = 1;
            if (sc == 0x48 && dir != 2 && !paused) new_dir = 0;
            if (sc == 0x50 && dir != 0 && !paused) new_dir = 2;
            if (sc == 0x19) paused = !paused;
            if (sc == 0x01) {
                for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
                    VGA_BUF[i] = (uint16_t)(0x07 << 8) | ' ';
                return;
            }
            if (sc == 0x39) { game_over = 1; goto restart; }
        }

        if (paused) { delay(20000); continue; }

        move_timer++;
        if (move_timer < speed) { delay(10000); continue; }
        move_timer = 0;
        dir = new_dir;

        int nx = snake[0].x;
        int ny = snake[0].y;
        if (dir == 0) ny--;
        if (dir == 2) ny++;
        if (dir == 3) nx--;
        if (dir == 1) nx++;

        if (nx < 0 || nx >= FIELD_W || ny < 0 || ny >= FIELD_H) {
            game_over = 1;
            break;
        }

        int ate = (nx == fx && ny == fy);

        for (int i = snake_len - 1; i > 0; i--)
            snake[i] = snake[i - 1];
        snake[0].x = nx;
        snake[0].y = ny;

        if (!ate)
            snake_len--;
        else {
            snake_len++;
            snake[snake_len - 1] = snake[snake_len - 2];
            do {
                fx = rnd() % FIELD_W;
                fy = rnd() % FIELD_H;
                int ok = 1;
                for (int i = 0; i < snake_len; i++)
                    if (snake[i].x == fx && snake[i].y == fy) { ok = 0; break; }
                if (ok) break;
            } while (1);
            score += 10;
            beep(880, 30);

            if (snake_len > MAX_SNAKE) snake_len = MAX_SNAKE;

            int s = speed - 1;
            if (s < 1) s = 1;
            speed = s;
        }

        for (int i = 1; i < snake_len; i++) {
            if (snake[0].x == snake[i].x && snake[0].y == snake[i].y) {
                game_over = 1;
                break;
            }
        }

        for (int y = 0; y < FIELD_H; y++)
            for (int x = 0; x < FIELD_W; x++)
                pxy(FIELD_X + x, FIELD_Y + y, ' ', COL_BLACK << 4);

        for (int i = 0; i < snake_len; i++) {
            uint8_t col = (i == 0) ? COL_LGREEN : COL_GREEN;
            pxy(FIELD_X + snake[i].x, FIELD_Y + snake[i].y, 0xDB, col);
        }

        pxy(FIELD_X + fx, FIELD_Y + fy, '*', COL_LRED);

        {
            char buf[12]; int ii = 11; buf[ii] = 0;
            uint32_t v = score;
            if (v == 0) buf[--ii] = '0';
            else while (v > 0) { buf[--ii] = '0' + (v % 10); v /= 10; }
            int si = 0;
            int sx = FIELD_X + FIELD_W + 7, sy = FIELD_Y + 2;
            while (buf[ii]) { pxy(sx + si, sy, buf[ii], COL_LYELLOW); ii++; si++; }
            while (si < 8) { pxy(sx + si, sy, ' ', COL_BLACK << 4); si++; }
        }

        delay(10000);
    }

    for (int i = 0; i < 4; i++) {
        beep(440, 80);
        delay(40000);
        beep(330, 80);
        delay(40000);
    }

    int cy = FIELD_Y + FIELD_H / 2 - 1;
    static const char *gm = "GAME OVER";
    for (int i = 0; gm[i]; i++)
        pxy(FIELD_X + (FIELD_W - 9) / 2 + i, cy, gm[i], COL_LRED);
    static const char *sc = "Press SPACE to restart, ESC for menu";
    for (int i = 0; sc[i]; i++)
        pxy(FIELD_X + (FIELD_W - 34) / 2 + i, cy + 1, sc[i], COL_GREY);

    while (1) {
        if (kbd_hit()) {
            uint8_t sc2 = kbd_get();
            if (!(sc2 & 0x80)) {
                if (sc2 == 0x39) goto restart;
                if (sc2 == 0x01) {
                    for (int i = 0; i < VGA_ROWS * VGA_COLS; i++)
                        VGA_BUF[i] = (uint16_t)(0x07 << 8) | ' ';
                    return;
                }
            }
        }
        delay(20000);
    }
}
