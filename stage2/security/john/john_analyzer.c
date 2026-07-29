#include "john_core.h"
#include "boot/security.h"

static john_stats_t g_stats;

static const char *common_words[] = {
    "password", "admin", "welcome", "letmein", "login",
    "master", "dragon", "monkey", "shadow", "sun",
    "love", "secret", "trust", "football", "batman",
    "access", "hello", "charlie", "donald", "michael",
    "superman", "princess", "ginger", "pepper", "thomas",
    "jennifer", "hunter", "ranger", "baseball", "hockey",
    0
};

static const char *patterns[] = {
    "123", "abc", "qwerty", "abcdef", "012",
    "xyz", "aaa", "111", "a1b2c3",
    0
};

static unsigned int int_log2(unsigned int v) {
    unsigned int c = 0;
    v >>= 1;
    while (v) {
        c++;
        v >>= 1;
    }
    return c;
}

static int to_lower_a(int c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

static int word_match(const char *pw, uint32_t pw_len, const char *word) {
    uint32_t wlen = 0;
    while (word[wlen]) wlen++;
    if (wlen != pw_len) return 0;
    for (uint32_t i = 0; i < pw_len; i++) {
        if (to_lower_a(pw[i]) != to_lower_a(word[i])) return 0;
    }
    return 1;
}

static int pattern_match(const char *pw, uint32_t pw_len, const char *pat) {
    uint32_t plen = 0;
    while (pat[plen]) plen++;
    if (plen > pw_len) return 0;
    for (uint32_t i = 0; i <= pw_len - plen; i++) {
        int match = 1;
        for (uint32_t j = 0; j < plen; j++) {
            if (to_lower_a(pw[i + j]) != pat[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

int john_analyzer_init(void) {
    sec_memzero(&g_stats, sizeof(john_stats_t));
    g_stats.initialized = 1;
    return JOHN_OK;
}

uint32_t john_analyzer_charset_size(const char *password, uint32_t len) {
    int has_lower = 0, has_upper = 0, has_digit = 0, has_symbol = 0;
    uint32_t size = 0;

    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)password[i];
        if (c >= 'a' && c <= 'z') has_lower = 1;
        else if (c >= 'A' && c <= 'Z') has_upper = 1;
        else if (c >= '0' && c <= '9') has_digit = 1;
        else has_symbol = 1;
    }

    if (has_lower) size += 26;
    if (has_upper) size += 26;
    if (has_digit) size += 10;
    if (has_symbol) size += 33;

    return size;
}

uint32_t john_analyzer_entropy(const char *password, uint32_t len) {
    uint32_t charset = john_analyzer_charset_size(password, len);
    if (charset == 0) return 0;
    return len * int_log2(charset);
}

int john_analyzer_score(const char *password, uint32_t len, uint8_t hash_type) {
    (void)hash_type;
    int score = 0;
    int has_lower = 0, has_upper = 0, has_digit = 0, has_symbol = 0;

    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)password[i];
        if (c >= 'a' && c <= 'z') has_lower = 1;
        else if (c >= 'A' && c <= 'Z') has_upper = 1;
        else if (c >= '0' && c <= '9') has_digit = 1;
        else has_symbol = 1;
    }

    if (has_lower) score += 15;
    if (has_upper) score += 15;
    if (has_digit) score += 15;
    if (has_symbol) score += 15;

    int bonus = (int)len * 5;
    if (bonus > 80) bonus = 80;
    score += bonus;

    for (uint32_t i = 0; common_words[i]; i++) {
        if (word_match(password, len, common_words[i])) {
            score -= 30;
            break;
        }
    }

    for (uint32_t i = 0; patterns[i]; i++) {
        if (pattern_match(password, len, patterns[i])) {
            score -= 20;
        }
    }

    int repeat_count = 1;
    for (uint32_t i = 1; i < len; i++) {
        if (password[i] == password[i - 1]) repeat_count++;
        else repeat_count = 1;
        if (repeat_count >= 3) {
            score -= 25;
            break;
        }
    }

    if (has_upper && has_lower && has_digit) score += 10;

    uint32_t mid = len / 2;
    if (mid > 0 && mid < len) {
        unsigned char mc = (unsigned char)password[mid];
        if (!(mc >= 'a' && mc <= 'z') && !(mc >= 'A' && mc <= 'Z') && !(mc >= '0' && mc <= '9')) {
            score += 10;
        }
    }

    if (score < 0) score = 0;
    if (score > 100) score = 100;

    g_stats.passwords_cracked++;
    g_stats.total_time_ms += john_analyzer_entropy(password, len);

    return score;
}

uint32_t john_analyzer_crack_time_seconds(const char *password, uint32_t len, uint8_t hash_type) {
    uint32_t charset = john_analyzer_charset_size(password, len);
    if (charset == 0 || len == 0) return 0;

    uint64_t rate;
    switch (hash_type) {
        case JOHN_HASH_MD5:    rate = 10000000ULL; break;
        case JOHN_HASH_SHA1:   rate = 5000000ULL; break;
        case JOHN_HASH_SHA256: rate = 1000000ULL; break;
        case JOHN_HASH_SHA512: rate = 100000ULL; break;
        case JOHN_HASH_BCRYPT: rate = 30000ULL; break;
        default:               rate = 10000000ULL; break;
    }

    uint64_t combinations = 1;
    for (uint32_t i = 0; i < len; i++) {
        combinations *= charset;
        if (combinations > 0xFFFFFFFFFFFFULL) return 0xFFFFFFFF;
    }

    uint64_t half = combinations / 2;
    uint32_t seconds = (uint32_t)(half / rate);
    if (seconds == 0 && half > 0) seconds = 1;

    return seconds;
}

int john_analyzer_suggest(const char *password, uint32_t len, char *suggestion, uint32_t sug_max) {
    uint32_t pos = 0;
    int has_lower = 0, has_upper = 0, has_digit = 0, has_symbol = 0;
    int types_used = 0;

    for (uint32_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)password[i];
        if (c >= 'a' && c <= 'z') has_lower = 1;
        else if (c >= 'A' && c <= 'Z') has_upper = 1;
        else if (c >= '0' && c <= '9') has_digit = 1;
        else has_symbol = 1;
    }

    if (has_lower) types_used++;
    if (has_upper) types_used++;
    if (has_digit) types_used++;
    if (has_symbol) types_used++;

    if (!has_upper && pos + 21 < sug_max) {
        const char *msg = "Add uppercase letters. ";
        for (uint32_t i = 0; msg[i] && pos + 1 < sug_max; i++) suggestion[pos++] = msg[i];
    }

    if (!has_digit && pos + 20 < sug_max) {
        const char *msg = "Add digits. ";
        for (uint32_t i = 0; msg[i] && pos + 1 < sug_max; i++) suggestion[pos++] = msg[i];
    }

    if (len < 12 && pos + 20 < sug_max) {
        const char *msg = "Increase length. ";
        for (uint32_t i = 0; msg[i] && pos + 1 < sug_max; i++) suggestion[pos++] = msg[i];
    }

    if (types_used <= 1 && pos + 22 < sug_max) {
        const char *msg = "Mix character types. ";
        for (uint32_t i = 0; msg[i] && pos + 1 < sug_max; i++) suggestion[pos++] = msg[i];
    }

    if (pos < sug_max) suggestion[pos] = '\0';
    else if (sug_max > 0) suggestion[sug_max - 1] = '\0';

    return (int)pos;
}

void john_analyzer_get_stats(john_stats_t *stats) {
    if (!stats) return;
    uint8_t *d = (uint8_t *)stats;
    const uint8_t *s = (const uint8_t *)&g_stats;
    for (uint32_t i = 0; i < sizeof(john_stats_t); i++) d[i] = s[i];
}
