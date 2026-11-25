#include "kernel.h"
#include "boot_info.h"

extern uint8_t _bss_start;
extern uint8_t _bss_end;
extern void kernel_main(void);

/* Stage 3 passes the physical address of boot_info_t in RDI. */
void _start(boot_info_t *boot_info) {
    cli();
    uint8_t *bss = &_bss_start;
    while (bss < &_bss_end) {
        *bss++ = 0;
    }
    kernel_boot_info_init(boot_info);
    kernel_main();
}
