#include <stdint.h>
#include <string.h>

extern void *memset(void *dst, int val, unsigned int n);

#include "gui/gui.h"
#include "drivers/mouse.h"
#include "boot/ring0_init.h"
#include "boot/security.h"
#include "boot/fs_menu.h"
#include "memory/memory.h"
#include "vga/vga.h"
#include "fs/brainfs.h"
#include "fs/brainvfs.h"

#define KEY_UP     0xC8
#define KEY_DOWN   0xD0
#define KEY_LEFT   0xCB
#define KEY_RIGHT  0xCD

#define START_MENU_ITEMS 6
#define START_MENU_LAST  (START_MENU_ITEMS - 1)

gui_state_t gui;
uint8_t CLR_DESKTOP_BG_OVERRIDE = 0;

static uint8_t clr_desktop;
static uint8_t clr_title_f;
static uint8_t clr_title_u;
static uint8_t clr_border;
static uint8_t clr_taskbar;
static uint8_t clr_taskbar_active;
static uint8_t clr_taskbar_text;
static uint8_t clr_highlight;
static uint8_t clr_win_bg;
static uint8_t clr_win_close;
static uint8_t clr_title_txt;
static uint8_t clr_text_dark;
static uint8_t clr_text_light;
static uint8_t clr_mouse_cursor;

static char bx_tl, bx_tr, bx_bl, bx_br, bx_h, bx_v;

static int z_order[GUI_MAX_WINDOWS];
static int z_counter;

static uint16_t cursor_saved_cell;
static int cursor_saved_x;
static int cursor_saved_y;
static int cursor_has_saved;

static int start_menu_sel;

typedef struct {
    char     buf[2048];
    uint32_t len;
    char     input[128];
    uint32_t input_len;
    uint32_t cursor;
} term_app_state_t;

typedef struct {
    uint32_t scroll;
    uint32_t selected;
    uint32_t count;
    char     names[32][32];
    uint8_t  is_dir[32];
} fm_app_state_t;

static term_app_state_t term_state;
static fm_app_state_t   fm_state;

static int str_len(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static void str_itoa(uint32_t val, char *buf) {
    char rev[12];
    int ri = 0;
    if (val == 0) { buf[0] = '0'; buf[1] = 0; return; }
    while (val) { rev[ri++] = '0' + (val % 10); val /= 10; }
    int i = 0;
    while (ri > 0) buf[i++] = rev[--ri];
    buf[i] = 0;
}

static inline void vga_put(int row, int col, char c, uint8_t color) {
    volatile uint16_t *addr = (volatile uint16_t *)0xB8000;
    if (row < 0 || row >= GUI_SCREEN_H || col < 0 || col >= GUI_SCREEN_W) return;
    addr[row * GUI_SCREEN_W + col] = (uint16_t)c | ((uint16_t)color << 8);
}

static inline uint16_t vga_get(int row, int col) {
    volatile uint16_t *addr = (volatile uint16_t *)0xB8000;
    if (row < 0 || row >= GUI_SCREEN_H || col < 0 || col >= GUI_SCREEN_W) return 0x0720;
    return addr[row * GUI_SCREEN_W + col];
}

static inline void vga_write_cell(int row, int col, uint16_t cell) {
    volatile uint16_t *addr = (volatile uint16_t *)0xB8000;
    if (row < 0 || row >= GUI_SCREEN_H || col < 0 || col >= GUI_SCREEN_W) return;
    addr[row * GUI_SCREEN_W + col] = cell;
}

void gui_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color) {
    for (uint16_t dy = 0; dy < h; dy++) {
        for (uint16_t dx = 0; dx < w; dx++) {
            vga_put(y + dy, x + dx, ' ', color);
        }
    }
}

void gui_draw_rect_outline(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color) {
    if (w < 2 || h < 2) { gui_draw_rect(x, y, w, h, color); return; }
    vga_put(y, x, bx_tl, color);
    vga_put(y, x + w - 1, bx_tr, color);
    vga_put(y + h - 1, x, bx_bl, color);
    vga_put(y + h - 1, x + w - 1, bx_br, color);
    for (uint16_t dx = 1; dx < w - 1; dx++) {
        vga_put(y, x + dx, bx_h, color);
        vga_put(y + h - 1, x + dx, bx_h, color);
    }
    for (uint16_t dy = 1; dy < h - 1; dy++) {
        vga_put(y + dy, x, bx_v, color);
        vga_put(y + dy, x + w - 1, bx_v, color);
    }
    for (uint16_t dy = 1; dy < h - 1; dy++)
        for (uint16_t dx = 1; dx < w - 1; dx++)
            vga_put(y + dy, x + dx, ' ', color);
}

void gui_draw_char(int16_t x, int16_t y, char c, uint8_t color) {
    vga_put(y, x, c, color);
}

void gui_draw_string(int16_t x, int16_t y, const char *s, uint8_t color) {
    for (int i = 0; s[i]; i++) {
        vga_put(y, x + i, s[i], color);
    }
}

void gui_draw_hline(int16_t x, int16_t y, uint16_t w, uint8_t color) {
    for (uint16_t i = 0; i < w; i++) vga_put(y, x + i, bx_h, color);
}

void gui_draw_vline(int16_t x, int16_t y, uint16_t h, uint8_t color) {
    for (uint16_t i = 0; i < h; i++) vga_put(y + i, x, bx_v, color);
}

