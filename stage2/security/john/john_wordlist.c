#include "john_core.h"
#include "boot/security.h"
#include "boot/ring0_init.h"

static const char *john_wordlist_builtin[] = {
    "password", "123456", "12345678", "qwerty", "abc123",
    "monkey", "master", "dragon", "111111", "baseball",
    "iloveyou", "trustno1", "sunshine", "letmein", "shadow",
    "superman", "michael", "football", "batman", "access",
    "hello", "charlie", "donald", "000000", "1234",
    "password1", "admin", "welcome", "login", "master123",
    "passw0rd", "p@ssword", "pass123", "password!", "secret",
    "p@ssw0rd", "qwerty123", "letmein1", "123456789", "welcome1",
    "abc1234", "pass1234", "1qaz2wsx", "qwertyuiop", "1234567890",
    "654321", "computer", "whatever", "123123", "dragon1",
    "master1", "love", "ginger", "princess", "sunshine1",
    "baseball1", "hockey", "ranger", "jennifer", "hunter",
    "thomas", "football1", "charlie1", "donald1", "batman1",
    "access1", "hello1", "superman1", "michael1", "jordan",
    "trustno1", "pepper", "zxcvbnm", "admin123", "test",
    "test123", "summer", "winter", "spring", "autumn",
    "1234qwer", "abcd1234", "pass", "secret1", "mypassword",
    "changeme", "default", "root", "toor", "p@ss1",
    "root123", "ubuntu", "centos", "fedora", "debian",
    "kali", "linux", "windows", "macbook", "iphone",
    "android", "1234abcd", "qwerty1", "abcdef", "a1b2c3",
    "121212", "112233", "101010", "202020", "222222",
    "333333", "444444", "555555", "666666", "777777",
    "888888", "999999", "password123", "admin123", "welcome123",
    "letmein123", "hello123", "dragon123", "master123", "login123",
    "abc12345", "pass12345"
};

const john_wordlist_t john_wordlist_common = {
    .words = john_wordlist_builtin,
    .count = sizeof(john_wordlist_builtin) / sizeof(john_wordlist_builtin[0])
};

static struct {
    const char **words;
    uint32_t count;
    uint32_t index;
    uint64_t rate_start;
    uint64_t candidates_tested;
    uint8_t  initialized;
} wl_state;

static uint32_t strlen_bare(const char *s) {
    uint32_t len = 0;
    while (s[len]) len++;
    return len;
}

static void memcpy_bare(void *dst, const void *src, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < len; i++)
        d[i] = s[i];
}

static uint8_t is_upper(char c) {
    return (c >= 'A' && c <= 'Z');
}

static uint8_t is_lower(char c) {
    return (c >= 'a' && c <= 'z');
}

static char to_upper(char c) {
    if (c >= 'a' && c <= 'z')
        return c - 32;
    return c;
}

static char to_lower(char c) {
    if (c >= 'A' && c <= 'Z')
        return c + 32;
    return c;
}

int john_wordlist_init(void) {
    wl_state.words = (const char **)john_wordlist_common.words;
    wl_state.count = john_wordlist_common.count;
    wl_state.index = 0;
    wl_state.rate_start = 0;
    wl_state.candidates_tested = 0;
    wl_state.initialized = 1;
    return JOHN_OK;
}

int john_wordlist_load(const char **words, uint32_t count) {
    if (!words || count == 0)
        return JOHN_ERR_BAD_PARAM;
    wl_state.words = words;
    wl_state.count = count;
    wl_state.index = 0;
    return JOHN_OK;
}

int john_wordlist_next(john_candidate_t *candidate) {
    if (!candidate)
        return JOHN_ERR_BAD_PARAM;
    if (!wl_state.initialized)
        return JOHN_ERR_NOT_INIT;
    if (wl_state.index >= wl_state.count)
        return JOHN_ERR_NO_MATCH;

    const char *word = wl_state.words[wl_state.index];
    uint32_t len = strlen_bare(word);
    if (len >= JOHN_MAX_PASSWORD)
        len = JOHN_MAX_PASSWORD - 1;
    memcpy_bare(candidate->password, word, len);
    candidate->password[len] = '\0';
    candidate->len = (uint8_t)len;
    candidate->candidates_tried = wl_state.index + 1;
    wl_state.index++;
    return JOHN_OK;
}

