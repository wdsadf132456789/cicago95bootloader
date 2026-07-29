#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_model_spoof_state_t state;

static uint32_t slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int streq(const char *a, const char *b, uint32_t max)
{
    uint32_t i;
    for (i = 0; i < max; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static void strcopy(char *dst, const char *src, uint32_t max)
{
    uint32_t i;
    for (i = 0; i < max && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

int dp_model_spoof_init(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_model_spoof_state_t); i++)
        d[i] = 0;

    sec_random_bytes(state.key, DP_KEY_LEN);
    state.armed = 1;

    return DP_OK;
}

int dp_model_spoof_add(uint8_t disk_idx, const char *real_model)
{
    uint32_t i, n;

    if (real_model == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    if (state.count >= DP_MAX_DISKS)
        return DP_ERR_NO_SPACE;

    state.map[state.count].disk_idx = disk_idx;

    n = slen(real_model);
    if (n >= DP_MODEL_LEN) n = DP_MODEL_LEN - 1;
    for (i = 0; i < n; i++)
        state.map[state.count].real_model[i] = real_model[i];
    state.map[state.count].real_model[n] = '\0';

    state.map[state.count].active = 1;
    state.count++;

    return DP_OK;
}

int dp_model_spoof_spoof(uint8_t disk_idx, const char *spoofed_model)
{
    uint32_t i, j, n;

    if (spoofed_model == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].disk_idx == disk_idx && state.map[i].active) {
            n = slen(spoofed_model);
            if (n >= DP_MODEL_LEN) n = DP_MODEL_LEN - 1;
            for (j = 0; j < n; j++)
                state.map[i].spoofed_model[j] = spoofed_model[j];
            state.map[i].spoofed_model[n] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_model_spoof_get(uint8_t disk_idx, char *out_model)
{
    uint32_t i, j;

    if (out_model == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].disk_idx == disk_idx && state.map[i].active) {
            for (j = 0; state.map[i].spoofed_model[j]; j++)
                out_model[j] = state.map[i].spoofed_model[j];
            out_model[j] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_model_spoof_get_real(const char *spoofed, char *out_real)
{
    uint32_t i, j;

    if (spoofed == 0 || out_real == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].active &&
            streq(state.map[i].spoofed_model, spoofed, DP_MODEL_LEN)) {
            for (j = 0; state.map[i].real_model[j]; j++)
                out_real[j] = state.map[i].real_model[j];
            out_real[j] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_model_spoof_count(void)
{
    return (int)state.count;
}

void dp_model_spoof_reset(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_model_spoof_state_t); i++)
        d[i] = 0;
}
