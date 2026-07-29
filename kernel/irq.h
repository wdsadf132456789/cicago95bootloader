#ifndef IRQ_H
#define IRQ_H

#include <stdint.h>
#include "idt.h"

typedef void (*irq_handler_t)(isr_frame_t *);

void irq_init(void);
void irq_register_handler(int irq, irq_handler_t handler);
void irq_unregister_handler(int irq);
void irq_enable(int irq);
void irq_disable(int irq);
void irq_send_eoi(int irq);

extern irq_handler_t irq_handlers[16];

#endif
