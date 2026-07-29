/**
 * Chicago-95 PS/2 Mouse Driver
 * IRQ12 handler, 3-byte packet parsing, position tracking
 */

#include <stdint.h>
#include "drivers/mouse.h"

mouse_state_t mouse_state;

/* ======================================================================== */
/* Port I/O                                                                 */
/* ======================================================================== */

static inline void mouse_outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t mouse_inb(uint16_t port) {
    uint8_t val;
    __asm__ volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

/* ======================================================================== */
/* PS/2 Controller helpers                                                  */
/* ======================================================================== */

static void mouse_wait_input(void) {
    for (int i = 0; i < 100000; i++) {
        if ((mouse_inb(MOUSE_PORT_STATUS) & 0x01) != 0) return;
    }
}

static void mouse_wait_output(void) {
    for (int i = 0; i < 100000; i++) {
        if ((mouse_inb(MOUSE_PORT_STATUS) & 0x02) == 0) return;
    }
}

static void mouse_send_cmd(uint8_t cmd) {
    mouse_wait_output();
    mouse_outb(MOUSE_PORT_CMD, 0xD4);
    mouse_wait_output();
    mouse_outb(MOUSE_PORT_DATA, cmd);
}

static uint8_t mouse_read_ack(void) {
    mouse_wait_input();
    return mouse_inb(MOUSE_PORT_DATA);
}

/* ======================================================================== */
/* Mouse Init                                                               */
/* ======================================================================== */

int mouse_init(void) {
    /* Zero state */
    mouse_state.x = 40;    /* Center of 80-wide screen */
    mouse_state.y = 12;    /* Center of 25-high screen */
    mouse_state.buttons = 0;
    mouse_state.prev_buttons = 0;
    mouse_state.dx = 0;
    mouse_state.dy = 0;
    mouse_state.ready = 0;
    mouse_state.active = 0;
    mouse_state.packet_idx = 0;
    mouse_state.wheel = 0;

    /* Enable auxiliary device (mouse) on PS/2 controller */
    mouse_wait_output();
    mouse_outb(MOUSE_PORT_CMD, 0xA8);
    mouse_read_ack(); /* consume ACK */

    /* Enable IRQ12 */
    mouse_wait_output();
    mouse_outb(MOUSE_PORT_CMD, 0x20);
    mouse_wait_input();
    uint8_t cmd_byte = mouse_inb(MOUSE_PORT_DATA);
    cmd_byte |= 0x02; /* Set bit 1 = enable IRQ12 */
    mouse_wait_output();
    mouse_outb(MOUSE_PORT_CMD, 0x60);
    mouse_wait_output();
    mouse_outb(MOUSE_PORT_DATA, cmd_byte);
    mouse_read_ack();

    /* Reset mouse */
    mouse_send_cmd(MOUSE_CMD_RESET);
    mouse_read_ack(); /* ACK */
    /* Wait for self-test result */
    for (int i = 0; i < 100000; i++) {
        if ((mouse_inb(MOUSE_PORT_STATUS) & 0x01)) {
            uint8_t val = mouse_inb(MOUSE_PORT_DATA);
            (void)val;
            break;
        }
    }

    /* Set defaults */
    mouse_send_cmd(MOUSE_CMD_SET_DEFAULT);
    mouse_read_ack();

    /* Set resolution: 3 = 8 counts/mm */
    mouse_send_cmd(MOUSE_CMD_SET_RESOLUTION);
    mouse_read_ack();
    mouse_send_cmd(3);
    mouse_read_ack();

    /* Set sample rate: 100 Hz */
    mouse_send_cmd(MOUSE_CMD_SET_SAMPLE);
    mouse_read_ack();
    mouse_send_cmd(100);
    mouse_read_ack();

    /* Enable data reporting */
    mouse_send_cmd(MOUSE_CMD_ENABLE_DATA);
    mouse_read_ack();

    mouse_state.active = 1;
    return 0;
}

/* ======================================================================== */
/* Mouse Packet Processing (called from IRQ12 handler)                      */
/* ======================================================================== */

void mouse_irq_handler(void) {
    if (!mouse_state.active) return;

    uint8_t data = mouse_inb(MOUSE_PORT_DATA);

    /* 3-byte packet: [buttons] [x-delta] [y-delta] */
    switch (mouse_state.packet_idx) {
        case 0:
            mouse_state.packet[0] = data;
            mouse_state.packet_idx = 1;
            break;
        case 1:
            mouse_state.packet[1] = data;
            mouse_state.packet_idx = 2;
            break;
        case 2:
            mouse_state.packet[2] = data;
            mouse_state.packet_idx = 0;

            /* Parse packet */
            mouse_state.prev_buttons = mouse_state.buttons;
            mouse_state.buttons = mouse_state.packet[0] & 0x07;
            mouse_state.dx = (int8_t)mouse_state.packet[1];
            mouse_state.dy = -(int8_t)mouse_state.packet[2]; /* Y is inverted */

            /* Scale from mouse counts to text cells (80x25) */
            /* PS/2 mouse: ~4 counts/mm at 8 counts/mm resolution */
            /* Divide by 4 to get roughly 1 cell per mm of mouse movement */
            int16_t text_dx = mouse_state.dx / 4;
            int16_t text_dy = mouse_state.dy / 3;

            /* Ensure at least 1 pixel of movement per packet if mouse moved */
            if (mouse_state.dx != 0 && text_dx == 0) text_dx = (mouse_state.dx > 0) ? 1 : -1;
            if (mouse_state.dy != 0 && text_dy == 0) text_dy = (mouse_state.dy > 0) ? 1 : -1;

            /* Update position in text coordinates */
            mouse_state.x += text_dx;
            mouse_state.y += text_dy;

            /* Clamp to screen bounds (80x25 text mode) */
            if (mouse_state.x < 0) mouse_state.x = 0;
            if (mouse_state.x > 79) mouse_state.x = 79;
            if (mouse_state.y < 0) mouse_state.y = 0;
            if (mouse_state.y > 24) mouse_state.y = 24;

            mouse_state.ready = 1;
            break;
    }
}

/* ======================================================================== */
/* Public API                                                               */
/* ======================================================================== */

int mouse_poll(void) {
    if (!mouse_state.active) return -1;
    if (mouse_state.ready) {
        mouse_state.ready = 0;
        return 0;
    }
    return -1;
}

void mouse_get_position(int16_t *x, int16_t *y) {
    *x = mouse_state.x;
    *y = mouse_state.y;
}

uint8_t mouse_get_buttons(void) {
    return mouse_state.buttons;
}

int mouse_left_pressed(void) {
    return (mouse_state.buttons & MOUSE_BTN_LEFT) &&
           !(mouse_state.prev_buttons & MOUSE_BTN_LEFT);
}

int mouse_right_pressed(void) {
    return (mouse_state.buttons & MOUSE_BTN_RIGHT) &&
           !(mouse_state.prev_buttons & MOUSE_BTN_RIGHT);
}

int mouse_left_released(void) {
    return !(mouse_state.buttons & MOUSE_BTN_LEFT) &&
           (mouse_state.prev_buttons & MOUSE_BTN_LEFT);
}

int mouse_right_released(void) {
    return !(mouse_state.buttons & MOUSE_BTN_RIGHT) &&
           (mouse_state.prev_buttons & MOUSE_BTN_RIGHT);
}

void mouse_set_position(int16_t x, int16_t y) {
    mouse_state.x = x;
    mouse_state.y = y;
}

void mouse_set_bounds(int16_t max_x, int16_t max_y) {
    if (mouse_state.x >= max_x) mouse_state.x = max_x - 1;
    if (mouse_state.y >= max_y) mouse_state.y = max_y - 1;
}