void gui_button_draw(gui_button_t *btn) {
    uint8_t bg = btn->hovered ? btn->hover_color : btn->color;
    gui_draw_rect(btn->x, btn->y, btn->w, btn->h, bg);
    int tlen = str_len(btn->label);
    int tstart = btn->x + (btn->w - tlen) / 2;
    gui_draw_string(tstart, btn->y, btn->label, btn->text_color);
}

int gui_button_hit(gui_button_t *btn, int16_t mx, int16_t my) {
    return mx >= btn->x && mx < btn->x + (int16_t)btn->w &&
           my >= btn->y && my < btn->y + (int16_t)btn->h;
}

int gui_window_create(const char *title, int16_t x, int16_t y,
                      uint16_t w, uint16_t h, gui_app_type_t app_type) {
    if (gui.window_count >= GUI_MAX_WINDOWS) return -1;
    int id = gui.window_count;
    gui_window_t *win = &gui.windows[id];
    memset(win, 0, sizeof(gui_window_t));
    win->x = x; win->y = y; win->w = w; win->h = h;
    win->visible = 1; win->focused = 0; win->bg_color = clr_win_bg;
    win->app_type = app_type;
    int tlen = str_len(title);
    if (tlen > 46) tlen = 46;
    for (int i = 0; i < tlen; i++) win->title[i] = title[i];
    win->title[tlen] = 0;

    win->close_btn.x = x + w - 4;
    win->close_btn.y = y;
    win->close_btn.w = 3;
    win->close_btn.h = 1;
    win->close_btn.label[0] = 'X'; win->close_btn.label[1] = 0;
    win->close_btn.color = clr_win_close;
    win->close_btn.text_color = clr_title_txt;
    win->close_btn.hover_color = 0xCF;

    z_order[id] = z_counter++;

    gui.window_count++;
    return id;
}

void gui_window_close(int id) {
    if (id < 0 || id >= gui.window_count) return;
    gui_window_t *win = &gui.windows[id];
    win->visible = 0;
    if (gui.focused_window == id) {
        gui.focused_window = -1;
        int best = -1;
        int best_z = -1;
        for (int i = 0; i < gui.window_count; i++) {
            if (gui.windows[i].visible && z_order[i] > best_z) {
                best_z = z_order[i];
                best = i;
            }
        }
        if (best >= 0) { gui.focused_window = best; gui.windows[best].focused = 1; }
    }
    gui.needs_full_redraw = 1;
}

void gui_window_focus(int id) {
    if (id < 0 || id >= gui.window_count) return;
    for (int i = 0; i < gui.window_count; i++) gui.windows[i].focused = (i == id);
    gui.focused_window = id;
    z_order[id] = z_counter++;
    gui.needs_full_redraw = 1;
}

static void gui_draw_window_frame(gui_window_t *win) {
    int16_t x = win->x, y = win->y;
    uint16_t w = win->w, h = win->h;

    gui_draw_rect(x, y, w, h, win->bg_color);

    uint8_t title_c = win->focused ? clr_title_f : clr_title_u;
    for (uint16_t dx = 0; dx < w; dx++) vga_put(y, x + dx, ' ', title_c);
    int tlen = str_len(win->title);
    if (tlen > (int)w - 4) tlen = w - 4;
    if (tlen > 0) gui_draw_string(x + 2, y, win->title, title_c);

    vga_put(y, x + w - 3, ' ', clr_win_close);
    vga_put(y, x + w - 2, 'X', clr_win_close);
    vga_put(y, x + w - 1, ' ', clr_win_close);

    vga_put(y, x, bx_tl, clr_border);
    vga_put(y, x + w - 1, bx_tr, clr_border);
    vga_put(y + h - 1, x, bx_bl, clr_border);
    vga_put(y + h - 1, x + w - 1, bx_br, clr_border);
    for (uint16_t dx = 1; dx < w - 1; dx++) {
        vga_put(y, x + dx, bx_h, title_c);
        vga_put(y + h - 1, x + dx, bx_h, clr_border);
    }
    for (uint16_t dy = 1; dy < h - 1; dy++) {
        vga_put(y + dy, x, bx_v, clr_border);
        vga_put(y + dy, x + w - 1, bx_v, clr_border);
    }

    vga_put(y, x + w - 3, ' ', clr_win_close);
    vga_put(y, x + w - 2, 'X', clr_win_close);
    vga_put(y, x + w - 1, ' ', clr_win_close);
}

void gui_window_draw(gui_window_t *win) {
    if (!win->visible) return;
    gui_draw_window_frame(win);
    if (win->on_draw) {
        win->on_draw(win);
    }
}

static void gui_start_menu_draw(void);

void gui_desktop_draw(void) {
    uint8_t bg = CLR_DESKTOP_BG_OVERRIDE ? CLR_DESKTOP_BG_OVERRIDE : clr_desktop;
    gui_draw_rect(0, 0, GUI_SCREEN_W, GUI_SCREEN_H - 1, bg);

    int draw_order[GUI_MAX_WINDOWS];
    int draw_count = 0;
    for (int i = 0; i < gui.window_count; i++) {
        if (gui.windows[i].visible) draw_order[draw_count++] = i;
    }
    for (int i = 0; i < draw_count - 1; i++) {
        for (int j = i + 1; j < draw_count; j++) {
            if (z_order[draw_order[i]] > z_order[draw_order[j]]) {
                int tmp = draw_order[i];
                draw_order[i] = draw_order[j];
                draw_order[j] = tmp;
            }
        }
    }
    for (int i = 0; i < draw_count; i++) {
        gui_window_draw(&gui.windows[draw_order[i]]);
    }

    for (int i = 0; i < gui.icon_count; i++) {
        gui_icon_t *ic = &gui.icons[i];
        gui_draw_rect(ic->x, ic->y, 3, 3, ic->color);
        vga_put(ic->y + 1, ic->x + 1, ic->icon_char, clr_text_light);
        gui_draw_string(ic->x - 1, ic->y + 3, ic->label, clr_text_light);
    }

    gui_taskbar_draw();
    gui_start_menu_draw();
}

