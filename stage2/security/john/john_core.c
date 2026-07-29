#include "john_core.h"
#include "boot/security.h"

static uint16_t *vga_text = (uint16_t *)0xB8000;
static uint16_t vga_row = 0;
static uint16_t vga_col = 0;

typedef struct {
    uint8_t  initialized;
    john_hash_t target;
    john_stats_t stats;
    uint8_t  stop_flag;
    john_attack_config_t config;
} john_state_t;

static john_state_t state;

static void vga_put_char(char c) {
    if (c == '\n') {
        vga_col = 0;
        vga_row++;
        return;
    }
    uint32_t offset = (uint32_t)vga_row * 80 + (uint32_t)vga_col;
    vga_text[offset] = (uint16_t)c | ((uint16_t)0x07 << 8);
    vga_col++;
    if (vga_col >= 80) {
        vga_col = 0;
        vga_row++;
    }
}

static void vga_print(const char *str) {
    while (*str)
        vga_put_char(*str++);
}

int john_init(void) {
    sec_memzero(&state, sizeof(john_state_t));
    sec_memzero(&state.stats, sizeof(john_stats_t));

    int ret;
    ret = john_brute_init();
    if (ret != JOHN_OK)
        return ret;
    ret = john_wordlist_init();
    if (ret != JOHN_OK)
        return ret;
    ret = john_analyzer_init();
    if (ret != JOHN_OK)
        return ret;
    ret = john_table_init();
    if (ret != JOHN_OK)
        return ret;

    state.stats.initialized = 1;
    state.initialized = 1;

    vga_print("  [JOHN] John the Reaper: OK\n");

    return JOHN_OK;
}

int john_set_target(const john_hash_t *hash) {
    if (!hash)
        return JOHN_ERR_BAD_PARAM;
    if (!state.initialized)
        return JOHN_ERR_NOT_INIT;

    uint32_t i;
    state.target.type = hash->type;
    state.target.hash_len = hash->hash_len;
    state.target.salt_len = hash->salt_len;
    state.target.iterations = hash->iterations;
    state.target.format_id = hash->format_id;

    for (i = 0; i < hash->hash_len && i < 64; i++)
        state.target.hash[i] = hash->hash[i];
    for (i = 0; i < hash->salt_len && i < JOHN_MAX_SALT; i++)
        state.target.salt[i] = hash->salt[i];

    state.stats.current_hash_type = hash->type;

    return JOHN_OK;
}

static int crack_hybrid(const john_hash_t *target,
                         const john_attack_config_t *config,
                         john_result_t *result) {
    john_attack_config_t wl_config;
    int ret;

    sec_memzero(&wl_config, sizeof(john_attack_config_t));
    wl_config.mode = JOHN_ATTACK_WORDLIST;
    wl_config.hash_type = config->hash_type;
    wl_config.max_candidates = config->max_candidates;
    wl_config.max_time_ms = config->max_time_ms;
    wl_config.rule_count = config->rule_count;

    uint32_t i;
    for (i = 0; i < sizeof(john_attack_config_t); i++)
        ((uint8_t *)&wl_config)[i] = ((uint8_t *)config)[i];
    wl_config.mode = JOHN_ATTACK_WORDLIST;

    ret = john_wordlist_crack(target, &wl_config, result);
    if (result->found)
        return JOHN_OK;

    ret = john_brute_crack(target, config, result);
    return ret;
}

