#include <stdint.h>
#include "console.h"
#include "e820.h"

uint32_t e820_get_count(void) {
    uint32_t *hdr = (uint32_t *)E820_MAP_ADDR;
    if (hdr[0] == 0x30303845)
        return hdr[1];
    return 0;
}

uint64_t e820_total_ram(void) {
    uint32_t count = e820_get_count();
    uint64_t total = 0;
    for (uint32_t i = 0; i < count; i++) {
        uint32_t *ent32 = (uint32_t *)(uint64_t)(E820_MAP_ADDR + 8 + i * 20);
        uint64_t len = (uint64_t)ent32[2] | ((uint64_t)ent32[3] << 32);
        uint32_t type = ent32[4];
        if (type == 1) total += len;
    }
    return total;
}

void e820_report(void) {
    cons_color("  Reading E820 memory map...\n", COL_DEFAULT);
    uint32_t count = e820_get_count();
    if (count > 0) {
        cons_color("    E820 map at 0x8000: ", COL_OK);
        cons_dec32(count);
        cons_color(" entries\n", COL_OK);
        uint64_t ram = e820_total_ram();
        cons_color("    Total usable RAM: ", COL_DEFAULT);
        cons_dec32((uint32_t)(ram / (1024 * 1024)));
        cons_color(" MB\n", COL_DEFAULT);
    } else {
        cons_color("    E820 map not found\n", COL_WARN);
    }
}
