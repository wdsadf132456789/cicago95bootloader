#include <stdint.h>
#include "cmos.h"
#include "console.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

#define RTC_SEC     0x00
#define RTC_MIN     0x02
#define RTC_HOUR    0x04
#define RTC_DAY     0x07
#define RTC_MON     0x08
#define RTC_YEAR    0x09
#define RTC_CENTURY 0x32
#define RTC_STAT_A  0x0A
#define RTC_STAT_B  0x0B

static inline uint8_t cmos_read(uint8_t reg) {
    __asm__ volatile("outb %0, %%dx" : : "a"(reg), "d"((uint16_t)CMOS_ADDR) : "memory");
    __asm__ volatile("outb %%al, %%dx" : : "a"((uint8_t)0x80), "d"((uint16_t)CMOS_ADDR) : "memory");
    uint8_t v;
    __asm__ volatile("inb %%dx, %0" : "=a"(v) : "d"((uint16_t)CMOS_DATA) : "memory");
    return v;
}

static uint8_t bcd_to_bin(uint8_t bcd) {
    return (bcd >> 4) * 10 + (bcd & 0xF);
}

static void rtc_wait_update(void) {
    while (cmos_read(RTC_STAT_A) & 0x80);
}

void cmos_read_rtc(rtc_time_t *t) {
    rtc_wait_update();
    uint8_t b = cmos_read(RTC_STAT_B);
    int is_bcd = !(b & 0x04);

    rtc_wait_update();
    uint8_t sec   = cmos_read(RTC_SEC);
    uint8_t min   = cmos_read(RTC_MIN);
    uint8_t hour  = cmos_read(RTC_HOUR);
    uint8_t day   = cmos_read(RTC_DAY);
    uint8_t mon   = cmos_read(RTC_MON);
    uint16_t year = cmos_read(RTC_YEAR);
    uint8_t cent  = cmos_read(RTC_CENTURY);

    if (is_bcd) {
        sec   = bcd_to_bin(sec);
        min   = bcd_to_bin(min);
        hour  = bcd_to_bin(hour);
        day   = bcd_to_bin(day);
        mon   = bcd_to_bin(mon);
        year  = bcd_to_bin((uint8_t)year);
        cent  = bcd_to_bin(cent);
    }

    if (!(b & 0x02)) {
        if (hour & 0x80) {
            hour = ((hour & 0x7F) + 12) % 24;
        }
    }

    t->second  = sec;
    t->minute  = min;
    t->hour    = hour;
    t->day     = day;
    t->month   = mon;
    t->year    = 2000 + year;
    t->century = cent;
}

void cmos_print(const rtc_time_t *t) {
    cons_dec32(t->year);
    cons_putc('-');
    if (t->month < 10) cons_putc('0');
    cons_dec32(t->month);
    cons_putc('-');
    if (t->day < 10) cons_putc('0');
    cons_dec32(t->day);
    cons_putc(' ');
    if (t->hour < 10) cons_putc('0');
    cons_dec32(t->hour);
    cons_putc(':');
    if (t->minute < 10) cons_putc('0');
    cons_dec32(t->minute);
    cons_putc(':');
    if (t->second < 10) cons_putc('0');
    cons_dec32(t->second);
}
