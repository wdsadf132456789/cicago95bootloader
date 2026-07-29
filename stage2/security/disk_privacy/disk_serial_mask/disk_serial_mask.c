#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_serial_mask_state_t state;

static const char hex_lut[] = "0123456789abcdef";

static int hex_digit(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

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

static void compute_masked(uint8_t disk_idx, const char *real, char *masked)
{
    uint32_t len = slen(real);
    uint32_t i;
    if (len > (DP_SERIAL_LEN / 2) - 1)
        len = (DP_SERIAL_LEN / 2) - 1;
    for (i = 0; i < len; i++) {
        uint8_t x = (uint8_t)real[i] ^ state.key[(i + disk_idx) % DP_KEY_LEN];
        masked[i * 2]     = hex_lut[(x >> 4) & 0x0f];
        masked[i * 2 + 1] = hex_lut[x & 0x0f];
    }
    masked[len * 2] = '\0';
}

static void xor_from_masked(uint8_t disk_idx, const char *masked, char *real)
{
    uint32_t mlen = slen(masked);
    uint32_t rlen = mlen / 2;
    uint32_t i;
    if (rlen > DP_SERIAL_LEN - 1)
        rlen = DP_SERIAL_LEN - 1;
    for (i = 0; i < rlen; i++) {
        int hi = hex_digit(masked[i * 2]);
        int lo = hex_digit(masked[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            real[i] = '\0';
            return;
        }
        uint8_t x = (uint8_t)((hi << 4) | lo);
        real[i] = (char)(x ^ state.key[(i + disk_idx) % DP_KEY_LEN]);
    }
    real[rlen] = '\0';
}

int dp_serial_mask_init(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_serial_mask_state_t); i++)
        d[i] = 0;

    sec_random_bytes(state.key, DP_KEY_LEN);
    state.armed = 1;

    return DP_OK;
}

int dp_serial_mask_add(uint8_t disk_idx, const char *real_serial)
{
    uint32_t i, n;

    if (real_serial == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    if (state.count >= DP_MAX_DISKS)
        return DP_ERR_NO_SPACE;

    state.map[state.count].disk_idx = disk_idx;

    n = slen(real_serial);
    if (n >= DP_SERIAL_LEN) n = DP_SERIAL_LEN - 1;
    for (i = 0; i < n; i++)
        state.map[state.count].real_serial[i] = real_serial[i];
    state.map[state.count].real_serial[n] = '\0';

    compute_masked(disk_idx, real_serial, state.map[state.count].masked_serial);

    state.map[state.count].active = 1;
    state.count++;

    return DP_OK;
}

int dp_serial_mask_get_masked(uint8_t disk_idx, char *out_serial)
{
    uint32_t i, j;

    if (out_serial == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].disk_idx == disk_idx && state.map[i].active) {
            for (j = 0; state.map[i].masked_serial[j]; j++)
                out_serial[j] = state.map[i].masked_serial[j];
            out_serial[j] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_serial_mask_get_real(const char *masked_serial, char *out_real)
{
    uint32_t i;

    if (masked_serial == 0 || out_real == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].active &&
            streq(state.map[i].masked_serial, masked_serial, DP_SERIAL_LEN)) {
            xor_from_masked(state.map[i].disk_idx, masked_serial, out_real);
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_serial_mask_count(void)
{
    return (int)state.count;
}

void dp_serial_mask_reset(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_serial_mask_state_t); i++)
        d[i] = 0;
}
