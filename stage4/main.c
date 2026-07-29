#include <stdint.h>

void stage4_entry(void) {
    void (*stage3)(void) = (void (*)(void))0x10000;
    stage3();
    while (1) __asm__ volatile("hlt");
}