static int crack_mask(const john_hash_t *target,
                       const john_attack_config_t *config,
                       john_result_t *result) {
    john_attack_config_t brute_config;
    uint32_t i;

    sec_memzero(&brute_config, sizeof(john_attack_config_t));
    for (i = 0; i < sizeof(john_attack_config_t); i++)
        ((uint8_t *)&brute_config)[i] = ((uint8_t *)config)[i];
    brute_config.mode = JOHN_ATTACK_BRUTE;

    uint32_t mask_len = 0;
    while (mask_len < JOHN_MAX_PASSWORD && config->mask[mask_len] != '\0')
        mask_len++;

    uint8_t has_wildcard = 0;
    for (i = 0; i < mask_len; i++) {
        if (config->mask[i] == '?' || config->mask[i] == '*') {
            has_wildcard = 1;
            break;
        }
    }

    if (!has_wildcard) {
        john_result_t temp;
        sec_memzero(&temp, sizeof(john_result_t));
        temp.found = 0;
        temp.password[0] = '\0';
        temp.password_len = 0;
        temp.candidates_tried = 0;
        temp.time_ms = 0;
        temp.rate = 0;

        if (john_hash_compare(target, config->mask, mask_len)) {
            temp.found = 1;
            for (i = 0; i < mask_len; i++)
                temp.password[i] = config->mask[i];
            temp.password[mask_len] = '\0';
            temp.password_len = mask_len;
            sec_memzero(result, sizeof(john_result_t));
            uint32_t j;
            for (j = 0; j < sizeof(john_result_t); j++)
                ((uint8_t *)result)[j] = ((uint8_t *)&temp)[j];
            return JOHN_OK;
        }
        sec_memzero(result, sizeof(john_result_t));
        result->found = 0;
        result->password[0] = '\0';
        result->password_len = 0;
        result->candidates_tried = 1;
        result->time_ms = 0;
        result->rate = 0;
        return JOHN_ERR_NO_MATCH;
    }

    return john_brute_crack(target, &brute_config, result);
}

int john_crack(const john_attack_config_t *config, john_result_t *result) {
    if (!config || !result)
        return JOHN_ERR_BAD_PARAM;
    if (!state.initialized)
        return JOHN_ERR_NOT_INIT;

    int ret;
    state.stop_flag = 0;
    sec_memzero(result, sizeof(john_result_t));

    uint32_t i;
    for (i = 0; i < sizeof(john_attack_config_t); i++)
        ((uint8_t *)&state.config)[i] = ((uint8_t *)config)[i];

    state.stats.current_mode = config->mode;

    switch (config->mode) {
    case JOHN_ATTACK_WORDLIST:
        ret = john_wordlist_crack(&state.target, config, result);
        break;
    case JOHN_ATTACK_RULES:
        ret = john_wordlist_crack(&state.target, config, result);
        break;
    case JOHN_ATTACK_BRUTE:
        ret = john_brute_crack(&state.target, config, result);
        break;
    case JOHN_ATTACK_RAINBOW:
        ret = john_table_lookup(&state.target, result);
        break;
    case JOHN_ATTACK_HYBRID:
        ret = crack_hybrid(&state.target, config, result);
        break;
    case JOHN_ATTACK_MASK:
        ret = crack_mask(&state.target, config, result);
        break;
    default:
        return JOHN_ERR_BAD_PARAM;
    }

    state.stats.candidates_generated += result->candidates_tried;
    state.stats.current_rate = result->rate;
    if (result->rate > state.stats.peak_rate)
        state.stats.peak_rate = result->rate;
    state.stats.total_time_ms += result->time_ms;

    if (result->found)
        state.stats.passwords_cracked++;
    state.stats.passwords_tested += result->candidates_tried;

    return ret;
}

int john_analyze_password(const char *password, uint32_t len, uint8_t hash_type, john_result_t *result) {
    if (!password || !result)
        return JOHN_ERR_BAD_PARAM;
    if (!state.initialized)
        return JOHN_ERR_NOT_INIT;

    sec_memzero(result, sizeof(john_result_t));

    int score = john_analyzer_score(password, len, hash_type);

    uint32_t i;
    for (i = 0; i < len && i < JOHN_MAX_PASSWORD; i++)
        result->password[i] = password[i];
    result->password[len] = '\0';
    result->password_len = len;
    result->rate = (uint32_t)score;

    if (score < 50)
        result->found = 1;
    else
        result->found = 0;

    return JOHN_OK;
}

void john_stop(void) {
    state.stop_flag = 1;
}

void john_reset(void) {
    sec_memzero(&state, sizeof(john_state_t));

    state.stats.initialized = 1;
    state.initialized = 1;

    john_brute_reset();
    john_wordlist_reset();
    john_table_reset();
}

void john_get_stats(john_stats_t *stats) {
    if (!stats)
        return;
    uint32_t i;
    for (i = 0; i < sizeof(john_stats_t); i++)
        ((uint8_t *)stats)[i] = ((uint8_t *)&state.stats)[i];
}
