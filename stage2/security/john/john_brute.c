#include "john_core.h"
#include "boot/security.h"
#include "boot/ring0_init.h"

static uint8_t brute_initialized;
static uint8_t position[JOHN_MAX_PASSWORD];
static uint8_t current_len;
static uint64_t candidates_tried;

static char charset_table[256];
static uint8_t charset_len;

static void build_charset_table(uint8_t charset_flags) {
    uint8_t idx = 0;
    if (charset_flags & JOHN_CHARSET_LOWER) {
        for (char c = 'a'; c <= 'z'; c++)
            charset_table[idx++] = (uint8_t)c;
    }
    if (charset_flags & JOHN_CHARSET_UPPER) {
        for (char c = 'A'; c <= 'Z'; c++)
            charset_table[idx++] = (uint8_t)c;
    }
    if (charset_flags & JOHN_CHARSET_DIGITS) {
        for (char c = '0'; c <= '9'; c++)
            charset_table[idx++] = (uint8_t)c;
    }
    if (charset_flags & JOHN_CHARSET_SYMBOLS) {
        const char symbols[] = "!@#$%^&*()-_+=~`[]{}|;':\",./<>?";
        for (uint32_t i = 0; i < sizeof(symbols) - 1; i++)
            charset_table[idx++] = (uint8_t)symbols[i];
    }
    charset_len = idx;
}

int john_brute_init(void) {
    uint32_t i;
    for (i = 0; i < JOHN_MAX_PASSWORD; i++)
        position[i] = 0;
    current_len = 0;
    candidates_tried = 0;
    charset_len = 0;
    brute_initialized = 1;
    return JOHN_OK;
}

void john_brute_reset(void) {
    uint32_t i;
    for (i = 0; i < JOHN_MAX_PASSWORD; i++)
        position[i] = 0;
    current_len = 0;
    candidates_tried = 0;
}

static int increment_odometer(uint8_t len) {
    int pos = (int)len - 1;
    while (pos >= 0) {
        if (position[pos] < charset_len - 1) {
            position[pos]++;
            return 1;
        }
        position[pos] = 0;
        pos--;
    }
    return 0;
}

static void set_all_zero(uint8_t len) {
    uint8_t i;
    for (i = 0; i < len; i++)
        position[i] = 0;
}

int john_brute_next(john_candidate_t *candidate, const john_attack_config_t *config) {
    uint8_t i;

    if (!brute_initialized)
        return JOHN_ERR_NOT_INIT;

    if (charset_len == 0)
        build_charset_table(config->charset);

    if (current_len < config->min_len) {
        current_len = config->min_len;
        set_all_zero(current_len);
    }

    for (i = 0; i < current_len; i++)
        candidate->password[i] = charset_table[position[i]];
    candidate->password[current_len] = '\0';
    candidate->len = current_len;
    candidate->candidates_tried = candidates_tried;

    if (!increment_odometer(current_len)) {
        if (current_len >= config->max_len)
            return JOHN_ERR_OVERFLOW;
        current_len++;
        set_all_zero(current_len);
    }

    candidates_tried++;
    return JOHN_OK;
}

static uint64_t compute_rate(uint64_t start_tsc, uint64_t end_tsc, uint64_t count) {
    uint64_t elapsed = end_tsc - start_tsc;
    if (elapsed == 0 || count == 0)
        return 0;
    if (ring0_state.tsc_per_ms == 0)
        return count;
    uint64_t elapsed_ms = elapsed / ring0_state.tsc_per_ms;
    if (elapsed_ms == 0)
        return count;
    return (count * 1000) / elapsed_ms;
}

int john_brute_crack(const john_hash_t *target, const john_attack_config_t *config, john_result_t *result) {
    john_candidate_t candidate;
    int ret;
    uint64_t tsc_start, tsc_now;
    uint64_t checked = 0;

    if (!brute_initialized)
        return JOHN_ERR_NOT_INIT;

    if (!target || !config || !result)
        return JOHN_ERR_BAD_PARAM;

    result->found = 0;
    result->password[0] = '\0';
    result->password_len = 0;
    result->candidates_tried = 0;
    result->time_ms = 0;
    result->rate = 0;

    build_charset_table(config->charset);

    if (charset_len == 0)
        return JOHN_ERR_BAD_PARAM;

    john_brute_reset();

    tsc_start = ring0_rdtsc();

    for (;;) {
        ret = john_brute_next(&candidate, config);
        if (ret != JOHN_OK)
            break;

        if (config->max_candidates != 0 && checked >= config->max_candidates)
            break;

        if (john_hash_compare(target, candidate.password, candidate.len)) {
            result->found = 1;
            uint32_t i;
            for (i = 0; i < candidate.len; i++)
                result->password[i] = candidate.password[i];
            result->password[candidate.len] = '\0';
            result->password_len = candidate.len;
            break;
        }

        checked++;

        if ((checked & 0xFFF) == 0) {
            tsc_now = ring0_rdtsc();
            if (config->max_time_ms != 0 && ring0_state.tsc_per_ms != 0) {
                uint64_t elapsed_ms = (tsc_now - tsc_start) / ring0_state.tsc_per_ms;
                if (elapsed_ms >= config->max_time_ms)
                    break;
            }
        }
    }

    tsc_now = ring0_rdtsc();

    if (ring0_state.tsc_per_ms != 0)
        result->time_ms = (tsc_now - tsc_start) / ring0_state.tsc_per_ms;
    else
        result->time_ms = 0;

    result->candidates_tried = candidates_tried;
    result->rate = (uint32_t)compute_rate(tsc_start, tsc_now, candidates_tried);

    if (!result->found && ret != JOHN_OK && ret != JOHN_ERR_OVERFLOW)
        return ret;

    return result->found ? JOHN_OK : JOHN_ERR_NO_MATCH;
}
