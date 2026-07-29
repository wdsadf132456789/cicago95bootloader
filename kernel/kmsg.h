#ifndef KMSG_H
#define KMSG_H

#include <stdint.h>

#define KMSG_BUF_SIZE   256
#define KMSG_MAX_MSG    128

typedef enum {
    KMSG_EMERG = 0,
    KMSG_ALERT,
    KMSG_CRIT,
    KMSG_ERR,
    KMSG_WARN,
    KMSG_NOTICE,
    KMSG_INFO,
    KMSG_DEBUG,
    KMSG_LOG
} kmsg_level_t;

typedef struct {
    uint64_t     timestamp;
    kmsg_level_t level;
    char         msg[KMSG_MAX_MSG];
} kmsg_entry_t;

void     kmsg_init(void);
void     kmsg_add(kmsg_level_t level, const char *fmt, ...);
uint32_t kmsg_count(void);
kmsg_entry_t *kmsg_get(uint32_t index);
const char *kmsg_level_str(kmsg_level_t level);
uint8_t  kmsg_level_color(kmsg_level_t level);

#define kmsg_emerg(fmt, ...) kmsg_add(KMSG_EMERG, fmt, ##__VA_ARGS__)
#define kmsg_alert(fmt, ...) kmsg_add(KMSG_ALERT, fmt, ##__VA_ARGS__)
#define kmsg_crit(fmt, ...)  kmsg_add(KMSG_CRIT, fmt, ##__VA_ARGS__)
#define kmsg_err(fmt, ...)   kmsg_add(KMSG_ERR, fmt, ##__VA_ARGS__)
#define kmsg_warn(fmt, ...)  kmsg_add(KMSG_WARN, fmt, ##__VA_ARGS__)
#define kmsg_notice(fmt, ...) kmsg_add(KMSG_NOTICE, fmt, ##__VA_ARGS__)
#define kmsg_info(fmt, ...)  kmsg_add(KMSG_INFO, fmt, ##__VA_ARGS__)
#define kmsg_debug(fmt, ...) kmsg_add(KMSG_DEBUG, fmt, ##__VA_ARGS__)
#define kmsg_log(fmt, ...)   kmsg_add(KMSG_LOG, fmt, ##__VA_ARGS__)

#endif
