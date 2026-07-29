#ifndef IDT_H
#define IDT_H

#include <stdint.h>

typedef void (*isr_handler_t)(void);

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

typedef struct {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t int_num, error_code;
    uint64_t rip, cs, rflags, rsp, ss;
} __attribute__((packed)) isr_frame_t;

void idt_init(void);
void idt_set_gate(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags);
void idt_load(void);

extern idt_entry_t idt[256];
extern idt_ptr_t idt_ptr;

typedef void (*exception_handler_t)(isr_frame_t *);

void register_exception_handler(int num, exception_handler_t handler);

extern void *isr_stub_table[256];

#endif
