#include "kernel.h"

extern uint8_t _bss_start;
extern uint8_t _bss_end;
extern void kernel_main(void);

void _start(void) {
    cli();
    uint8_t *bss = &_bss_start;
    while (bss < &_bss_end) {
        *bss++ = 0;
    }
    kernel_main();
}