void gui_desktop_add_icon(int16_t x, int16_t y, const char *label,
                          uint8_t icon_char, gui_app_type_t app_type) {
    if (gui.icon_count >= GUI_MAX_ICONS) return;
    gui_icon_t *ic = &gui.icons[gui.icon_count];
    ic->x = x; ic->y = y; ic->w = 3; ic->h = 3;
    ic->color = clr_win_bg; ic->icon_char = icon_char;
    ic->app_type = app_type;
    int len = str_len(label);
    if (len > 30) len = 30;
    for (int i = 0; i < len; i++) ic->label[i] = label[i];
    ic->label[len] = 0;
    gui.icon_count++;
}

void gui_taskbar_draw(void) {
    int y = GUI_SCREEN_H - 1;
    gui_draw_rect(0, y, GUI_SCREEN_W, 1, clr_taskbar);

    gui_draw_string(0, y, " Start ", clr_taskbar_active);

    int pos = 9;
    for (int i = 0; i < gui.window_count && pos < GUI_SCREEN_W - 10; i++) {
        gui_window_t *win = &gui.windows[i];
        if (!win->visible) continue;
        uint8_t bg = win->focused ? clr_title_f : clr_taskbar;
        vga_put(y, pos++, ' ', bg);
        int tlen = str_len(win->title);
        if (tlen > 10) tlen = 10;
        for (int j = 0; j < tlen && pos < GUI_SCREEN_W - 2; j++)
            vga_put(y, pos++, win->title[j], bg);
        vga_put(y, pos++, ' ', bg);
    }

    char buf[16];
    uint64_t ms = ring0_ticks() / ring0_state.tsc_per_ms;
    uint32_t secs = (uint32_t)(ms / 1000);
    str_itoa(secs, buf);
    int blen = str_len(buf);
    buf[blen++] = 's'; buf[blen] = 0;
    gui_draw_string(GUI_SCREEN_W - blen - 2, y, buf, clr_taskbar_text);
}

void gui_cursor_draw(void) {
    if (cursor_has_saved) {
        vga_write_cell(cursor_saved_y, cursor_saved_x, cursor_saved_cell);
    }
    cursor_saved_x = gui.mouse_x;
    cursor_saved_y = gui.mouse_y;
    cursor_saved_cell = vga_get(cursor_saved_y, cursor_saved_x);
    vga_put(cursor_saved_y, cursor_saved_x, gui.cursor_char, clr_mouse_cursor);
    cursor_has_saved = 1;
}

void gui_cursor_undraw(void) {
    if (cursor_has_saved) {
        vga_write_cell(cursor_saved_y, cursor_saved_x, cursor_saved_cell);
        cursor_has_saved = 0;
    }
}

static void app_info_draw(gui_window_t *win) {
    int16_t cx = win->x + 1, cy = win->y + 1;
    uint16_t ch = win->h - 2;
    uint8_t tc = clr_text_dark;
    uint8_t hc = clr_title_f;
    uint8_t gc = 0x0A;
    int row = 0;

    if (row < (int)ch) gui_draw_string(cx, cy + row, "--- System ---", hc);
    row += 2;

    if (row < (int)ch) { gui_draw_string(cx, cy + row, "CPU:", hc); gui_draw_string(cx + 5, cy + row, ring0_state.cpu.brand, tc); }
    row++;

    if (row < (int)ch) {
        char feat[64]; int fi = 0;
        if (ring0_state.cpu.cpu_features & R0_CPUID_SSE) { feat[fi++]='S'; feat[fi++]='S'; feat[fi++]='E'; feat[fi++]=' '; }
        if (ring0_state.cpu.cpu_features & R0_CPUID_AVX) { feat[fi++]='A'; feat[fi++]='V'; feat[fi++]='X'; feat[fi++]=' '; }
        if (ring0_state.cpu.cpu_features & R0_CPUID_AES_NI) { feat[fi++]='A'; feat[fi++]='E'; feat[fi++]='S'; }
        feat[fi] = 0;
        gui_draw_string(cx, cy + row, feat, gc);
    }
    row += 2;

    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "Mem:", hc);
        uint64_t mb = pmm_get_total() / (1024 * 1024);
        char buf[32]; str_itoa((uint32_t)mb, buf);
        int blen = str_len(buf); buf[blen++] = 'M'; buf[blen] = 0;
        gui_draw_string(cx + 5, cy + row, buf, gc);
    }
    row += 2;

    if (row < (int)ch) { gui_draw_string(cx, cy + row, "Sec:", hc); gui_draw_string(cx + 5, cy + row, "26 modules", tc); }
    row++;

    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "FS:", hc);
        if (fs_selection.fat_width == 0) gui_draw_string(cx + 4, cy + row, "none", tc);
        else { char buf[8]; str_itoa(fs_selection.fat_width, buf); int blen=str_len(buf); buf[blen++]='-'; buf[blen++]='b'; buf[blen++]='i'; buf[blen++]='t'; buf[blen]=0; gui_draw_string(cx+4, cy+row, buf, gc); }
    }
    row++;

    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "Tor:", hc);
        if (tor_bootstrap_is_ready()) gui_draw_string(cx + 5, cy + row, "online", gc);
        else gui_draw_string(cx + 5, cy + row, "offline", tc);
    }
    row++;

    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "Up:", hc);
        uint32_t secs = (uint32_t)((ring0_ticks() / ring0_state.tsc_per_ms) / 1000);
        char buf[32]; str_itoa(secs, buf);
        int blen = str_len(buf); buf[blen++] = 's'; buf[blen] = 0;
        gui_draw_string(cx + 4, cy + row, buf, gc);
    }
}

