#ifndef STAGE3_CMOS_H
#define STAGE3_CMOS_H

#include <stdint.h>

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  century;
} rtc_time_t;

void cmos_read_rtc(rtc_time_t *t);
void cmos_print(const rtc_time_t *t);

#endif