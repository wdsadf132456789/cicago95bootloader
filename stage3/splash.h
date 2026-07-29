#ifndef STAGE3_SPLASH_H
#define STAGE3_SPLASH_H

#include <stdint.h>

void splash_banner(void);
void splash_dash_item(uint8_t row, const char *label, const char *value, uint8_t val_color);
void splash_dash_sep(uint8_t row);
void splash_phase(const char *phase, int done);

#endif