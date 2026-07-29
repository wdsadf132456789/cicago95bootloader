#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>
#include "idt.h"

typedef void (*timer_callback_t)(void);

void timer_init(void);
uint64_t timer_get_ticks(void);
uint64_t timer_get_seconds(void);
void timer_sleep(uint64_t ms);
void timer_register_callback(timer_callback_t cb);
void timer_handler(isr_frame_t *frame);
void timer_set_scheduler_interval(uint32_t ticks);

#endif