static void app_fm_draw(gui_window_t *win) {
    int16_t cx = win->x + 1, cy = win->y + 1;
    uint16_t cw = win->w - 2, ch = win->h - 2;
    uint8_t tc = clr_text_dark;
    uint8_t hc = clr_title_f;
    uint8_t sc = clr_highlight;

    uint16_t scrollbar_w = (fm_state.count > (uint32_t)(ch - 2)) ? 1 : 0;
    uint16_t name_w = cw - 1 - scrollbar_w;
    uint16_t vis_items = ch - 2;

    gui_draw_string(cx, cy, "/ ", hc);

    for (uint32_t i = 0; i < vis_items && i + fm_state.scroll < fm_state.count; i++) {
        uint32_t idx = i + fm_state.scroll;
        uint8_t color = (idx == fm_state.selected) ? sc : tc;
        char prefix = fm_state.is_dir[idx] ? '/' : ' ';
        vga_put(cy + 1 + i, cx, prefix, fm_state.is_dir[idx] ? hc : tc);
        int nlen = str_len(fm_state.names[idx]);
        if (nlen > (int)name_w - 1) nlen = name_w - 1;
        for (int j = 0; j < nlen; j++) vga_put(cy + 1 + i, cx + 1 + j, fm_state.names[idx][j], color);
    }

    if (scrollbar_w) {
        int16_t sb_x = cx + cw - 1;
        int16_t sb_top = cy + 1;
        int16_t sb_bot = cy + ch - 2;
        uint16_t sb_h = sb_bot - sb_top + 1;
        vga_put(sb_top, sb_x, bx_tr, clr_border);
        vga_put(sb_bot, sb_x, bx_br, clr_border);
        for (uint16_t i = 1; i < sb_h - 1; i++)
            vga_put(sb_top + i, sb_x, bx_v, clr_border);

        uint32_t total = fm_state.count;
        uint32_t thumb_pos = 0;
        uint32_t thumb_size = 1;
        if (total > vis_items) {
            thumb_size = (sb_h - 2) * vis_items / total;
            if (thumb_size < 1) thumb_size = 1;
            thumb_pos = (sb_h - 2 - thumb_size) * fm_state.scroll / (total - vis_items);
        }
        for (uint32_t i = 0; i < thumb_size && 1 + thumb_pos + i < (uint32_t)(sb_h - 1); i++)
            vga_put(sb_top + 1 + thumb_pos + i, sb_x, 0xDB, clr_highlight);
    }

    uint8_t sc_status = 0x08;
    int16_t sy = cy + ch - 1;
    for (uint16_t dx = 0; dx < cw; dx++) vga_put(sy, cx + dx, ' ', sc_status);
    gui_draw_string(cx, sy, "/", sc_status);
    char cbuf[8];
    str_itoa(fm_state.count, cbuf);
    int clen = str_len(cbuf);
    int rlen = clen + 6;
    gui_draw_string(cx + cw - rlen, sy, cbuf, sc_status);
    gui_draw_string(cx + cw - rlen + clen, sy, " items", sc_status);
}

static void app_fm_init(gui_window_t *win) {
    (void)win;
    fm_state.scroll = 0;
    fm_state.selected = 0;
    fm_state.count = 0;
    vfs_dirent_t entries[32];
    int count = vfs_readdir("/", entries, 32);
    if (count > 0) {
        fm_state.names[0][0] = '.'; fm_state.names[0][1] = '.'; fm_state.names[0][2] = 0;
        fm_state.is_dir[0] = 1;
        fm_state.count = 1;
        for (int i = 0; i < count && fm_state.count < 32; i++) {
            int n;
            for (n = 0; n < 31 && entries[i].name[n]; n++)
                fm_state.names[fm_state.count][n] = entries[i].name[n];
            fm_state.names[fm_state.count][n] = 0;
            fm_state.is_dir[fm_state.count] = (entries[i].type == VFS_TYPE_DIR);
            fm_state.count++;
        }
    }
}

static void app_fm_key(gui_window_t *win, int key) {
    (void)win;
    if (key == 11 && fm_state.selected > 0) {
        fm_state.selected--;
        if (fm_state.selected < fm_state.scroll) fm_state.scroll = fm_state.selected;
    }
    if (key == 12 && fm_state.selected + 1 < fm_state.count) {
        fm_state.selected++;
        if (fm_state.selected >= fm_state.scroll + 12) fm_state.scroll = fm_state.selected - 11;
    }
    gui.needs_full_redraw = 1;
}

static void term_puts(const char *s) {
    for (int i = 0; s[i] && term_state.len < 2040; i++)
        term_state.buf[term_state.len++] = s[i];
}

