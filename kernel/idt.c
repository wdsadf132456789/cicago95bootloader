#include "idt.h"
#include "irq.h"
#include "kernel.h"

idt_entry_t idt[256];
idt_ptr_t idt_ptr;

exception_handler_t exception_handlers[32] = {0};

void register_exception_handler(int num, exception_handler_t handler) {
    if (num < 32) {
        exception_handlers[num] = handler;
    }
}

void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = base & 0xFFFF;
    idt[num].offset_mid = (base >> 16) & 0xFFFF;
    idt[num].offset_high = (base >> 32) & 0xFFFFFFFF;
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].type_attr = flags;
    idt[num].reserved = 0;
}

void idt_init(void) {
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base = (uint64_t)&idt;

    __builtin_memset(idt, 0, sizeof(idt_entry_t) * 256);

    for (int i = 0; i < 256; i++) {
        uint64_t stub = (uint64_t)isr_stub_table[i];
        idt_set_gate(i, stub, 0x08, 0x8E);
    }

    idt_set_gate(0x80, (uint64_t)isr_stub_table[0x80], 0x08, 0xEE);

    idt_load();
}

static const char *exception_names[] = {
    "Divide Error", "Debug", "NMI", "Breakpoint", "Overflow",
    "Bound Range Exceeded", "Invalid Opcode", "Device Not Available",
    "Double Fault", "Coprocessor Segment Overrun", "Invalid TSS",
    "Segment Not Present", "Stack-Segment Fault", "General Protection Fault",
    "Page Fault", "Reserved", "x87 FP Exception", "Alignment Check",
    "Machine Check", "SIMD FP Exception", "Virtualization Exception",
    "Control Protection Exception"
};

extern void console_puts(const char *s, uint8_t color);
extern void console_printf(const char *fmt, ...);
extern void serial_puts(const char *s);
extern void serial_printf(const char *fmt, ...);
extern void timer_handler(isr_frame_t *frame);
extern void keyboard_handler(isr_frame_t *frame);

void isr_handler(isr_frame_t *frame) {
    uint64_t int_num = frame->int_num;

    if (int_num < 32) {
        if (exception_names[int_num]) {
            serial_printf("EXCEPTION: %s (int %u)\n", exception_names[int_num], int_num);
            console_printf("EXCEPTION: %s (int %u)\n", exception_names[int_num], int_num);
        } else {
            serial_printf("EXCEPTION: Unknown (int %u)\n", int_num);
        }
        serial_printf("RIP=%x RSP=%x RFLAGS=%x\n", frame->rip, frame->rsp, frame->rflags);
        serial_printf("RAX=%x RBX=%x RCX=%x RDX=%x\n", frame->rax, frame->rbx, frame->rcx, frame->rdx);
        serial_printf("RSI=%x RDI=%x RBP=%x\n", frame->rsi, frame->rdi, frame->rbp);
        serial_printf("Error code: %x\n", frame->error_code);
        if (int_num == 14) {
            uint64_t faulting_addr;
            __asm__ volatile ("mov %%cr2, %0" : "=r"(faulting_addr));
            serial_printf("Page fault at address: %x\n", faulting_addr);
            console_printf("Page fault at address: %x\n", faulting_addr);
        }
        while (1) {
            __asm__ volatile ("cli\nhlt");
        }
    }

    if (int_num >= 32 && int_num < 48) {
        int irq = int_num - 32;
        if (irq == 0) {
            timer_handler(frame);
        } else if (irq == 1) {
            keyboard_handler(frame);
        }
        irq_send_eoi(irq);
        return;
    }

    if (int_num == 0x80) {
        return;
    }
}
