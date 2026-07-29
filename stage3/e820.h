#ifndef STAGE3_E820_H
#define STAGE3_E820_H

#include <stdint.h>

#define E820_MAP_ADDR 0x8000

uint32_t e820_get_count(void);
uint64_t e820_total_ram(void);
void e820_report(void);

#endif