static void term_process(void) {
    char cmd[128];
    uint32_t ci = 0;
    for (uint32_t i = 0; i < term_state.input_len && ci < 127; i++)
        cmd[ci++] = term_state.input[i];
    cmd[ci] = 0;

    term_puts("$ ");
    term_puts(cmd);
    term_puts("\n");

    if (ci == 0) { /* empty */ }
    else if (cmd[0] == 'h' && cmd[1] == 'e' && cmd[2] == 'l' && cmd[3] == 'p') {
        term_puts("Commands: help, clear, ver, mem,\n");
        term_puts("          fs, uptime, whoami\n");
    }
    else if (cmd[0] == 'c' && cmd[1] == 'l') { term_state.len = 0; }
    else if (cmd[0] == 'v' && cmd[1] == 'e' && cmd[2] == 'r') { term_puts("Chicago-95 v1.4M\n"); }
    else if (cmd[0] == 'w') { term_puts("root@chicago-95\n"); }
    else if (cmd[0] == 'm' && cmd[1] == 'e' && cmd[2] == 'm') {
        uint64_t total = pmm_get_total() / (1024*1024);
        uint64_t free_m = pmm_get_free() / (1024*1024);
        char buf[32];
        term_puts("Total: "); str_itoa((uint32_t)total, buf); term_puts(buf); term_puts("MB Free: ");
        str_itoa((uint32_t)free_m, buf); term_puts(buf); term_puts("MB\n");
    }
    else if (cmd[0] == 'f' && cmd[1] == 's') {
        if (fs_selection.fat_width == 0) term_puts("FS: none\n");
        else { char buf[8]; str_itoa(fs_selection.fat_width, buf); term_puts("FS: "); term_puts(buf); term_puts("-bit\n"); }
    }
    else if (cmd[0] == 'u') {
        uint32_t secs = (uint32_t)((ring0_ticks() / ring0_state.tsc_per_ms) / 1000);
        char buf[32]; str_itoa(secs, buf); int blen=str_len(buf); buf[blen++]='s'; buf[blen]=0;
        term_puts(buf); term_puts("\n");
    }
    else { term_puts("Unknown: "); term_puts(cmd); term_puts("\n"); }

    term_puts("$ ");
    term_state.cursor = term_state.len;
    term_state.input_len = 0;
}

static void app_term_draw(gui_window_t *win) {
    int16_t cx = win->x + 1, cy = win->y + 1;
    uint16_t cw = win->w - 2, ch = win->h - 2;
    uint8_t tc = 0x0A;

    int total_lines = 0;
    for (uint32_t i = 0; i < term_state.len; i++)
        if (term_state.buf[i] == '\n') total_lines++;
    total_lines++;

    int vis_start = 0, lines = 0;
    for (uint32_t i = 0; i < term_state.len; i++) {
        if (lines >= total_lines - (int)ch + 1) { vis_start = i; break; }
        if (term_state.buf[i] == '\n') lines++;
    }

    int row = 0;
    for (uint32_t i = vis_start; i < term_state.len && row < (int)ch - 1; i++) {
        if (term_state.buf[i] == '\n') row++;
        else if (row < (int)ch - 1 && (int)(i - vis_start) % (int)cw < (int)cw)
            vga_put(cy + row, cx + (i - vis_start) % (int)cw, term_state.buf[i], tc);
    }

    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "$ ", tc);
        for (uint32_t i = 0; i < term_state.input_len && i + 2 < cw; i++)
            vga_put(cy + row, cx + 2 + i, term_state.input[i], tc);
    }
}

static void app_term_init(gui_window_t *win) {
    (void)win;
    term_state.len = 0;
    term_state.input_len = 0;
    term_state.cursor = 0;
    term_puts("Chicago-95 Terminal\n");
    term_puts("Type 'help' for commands.\n\n$ ");
    term_state.cursor = term_state.len;
}

static void app_term_key(gui_window_t *win, int key) {
    (void)win;
    if (key == '\n' || key == '\r') {
        term_process();
    } else if (key == '\b' || key == 0x7F) {
        if (term_state.input_len > 0) term_state.input_len--;
    } else if (key >= 32 && key < 127 && term_state.input_len < 127) {
        term_state.input[term_state.input_len++] = (char)key;
    }
    gui.needs_full_redraw = 1;
}

static void app_about_draw(gui_window_t *win) {
    int16_t cx = win->x + 1, cy = win->y + 1;
    uint16_t ch = win->h - 2;
    uint8_t tc = clr_text_dark;
    uint8_t yc = 0x0E;
    int row = 0;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Chicago-95 BrainFS", yc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Bootloader v1.4M", 0x0B);
    row += 2;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "1.4M-line bootloader", tc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "with full security:", tc);
    row += 2;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "26 security modules", tc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Firewall + DNS/WiFi", tc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Tor v3 hidden svc", tc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Encrypted filesystem", tc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Fish shell + GNU", tc);
    row++;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "Ring-0 bare-metal", tc);
    row += 2;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "All at boot time!", yc);
}

static uint8_t settings_border_style = 0;
static int settings_theme_idx = 0;

static const char *theme_names[] = { "Classic", "Ocean", "Forest", "Sunset", "Midnight" };

static const uint8_t theme_desktop[]  = { 0x07, 0x17, 0x27, 0x47, 0x17 };
static const uint8_t theme_title_f[]  = { 0x1F, 0x1B, 0x2F, 0x4F, 0x5F };
static const uint8_t theme_title_u[]  = { 0x78, 0x78, 0x78, 0x78, 0x78 };
static const uint8_t theme_border[]   = { 0x08, 0x11, 0x22, 0x44, 0x55 };
static const uint8_t theme_taskbar[]  = { 0x1F, 0x11, 0x21, 0x41, 0x51 };
static const uint8_t theme_highlight[]= { 0x30, 0x30, 0x20, 0x40, 0x50 };

