#include "timer.h"
#include "idt.h"
#include "kernel.h"
#include "process.h"

static volatile uint64_t timer_ticks = 0;
static timer_callback_t timer_callback = 0;
static uint32_t scheduler_interval = 10;  /* Call scheduler every 10 ticks (100ms) */

void timer_init(void) {
    uint32_t divisor = 1193182 / 100;
    outb(0x43, 0x36);
    io_wait();
    outb(0x40, divisor & 0xFF);
    io_wait();
    outb(0x40, (divisor >> 8) & 0xFF);
}

uint64_t timer_get_ticks(void) {
    return timer_ticks;
}

uint64_t timer_get_seconds(void) {
    return timer_ticks / 100;
}

void timer_sleep(uint64_t ms) {
    uint64_t target = timer_ticks + ms;
    while (timer_ticks < target) {
        hlt();
    }
}

void timer_register_callback(timer_callback_t cb) {
    timer_callback = cb;
}

void timer_set_scheduler_interval(uint32_t ticks) {
    scheduler_interval = ticks;
}

void timer_handler(isr_frame_t *frame) {
    (void)frame;
    timer_ticks++;

    /* Call user-registered callback */
    if (timer_callback) {
        timer_callback();
    }

    /* Periodically invoke the process scheduler */
    if (scheduler_interval > 0 && (timer_ticks % scheduler_interval) == 0) {
        process_scheduler();
    }
}
