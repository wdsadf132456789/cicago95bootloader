#include "irq.h"
#include "kernel.h"

irq_handler_t irq_handlers[16] = {0};

void irq_init(void) {
    outb(0x20, 0x11);
    io_wait();
    outb(0xA0, 0x11);
    io_wait();
    outb(0x21, 0x20);
    io_wait();
    outb(0xA1, 0x28);
    io_wait();
    outb(0x21, 0x04);
    io_wait();
    outb(0xA1, 0x02);
    io_wait();
    outb(0x21, 0x01);
    io_wait();
    outb(0xA1, 0x01);
    io_wait();
    outb(0x21, 0x0);
    io_wait();
    outb(0xA1, 0x0);
    io_wait();
}

void irq_register_handler(int irq, irq_handler_t handler) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = handler;
    }
}

void irq_unregister_handler(int irq) {
    if (irq >= 0 && irq < 16) {
        irq_handlers[irq] = 0;
    }
}

void irq_enable(int irq) {
    if (irq < 8) {
        outb(0x21, inb(0x21) & ~(1 << irq));
    } else {
        outb(0xA1, inb(0xA1) & ~(1 << (irq - 8)));
        outb(0x21, inb(0x21) & ~2);
    }
}

void irq_disable(int irq) {
    if (irq < 8) {
        outb(0x21, inb(0x21) | (1 << irq));
    } else {
        outb(0xA1, inb(0xA1) | (1 << (irq - 8)));
    }
}

void irq_send_eoi(int irq) {
    if (irq >= 8) {
        outb(0xA0, 0x20);
    }
    outb(0x20, 0x20);
}
