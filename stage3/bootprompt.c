#include <stdint.h>
#include "console.h"
#include "kbd.h"
#include "disk.h"

#define STAGE_BASE_LBA    0x800
#define STAGE_BASE_ADDR   0x30000
#define STAGE_LBA_STEP    16
#define STAGE_ADDR_STEP   0x10000
#define STAGE_MAX_SECT    64

static void stage5_load(void) {
    cons_color("Loading stages 5-100...\n", COL_WARN);

    for (int s = 5; s <= 100; s++) {
        uint32_t lba = STAGE_BASE_LBA + (s - 5) * STAGE_LBA_STEP;
        void *addr = (void *)(STAGE_BASE_ADDR + (s - 5) * STAGE_ADDR_STEP);

        if (disk_ata_read(lba, STAGE_MAX_SECT, addr))
            cons_color("  Stage ", COL_DIM);
        else
            cons_color("  [WARN] Stage ", COL_WARN);

        cons_dec32(s);
        cons_color(" loaded at 0x", COL_DIM);
        cons_hex32((uint32_t)(uint64_t)addr);
        cons_color("\n", COL_DIM);
    }

    cons_color("All 100 stages loaded.\n", COL_OK);
}

static void delay_loop(uint32_t count) {
    for (volatile uint32_t i = 0; i < count; i++)
        __asm__ volatile("pause");
}

int boot_prompt(void) {
    kbd_init();
    kbd_flush();

    cons_color("\n", COL_DEFAULT);
    cons_color("  Press any key for boot menu...\n", COL_HDR);

    for (int i = 50; i >= 0; i--) {
        cons_set_cursor(24, 2);
        cons_clear_line(24);
        cons_color("Booting in ", COL_DIM);
        cons_dec32(i / 10 + 1);
        cons_color("  ", COL_DIM);

        if (kbd_is_key()) {
            uint8_t sc = kbd_get_scancode();
            if (!(sc & 0x80)) {
                cons_color("[ KEY ]\n", COL_HI);
                stage5_load();

                cons_color("Starting boot manager...\n", COL_HDR);
                void (*stage5)(void) = (void (*)(void))STAGE_BASE_ADDR;
                stage5();

                cons_color("Boot manager returned. Booting kernel...\n", COL_WARN);
                return 0;
            }
        }

        delay_loop(5000000);
    }

    cons_color("BOOT\n", COL_OK);
    return 0;
}