static void apply_theme(int idx) {
    clr_desktop      = theme_desktop[idx];
    clr_title_f      = theme_title_f[idx];
    clr_title_u      = theme_title_u[idx];
    clr_border       = theme_border[idx];
    clr_taskbar      = theme_taskbar[idx];
    clr_taskbar_active = 0x4F;
    clr_taskbar_text = theme_taskbar[idx];
    clr_highlight    = theme_highlight[idx];
    clr_win_bg       = 0x70;
    clr_win_close    = 0x4F;
    clr_title_txt    = theme_title_f[idx];
    clr_text_dark    = 0x07;
    clr_text_light   = 0x07;
    clr_mouse_cursor = 0x4F;
    CLR_DESKTOP_BG_OVERRIDE = clr_desktop;
    gui.needs_full_redraw = 1;
}

static void apply_border_style(int style) {
    if (style == 0) {
        bx_tl = 0xDA; bx_tr = 0xBF;
        bx_bl = 0xC0; bx_br = 0xD9;
        bx_h  = 0xC4; bx_v  = 0xB3;
    } else {
        bx_tl = 0xC9; bx_tr = 0xBB;
        bx_bl = 0xC8; bx_br = 0xBC;
        bx_h  = 0xCD; bx_v  = 0xBA;
    }
    gui.needs_full_redraw = 1;
}

