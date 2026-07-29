/**
 * Chicago-95 Minimal GUI
 * Text-mode (80x25) windowing system with mouse, taskbar, and demo apps
 */

#ifndef CHICAGO95_GUI_H
#define CHICAGO95_GUI_H

#include <stdint.h>

#define GUI_SCREEN_W    80
#define GUI_SCREEN_H    25
#define GUI_MAX_WINDOWS 8
#define GUI_MAX_ICONS   8

#define CLR_DESKTOP_BG      0x07
#define CLR_TASKBAR_BG      0x1F
#define CLR_TASKBAR_TEXT    0x1F
#define CLR_TASKBAR_ACTIVE  0x4F
#define CLR_WIN_BG          0x70
#define CLR_WIN_TITLE       0x1F
#define CLR_WIN_TITLE_TXT   0x1F
#define CLR_WIN_TITLE_UNF   0x78
#define CLR_WIN_BORDER      0x08
#define CLR_WIN_CLOSE       0x4F
#define CLR_TEXT_DARK       0x07
#define CLR_TEXT_LIGHT      0x07
#define CLR_MOUSE_CURSOR    0x4F
#define CLR_HIGHLIGHT       0x30

#define BOX_TL    0xDA
#define BOX_TR    0xBF
#define BOX_BL    0xC0
#define BOX_BR    0xD9
#define BOX_HLINE 0xC4
#define BOX_VLINE 0xB3

#define CURSOR_NORMAL   '*'
#define CURSOR_MOVE     '#'

typedef enum {
    APP_NONE = 0,
    APP_TERMINAL,
    APP_EDITOR,
    APP_FILES,
    APP_INFO,
    APP_ABOUT,
    APP_SETTINGS,
    APP_PROCMON
} gui_app_type_t;

typedef struct {
    int16_t  x, y;
    uint16_t w, h;
    char     label[32];
    uint8_t  color;
    uint8_t  hover_color;
    uint8_t  text_color;
    uint8_t  hovered;
    uint8_t  pressed;
    void     (*on_click)(void);
} gui_button_t;

struct gui_app;

typedef struct gui_window {
    int16_t  x, y;
    uint16_t w, h;
    char     title[48];
    uint8_t  visible;
    uint8_t  focused;
    uint8_t  dragging;
    int16_t  drag_off_x, drag_off_y;
    uint8_t  bg_color;
    gui_app_type_t app_type;
    struct gui_app *app;
    gui_button_t close_btn;
    void (*on_draw)(struct gui_window *win);
    void (*on_click_in)(struct gui_window *win, int16_t mx, int16_t my);
    void (*on_key)(struct gui_window *win, int key);
} gui_window_t;

typedef struct {
    int16_t  x, y;
    uint16_t w, h;
    char     label[32];
    uint8_t  color;
    uint8_t  icon_char;
    gui_app_type_t app_type;
} gui_icon_t;

typedef struct gui_app {
    gui_app_type_t type;
    char name[32];
    int  (*init)(gui_window_t *win);
    void (*draw)(gui_window_t *win);
    void (*key)(gui_window_t *win, int key);
    void (*click)(gui_window_t *win, int16_t mx, int16_t my);
    void (*cleanup)(gui_window_t *win);
    void *state;
} gui_app_t;

typedef struct {
    gui_window_t windows[GUI_MAX_WINDOWS];
    uint8_t      window_count;
    int8_t       focused_window;
    gui_icon_t   icons[GUI_MAX_ICONS];
    uint8_t      icon_count;
    int16_t      mouse_x, mouse_y;
    uint8_t      mouse_buttons;
    uint8_t      mouse_needs_redraw;
    uint8_t      needs_full_redraw;
    uint8_t      running;
    int          drag_window;
    int16_t      drag_off_x, drag_off_y;
    uint8_t      cursor_char;
} gui_state_t;

extern gui_state_t gui;
extern uint8_t CLR_DESKTOP_BG_OVERRIDE;

int  gui_init(void);
void gui_run(void);
void gui_exit(void);

int  gui_window_create(const char *title, int16_t x, int16_t y,
                       uint16_t w, uint16_t h, gui_app_type_t app_type);
void gui_window_close(int id);
void gui_window_focus(int id);
void gui_window_draw(gui_window_t *win);

void gui_desktop_draw(void);
void gui_desktop_add_icon(int16_t x, int16_t y, const char *label,
                          uint8_t icon_char, gui_app_type_t app_type);
void gui_taskbar_draw(void);
void gui_cursor_draw(void);

void gui_draw_rect(int16_t x, int16_t y, uint16_t w, uint16_t h, uint8_t color);
void gui_draw_char(int16_t x, int16_t y, char c, uint8_t color);
void gui_draw_string(int16_t x, int16_t y, const char *s, uint8_t color);

void gui_button_draw(gui_button_t *btn);
int  gui_button_hit(gui_button_t *btn, int16_t mx, int16_t my);

#endif
