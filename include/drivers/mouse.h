#ifndef CHICAGO95_MOUSE_H
#define CHICAGO95_MOUSE_H

#include <stdint.h>

#define MOUSE_PORT_DATA    0x60
#define MOUSE_PORT_STATUS  0x64
#define MOUSE_PORT_CMD     0x64

#define MOUSE_CMD_RESET    0xFF
#define MOUSE_CMD_RESEND   0xFE
#define MOUSE_CMD_SET_DEFAULT 0xF6
#define MOUSE_CMD_DISABLE_DATA 0xF5
#define MOUSE_CMD_ENABLE_DATA 0xF4
#define MOUSE_CMD_SET_SAMPLE 0xF3
#define MOUSE_CMD_GET_ID   0xF2
#define MOUSE_CMD_SET_RESOLUTION 0xE8
#define MOUSE_CMD_GET_TYPE 0xE9

#define MOUSE_IRQ          12
#define MOUSE_BTN_LEFT     0x01
#define MOUSE_BTN_RIGHT    0x02
#define MOUSE_BTN_MIDDLE   0x04

#define MOUSE_SCREEN_COLS  80
#define MOUSE_SCREEN_ROWS  25

typedef struct {
    int16_t  x;
    int16_t  y;
    uint8_t  buttons;
    uint8_t  prev_buttons;
    int8_t   dx;
    int8_t   dy;
    uint8_t  ready;
    uint8_t  active;
    uint8_t  packet[4];
    uint8_t  packet_idx;
    uint8_t  wheel;
} mouse_state_t;

extern mouse_state_t mouse_state;

int   mouse_init(void);
int   mouse_poll(void);
void  mouse_get_position(int16_t *x, int16_t *y);
uint8_t mouse_get_buttons(void);
int   mouse_left_pressed(void);
int   mouse_right_pressed(void);
int   mouse_left_released(void);
int   mouse_right_released(void);
void  mouse_set_position(int16_t x, int16_t y);
void  mouse_set_bounds(int16_t max_x, int16_t max_y);

#endif
