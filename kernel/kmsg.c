#include "kmsg.h"
#include "kernel.h"
#include "console.h"
#include "timer.h"
#include <stdarg.h>

static kmsg_entry_t kmsg_ring[KMSG_BUF_SIZE];
static uint32_t kmsg_head = 0;
static uint32_t kmsg_total = 0;

void kmsg_init(void) {
    for (uint32_t i = 0; i < KMSG_BUF_SIZE; i++) {
        kmsg_ring[i].timestamp = 0;
        kmsg_ring[i].level = KMSG_LOG;
        kmsg_ring[i].msg[0] = '\0';
    }
    kmsg_head = 0;
    kmsg_total = 0;
}

static void kmsg_vprintf(char *out, uint32_t max, const char *fmt, va_list args) {
    uint32_t pos = 0;
    while (*fmt && pos < max - 1) {
        if (*fmt == '%') {
            fmt++;
            switch (*fmt) {
                case 's': {
                    const char *s = __builtin_va_arg(args, const char *);
                    if (!s) s = "(null)";
                    while (*s && pos < max - 1) out[pos++] = *s++;
                    break;
                }
                case 'd': {
                    int64_t v = __builtin_va_arg(args, int64_t);
                    if (v < 0 && pos < max - 1) { out[pos++] = '-'; v = -v; }
                    char buf[21]; int i = 20; buf[i] = '\0';
                    do { buf[--i] = '0' + (v % 10); v /= 10; } while (v > 0);
                    while (buf[i] && pos < max - 1) out[pos++] = buf[i++];
                    break;
                }
                case 'u': {
                    uint64_t v = __builtin_va_arg(args, uint64_t);
                    char buf[21]; int i = 20; buf[i] = '\0';
                    do { buf[--i] = '0' + (v % 10); v /= 10; } while (v > 0);
                    while (buf[i] && pos < max - 1) out[pos++] = buf[i++];
                    break;
                }
                case 'x': {
                    uint64_t v = __builtin_va_arg(args, uint64_t);
                    const char *hex = "0123456789abcdef";
                    char buf[17]; int i = 16; buf[i] = '\0';
                    do { buf[--i] = hex[v & 0xF]; v >>= 4; } while (v > 0);
                    while (buf[i] && pos < max - 1) out[pos++] = buf[i++];
                    break;
                }
                case 'c': {
                    int c = __builtin_va_arg(args, int);
                    if (pos < max - 1) out[pos++] = (char)c;
                    break;
                }
                case '%':
                    if (pos < max - 1) out[pos++] = '%';
                    break;
                default:
                    if (pos < max - 1) out[pos++] = '%';
                    if (pos < max - 1) out[pos++] = *fmt;
                    break;
            }
        } else {
            out[pos++] = *fmt;
        }
        fmt++;
    }
    out[pos] = '\0';
}

void kmsg_add(kmsg_level_t level, const char *fmt, ...) {
    kmsg_entry_t *e = &kmsg_ring[kmsg_head];
    e->timestamp = timer_get_ticks();
    e->level = level;

    va_list args;
    __builtin_va_start(args, fmt);
    kmsg_vprintf(e->msg, KMSG_MAX_MSG, fmt, args);
    __builtin_va_end(args);

    kmsg_head = (kmsg_head + 1) % KMSG_BUF_SIZE;
    if (kmsg_total < KMSG_BUF_SIZE) kmsg_total++;
}

uint32_t kmsg_count(void) {
    return kmsg_total;
}

kmsg_entry_t *kmsg_get(uint32_t index) {
    if (index >= kmsg_total) return 0;
    uint32_t start;
    if (kmsg_total < KMSG_BUF_SIZE)
        start = 0;
    else
        start = kmsg_head;
    uint32_t real = (start + index) % KMSG_BUF_SIZE;
    return &kmsg_ring[real];
}

const char *kmsg_level_str(kmsg_level_t level) {
    switch (level) {
        case KMSG_EMERG:  return "EMERG";
        case KMSG_ALERT:  return "ALERT";
        case KMSG_CRIT:   return "CRIT ";
        case KMSG_ERR:    return "ERR  ";
        case KMSG_WARN:   return "WARN ";
        case KMSG_NOTICE: return "NOTE ";
        case KMSG_INFO:   return "INFO ";
        case KMSG_DEBUG:  return "DEBUG";
        case KMSG_LOG:    return "LOG  ";
        default:          return "?????";
    }
}

uint8_t kmsg_level_color(kmsg_level_t level) {
    switch (level) {
        case KMSG_EMERG:  return CONSOLE_RED | (CONSOLE_BLACK << 4);
        case KMSG_ALERT:  return CONSOLE_RED | (CONSOLE_BLACK << 4);
        case KMSG_CRIT:   return CONSOLE_LIGHT_RED | (CONSOLE_BLACK << 4);
        case KMSG_ERR:    return CONSOLE_LIGHT_RED | (CONSOLE_BLACK << 4);
        case KMSG_WARN:   return CONSOLE_YELLOW | (CONSOLE_BLACK << 4);
        case KMSG_NOTICE: return CONSOLE_LIGHT_CYAN | (CONSOLE_BLACK << 4);
        case KMSG_INFO:   return CONSOLE_LIGHT_GREY | (CONSOLE_BLACK << 4);
        case KMSG_DEBUG:  return CONSOLE_DARK_GREY | (CONSOLE_BLACK << 4);
        case KMSG_LOG:    return CONSOLE_WHITE | (CONSOLE_BLACK << 4);
        default:          return CONSOLE_WHITE | (CONSOLE_BLACK << 4);
    }
}