int john_wordlist_apply_rules(const char *word, uint32_t word_len,
                               const john_rule_t *rules, uint32_t rule_count,
                               char *out, uint32_t out_max) {
    if (!word || !out || out_max == 0)
        return JOHN_ERR_BAD_PARAM;

    uint32_t total = 0;

    for (uint32_t r = 0; r < rule_count; r++) {
        uint32_t data_len = strlen_bare(rules[r].data);
        uint32_t result_len = 0;
        uint8_t rejected = 0;

        switch (rules[r].type) {
        case 0:
            result_len = data_len + word_len;
            if (result_len >= out_max) { rejected = 1; break; }
            for (uint32_t i = 0; i < data_len; i++)
                out[total + i] = rules[r].data[i];
            for (uint32_t i = 0; i < word_len; i++)
                out[total + data_len + i] = word[i];
            out[total + result_len] = '\0';
            total += result_len + 1;
            break;

        case 1:
            result_len = word_len + data_len;
            if (result_len >= out_max) { rejected = 1; break; }
            for (uint32_t i = 0; i < word_len; i++)
                out[total + i] = word[i];
            for (uint32_t i = 0; i < data_len; i++)
                out[total + word_len + i] = rules[r].data[i];
            out[total + result_len] = '\0';
            total += result_len + 1;
            break;

        case 2:
            result_len = word_len;
            if (result_len >= out_max) { rejected = 1; break; }
            for (uint32_t i = 0; i < word_len; i++)
                out[total + i] = word[i];
            if (rules[r].position < word_len && data_len > 0) {
                out[total + rules[r].position] = rules[r].data[0];
            }
            out[total + result_len] = '\0';
            total += result_len + 1;
            break;

        case 3:
            result_len = word_len * 2;
            if (result_len >= out_max) { rejected = 1; break; }
            for (uint32_t i = 0; i < word_len; i++)
                out[total + i] = word[i];
            for (uint32_t i = 0; i < word_len; i++)
                out[total + word_len + i] = word[i];
            out[total + result_len] = '\0';
            total += result_len + 1;
            break;

        case 4:
            result_len = word_len;
            if (result_len >= out_max) { rejected = 1; break; }
            for (uint32_t i = 0; i < word_len; i++)
                out[total + i] = word[i];
            if (rules[r].position < word_len) {
                if (is_upper(out[total + rules[r].position]))
                    out[total + rules[r].position] = to_lower(word[rules[r].position]);
                else
                    out[total + rules[r].position] = to_upper(word[rules[r].position]);
            }
            out[total + result_len] = '\0';
            total += result_len + 1;
            break;

        case 5:
            rejected = 0;
            if (data_len > 0 && data_len == word_len) {
                uint8_t match = 1;
                for (uint32_t i = 0; i < word_len; i++) {
                    if (word[i] != rules[r].data[i]) {
                        match = 0;
                        break;
                    }
                }
                if (match)
                    rejected = 1;
            }
            if (!rejected) {
                result_len = word_len;
                if (result_len >= out_max) { rejected = 1; break; }
                for (uint32_t i = 0; i < word_len; i++)
                    out[total + i] = word[i];
                out[total + result_len] = '\0';
                total += result_len + 1;
            }
            break;

        default:
            break;
        }

        if (total >= out_max)
            break;
    }

    return (int)total;
}

static uint32_t compute_rate(uint64_t candidates, uint64_t tsc_start, uint64_t tsc_end) {
    uint64_t elapsed = tsc_end - tsc_start;
    if (elapsed == 0)
        return 0;
    uint64_t tsc_per_ms = ring0_state.tsc_per_ms;
    if (tsc_per_ms == 0)
        tsc_per_ms = 1;
    uint64_t ms = elapsed / tsc_per_ms;
    if (ms == 0)
        ms = 1;
    return (uint32_t)(candidates * 1000 / ms);
}

