#include <stdint.h>

#define STAGE4_SERIAL_PORT 0x3F8U
#define STAGE3_ENTRY       0x100000U

/* Stage 4 runs after stage 2 enters long mode.  A tiny serial trace makes the
 * integrity-verifier handoff visible without depending on stage 2 helpers. */
static void stage4_serial_putc(uint8_t ch) {
    __asm__ volatile("outb %0, %1" : : "a"(ch), "d"((uint16_t)STAGE4_SERIAL_PORT));
}

static void stage4_serial_puts(const char *text) {
    while (*text) stage4_serial_putc((uint8_t)*text++);
}

void stage4_entry(void) {
    stage4_serial_puts("[STAGE4] handing off to stage3 at 0x100000\n");

    void (*stage3)(void) = (void (*)(void))STAGE3_ENTRY;
    stage3();

    /* Stage 3 launches the kernel and should never return. */
    stage4_serial_puts("[STAGE4] unexpected stage3 return; halting\n");
    while (1) __asm__ volatile("hlt");
}