static void app_settings_draw(gui_window_t *win) {
    int16_t cx = win->x + 1, cy = win->y + 1;
    uint16_t ch = win->h - 2;
    uint8_t tc = clr_text_dark;
    uint8_t hc = clr_title_f;
    int row = 0;

    if (row < (int)ch) gui_draw_string(cx, cy + row, "--- Settings ---", hc);
    row += 2;
    if (row < (int)ch) { gui_draw_string(cx, cy + row, "Theme:   ", hc); gui_draw_string(cx + 9, cy + row, theme_names[settings_theme_idx], 0x0A); }
    row++;
    if (row < (int)ch) { gui_draw_string(cx, cy + row, "Border:  ", hc); gui_draw_string(cx + 9, cy + row, settings_border_style ? "Double" : "Single", tc); }
    row++;
    if (row < (int)ch) { gui_draw_string(cx, cy + row, "Mouse:   ", hc); gui_draw_string(cx + 9, cy + row, "Active", 0x0A); }
    row++;
    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "Pointer: ", hc);
        char buf[16]; int bi = 0;
        { int x = gui.mouse_x; char r[8]; int ri = 0; if(x==0){r[ri++]='0';}else{while(x){r[ri++]='0'+(x%10);x/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi++] = ',';
        { int x = gui.mouse_y; char r[8]; int ri = 0; if(x==0){r[ri++]='0';}else{while(x){r[ri++]='0'+(x%10);x/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi] = 0;
        gui_draw_string(cx + 9, cy + row, buf, tc);
    }
    row += 2;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "[T] Theme  [B] Border", 0x08);
}

static void app_settings_key(gui_window_t *win, int key) {
    (void)win;
    if (key == 't' || key == 'T') {
        settings_theme_idx = (settings_theme_idx + 1) % 5;
        apply_theme(settings_theme_idx);
        apply_border_style(settings_border_style);
    }
    if (key == 'b' || key == 'B') {
        settings_border_style = !settings_border_style;
        apply_border_style(settings_border_style);
    }
}

static const char *proc_names[] = { "kernel", "init", "gui", "shell", "tor", "netguard" };
static const int proc_states[] = { 1, 1, 1, 0, 1, 1 };
static const uint32_t proc_cpu[] = { 45, 12, 28, 5, 8, 2 };
static const uint32_t proc_mem[] = { 2048, 512, 4096, 1024, 768, 1536 };

static void app_procmon_draw(gui_window_t *win) {
    int16_t cx = win->x + 1, cy = win->y + 1;
    uint16_t ch = win->h - 2;
    uint8_t hc = clr_title_f;
    int row = 0;

    if (row < (int)ch) gui_draw_string(cx, cy + row, "--- Processes ---", hc);
    row += 2;
    if (row < (int)ch) gui_draw_string(cx, cy + row, "PID Name       CPU  Memory", 0x08);
    row++;

    for (int i = 0; i < 6 && row < (int)ch; i++) {
        char buf[48]; int bi = 0;
        buf[bi++] = '0' + i; buf[bi++] = ' '; buf[bi++] = ' ';
        int nlen = 0; while (proc_names[i][nlen]) { buf[bi++] = proc_names[i][nlen]; nlen++; }
        while (bi < 16) buf[bi++] = ' ';
        buf[bi++] = '0' + (proc_cpu[i] / 10); buf[bi++] = '0' + (proc_cpu[i] % 10);
        buf[bi++] = '%'; buf[bi++] = ' ';
        uint32_t m = proc_mem[i]; char mr[8]; int mri = 0;
        if (m == 0) { mr[mri++] = '0'; } else { while (m) { mr[mri++] = '0' + (m % 10); m /= 10; } }
        while (mri > 0) buf[bi++] = mr[--mri];
        buf[bi++] = 'K'; buf[bi] = 0;
        uint8_t col = proc_states[i] ? 0x0A : 0x0C;
        gui_draw_string(cx, cy + row, buf, col);
        row++;
    }
    row++;
    if (row < (int)ch) {
        gui_draw_string(cx, cy + row, "Total: ", hc);
        uint64_t total = pmm_get_total() / (1024*1024);
        uint64_t free_m = pmm_get_free() / (1024*1024);
        char buf[32]; int bi = 0;
        { uint64_t v = total; char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi++] = 'M'; buf[bi++] = ' '; buf[bi++] = 'f'; buf[bi++] = 'r'; buf[bi++] = 'e'; buf[bi++] = 'e'; buf[bi++] = ' ';
        { uint64_t v = free_m; char r[16]; int ri = 0; if(v==0){r[ri++]='0';}else{while(v){r[ri++]='0'+(v%10);v/=10;}} while(ri>0)buf[bi++]=r[--ri]; }
        buf[bi++] = 'M'; buf[bi] = 0;
        gui_draw_string(cx + 7, cy + row, buf, 0x0A);
    }
}

static uint8_t start_menu_open = 0;

static void gui_start_menu_draw(void) {
    if (!start_menu_open) return;
    int mx = 0, my = GUI_SCREEN_H - 7;
    gui_draw_rect_outline(mx, my, 20, 6, clr_border);
    const char *items[] = { "System Info", "Terminal", "File Manager", "Settings", "Processes", "Shutdown" };
    for (int i = 0; i < START_MENU_ITEMS; i++) {
        uint8_t col = (i == start_menu_sel) ? clr_highlight : clr_text_dark;
        gui_draw_string(mx + 1, my + 1 + i, items[i], col);
    }
}

void gui_palette_init(void) {
}

static void gui_start_menu_activate(int item) {
    start_menu_open = 0;
    if (item == 5) { gui_exit(); return; }
    gui_app_type_t types[] = { APP_INFO, APP_TERMINAL, APP_FILES, APP_SETTINGS, APP_PROCMON };
    if (item >= 0 && item < 5) {
        for (int i = 0; i < gui.window_count; i++) {
            if (gui.windows[i].app_type == types[item]) {
                gui.windows[i].visible = 1;
                gui_window_focus(i);
                gui.needs_full_redraw = 1;
                return;
            }
        }
    }
    gui.needs_full_redraw = 1;
}

static void gui_handle_click(int16_t x, int16_t y) {
    int y_taskbar = GUI_SCREEN_H - 1;

    if (y == y_taskbar) {
        if (x >= 0 && x <= 6) {
            start_menu_open = !start_menu_open;
            start_menu_sel = 0;
            gui.needs_full_redraw = 1;
            return;
        }
        if (start_menu_open && x < 20 && y >= GUI_SCREEN_H - 7 && y < GUI_SCREEN_H - 1) {
            int item = y - (GUI_SCREEN_H - 7) - 1;
            gui_start_menu_activate(item);
            return;
        }
        int pos = 9;
        for (int i = 0; i < gui.window_count; i++) {
            gui_window_t *win = &gui.windows[i];
            if (!win->visible) continue;
            int tlen = str_len(win->title);
            if (tlen > 10) tlen = 10;
            int bw = tlen + 2;
            if (x >= pos && x < pos + bw) {
                if (win->focused) { win->visible = 0; }
                else { gui_window_focus(i); win->visible = 1; }
                gui.needs_full_redraw = 1;
                return;
            }
            pos += bw + 1;
        }
        return;
    }

    for (int i = gui.window_count - 1; i >= 0; i--) {
        gui_window_t *win = &gui.windows[i];
        if (!win->visible) continue;
        if (x >= win->x && x < win->x + (int16_t)win->w &&
            y >= win->y && y < win->y + (int16_t)win->h) {

            gui_window_focus(i);

            if (y == win->y) {
                if (x >= win->x + win->w - 4 && x <= win->x + win->w - 2) {
                    gui_window_close(i);
                    return;
                }
                gui.drag_window = i;
                gui.drag_off_x = x - win->x;
                gui.drag_off_y = y - win->y;
                gui.cursor_char = 0x23;
                return;
            }

            if (win->on_click_in) {
                win->on_click_in(win, x, y);
            }
            return;
        }
    }

    gui.focused_window = -1;
    for (int i = 0; i < gui.window_count; i++) gui.windows[i].focused = 0;
    gui.needs_full_redraw = 1;
}

static int gui_read_esc_sequence(void) {
    int next = kbd_getchar();
    if (next < 0) return -1;
    if (next == '[') {
        int code = kbd_getchar();
        if (code < 0) return -1;
        if (code == 'A') return KEY_UP;
        if (code == 'B') return KEY_DOWN;
        if (code == 'C') return KEY_RIGHT;
        if (code == 'D') return KEY_LEFT;
        if (code == '3') { int t = kbd_getchar(); (void)t; return -1; }
        if (code == '5') { int t = kbd_getchar(); (void)t; return -1; }
        if (code == '6') { int t = kbd_getchar(); (void)t; return -1; }
    }
    if (next == 'O') {
        int code = kbd_getchar();
        (void)code;
    }
    return -1;
}

static void gui_handle_key(int key) {
    if (start_menu_open) {
        if (key == 27) {
            int ext = gui_read_esc_sequence();
            if (ext == KEY_UP) {
                start_menu_sel = (start_menu_sel + START_MENU_LAST) % START_MENU_ITEMS;
                gui.needs_full_redraw = 1;
                return;
            }
            if (ext == KEY_DOWN) {
                start_menu_sel = (start_menu_sel + 1) % START_MENU_ITEMS;
                gui.needs_full_redraw = 1;
                return;
            }
            start_menu_open = 0;
            gui.needs_full_redraw = 1;
            return;
        }
        if (key == 11) {
            start_menu_sel = (start_menu_sel + START_MENU_LAST) % START_MENU_ITEMS;
            gui.needs_full_redraw = 1;
            return;
        }
        if (key == 12) {
            start_menu_sel = (start_menu_sel + 1) % START_MENU_ITEMS;
            gui.needs_full_redraw = 1;
            return;
        }
        if (key == '\n' || key == '\r') {
            gui_start_menu_activate(start_menu_sel);
            return;
        }
        if (key == '\b' || key == 0x7F) {
            start_menu_open = 0;
            gui.needs_full_redraw = 1;
            return;
        }
        return;
    }

    if (gui.focused_window < 0) return;
    gui_window_t *win = &gui.windows[gui.focused_window];

    if (key == 27) {
        int ext = gui_read_esc_sequence();
        if (ext >= 0) return;
        gui.focused_window = -1;
        for (int i = 0; i < gui.window_count; i++) gui.windows[i].focused = 0;
        gui.needs_full_redraw = 1;
        return;
    }
    if (key == 23) { gui_window_close(gui.focused_window); return; }

    if (win->on_key) win->on_key(win, key);
}

int gui_init(void) {
    memset(&gui, 0, sizeof(gui_state_t));
    gui.running = 1;
    gui.needs_full_redraw = 1;
    gui.focused_window = -1;
    gui.drag_window = -1;
    gui.cursor_char = CURSOR_NORMAL;
    z_counter = 0;
    cursor_has_saved = 0;
    cursor_saved_x = -1;
    cursor_saved_y = -1;
    start_menu_sel = 0;

    apply_theme(0);
    apply_border_style(0);

    mouse_init();
    gui.mouse_x = GUI_SCREEN_W / 2;
    gui.mouse_y = GUI_SCREEN_H / 2;

    int info_id = gui_window_create("System Info", 2, 1, 40, 14, APP_INFO);
    gui.windows[info_id].on_draw = app_info_draw;

    int term_id = gui_window_create("Terminal", 44, 1, 34, 18, APP_TERMINAL);
    gui.windows[term_id].on_draw = app_term_draw;
    gui.windows[term_id].on_key = app_term_key;
    app_term_init(&gui.windows[term_id]);

    int fm_id = gui_window_create("Files", 12, 5, 28, 14, APP_FILES);
    gui.windows[fm_id].on_draw = app_fm_draw;
    gui.windows[fm_id].on_key = app_fm_key;
    app_fm_init(&gui.windows[fm_id]);

    int about_id = gui_window_create("About", 50, 6, 28, 14, APP_ABOUT);
    gui.windows[about_id].on_draw = app_about_draw;

    int settings_id = gui_window_create("Settings", 20, 3, 30, 12, APP_SETTINGS);
    gui.windows[settings_id].on_draw = app_settings_draw;
    gui.windows[settings_id].on_key = app_settings_key;

    int proc_id = gui_window_create("Processes", 40, 4, 32, 14, APP_PROCMON);
    gui.windows[proc_id].on_draw = app_procmon_draw;

    gui_window_focus(term_id);

    gui_desktop_add_icon(1, 1, "Info", 'S', APP_INFO);
    gui_desktop_add_icon(1, 6, "Term", 'T', APP_TERMINAL);
    gui_desktop_add_icon(1, 11, "Files", 'F', APP_FILES);
    gui_desktop_add_icon(1, 16, "Cfg", 'C', APP_SETTINGS);
    gui_desktop_add_icon(1, 21, "Proc", 'P', APP_PROCMON);

    return 0;
}

void gui_run(void) {
    gui_desktop_draw();

    while (gui.running) {
        gui_cursor_undraw();

        mouse_poll();
        if (mouse_state.ready) {
            gui.mouse_x = mouse_state.x;
            gui.mouse_y = mouse_state.y;
            gui_handle_click(gui.mouse_x, gui.mouse_y);
            if (gui.drag_window >= 0 && (mouse_state.buttons & MOUSE_BTN_LEFT)) {
                gui_window_t *win = &gui.windows[gui.drag_window];
                int16_t nx = gui.mouse_x - gui.drag_off_x;
                int16_t ny = gui.mouse_y - gui.drag_off_y;
                if (nx < 0) nx = 0;
                if (ny < 0) ny = 0;
                if (nx + (int16_t)win->w > GUI_SCREEN_W) nx = GUI_SCREEN_W - win->w;
                if (ny + (int16_t)win->h > GUI_SCREEN_H - 1) ny = GUI_SCREEN_H - 1 - win->h;
                win->x = nx; win->y = ny;
                gui.needs_full_redraw = 1;
            }
            if (!(mouse_state.buttons & MOUSE_BTN_LEFT) && gui.drag_window >= 0) {
                gui.drag_window = -1;
                gui.cursor_char = CURSOR_NORMAL;
                gui.needs_full_redraw = 1;
            }
            gui.needs_full_redraw = 1;
            mouse_state.ready = 0;
        }

        int key = kbd_getchar();
        if (key > 0) gui_handle_key(key);

        if (gui.needs_full_redraw) {
            gui_desktop_draw();
            gui.needs_full_redraw = 0;
        }

        gui_cursor_draw();
        ring0_delay_ms(1);
    }
}

void gui_exit(void) {
    gui.running = 0;
    vga_text_clear();
}