int john_wordlist_crack(const john_hash_t *target, const john_attack_config_t *config, john_result_t *result) {
    if (!target || !result)
        return JOHN_ERR_BAD_PARAM;
    if (!wl_state.initialized)
        return JOHN_ERR_NOT_INIT;

    char rule_buf[JOHN_MAX_PASSWORD * 2];
    uint64_t tsc_start, tsc_now;
    uint64_t max_tsc = 0;

    if (config && config->max_time_ms > 0 && ring0_state.tsc_per_ms > 0)
        max_tsc = config->max_time_ms * ring0_state.tsc_per_ms;

    result->found = 0;
    result->candidates_tried = 0;
    result->password_len = 0;
    result->rate = 0;

    tsc_start = ring0_rdtsc();
    wl_state.rate_start = tsc_start;
    wl_state.candidates_tested = 0;

    while (wl_state.index < wl_state.count) {
        if (wl_state.index >= wl_state.count)
            break;

        const char *word = wl_state.words[wl_state.index];
        uint32_t word_len = strlen_bare(word);
        wl_state.index++;

        if (word_len >= JOHN_MAX_PASSWORD)
            continue;

        if (john_hash_compare(target, word, word_len)) {
            tsc_now = ring0_rdtsc();
            result->found = 1;
            memcpy_bare(result->password, word, word_len);
            result->password[word_len] = '\0';
            result->password_len = (uint8_t)word_len;
            result->candidates_tried = wl_state.candidates_tested + 1;
            result->rate = compute_rate(wl_state.candidates_tested + 1, tsc_start, tsc_now);
            result->time_ms = 0;
            if (ring0_state.tsc_per_ms > 0)
                result->time_ms = (uint32_t)((tsc_now - tsc_start) / ring0_state.tsc_per_ms);
            return JOHN_OK;
        }

        wl_state.candidates_tested++;

        if (max_tsc > 0) {
            tsc_now = ring0_rdtsc();
            if ((tsc_now - tsc_start) >= max_tsc)
                break;
        }

        if (config && config->rule_count > 0) {
            const john_rule_t *rules = (const john_rule_t *)config->rules;
            uint32_t rule_count = config->rule_count;
            if (rule_count > JOHN_MAX_RULES)
                rule_count = JOHN_MAX_RULES;

            int mutated = john_wordlist_apply_rules(word, word_len, rules, rule_count,
                                                     rule_buf, sizeof(rule_buf));
            if (mutated > 0) {
                uint32_t pos = 0;
                while (pos < (uint32_t)mutated) {
                    uint32_t rlen = strlen_bare(rule_buf + pos);
                    if (rlen >= JOHN_MAX_PASSWORD) {
                        pos += rlen + 1;
                        continue;
                    }

                    if (john_hash_compare(target, rule_buf + pos, rlen)) {
                        tsc_now = ring0_rdtsc();
                        result->found = 1;
                        memcpy_bare(result->password, rule_buf + pos, rlen);
                        result->password[rlen] = '\0';
                        result->password_len = (uint8_t)rlen;
                        result->candidates_tried = wl_state.candidates_tested + 1;
                        result->rate = compute_rate(wl_state.candidates_tested + 1, tsc_start, tsc_now);
                        result->time_ms = 0;
                        if (ring0_state.tsc_per_ms > 0)
                            result->time_ms = (uint32_t)((tsc_now - tsc_start) / ring0_state.tsc_per_ms);
                        return JOHN_OK;
                    }

                    wl_state.candidates_tested++;

                    if (max_tsc > 0) {
                        tsc_now = ring0_rdtsc();
                        if ((tsc_now - tsc_start) >= max_tsc)
                            goto done;
                    }

                    pos += rlen + 1;
                }
            }
        }
    }

done:
    tsc_now = ring0_rdtsc();
    result->found = 0;
    result->candidates_tried = wl_state.candidates_tested;
    result->rate = compute_rate(wl_state.candidates_tested, tsc_start, tsc_now);
    result->time_ms = 0;
    if (ring0_state.tsc_per_ms > 0)
        result->time_ms = (uint32_t)((tsc_now - tsc_start) / ring0_state.tsc_per_ms);
    return JOHN_ERR_NO_MATCH;
}

void john_wordlist_reset(void) {
    wl_state.index = 0;
    wl_state.candidates_tested = 0;
    wl_state.rate_start = 0;
}
