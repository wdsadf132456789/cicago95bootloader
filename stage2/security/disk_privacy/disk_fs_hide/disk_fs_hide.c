#include "boot/disk_privacy.h"
#include "boot/security.h"

#define FS_SPOOF_TABLE_LEN 8

static const char *spoof_table[FS_SPOOF_TABLE_LEN] = {
    "fat32",
    "ntfs",
    "fat16",
    "linux-swap",
    "ext4",
    "xfs",
    "btrfs",
    "vfat"
};

static dp_fs_hide_state_t state;

static uint32_t fs_slen(const char *s)
{
    uint32_t n = 0;
    while (s[n]) n++;
    return n;
}

static int fs_streq(const char *a, const char *b, uint32_t max)
{
    uint32_t i;
    for (i = 0; i < max; i++) {
        if (a[i] != b[i]) return 0;
        if (a[i] == '\0') return 1;
    }
    return 1;
}

static void fs_strcopy(char *dst, const char *src, uint32_t max)
{
    uint32_t i;
    for (i = 0; i < max && src[i]; i++)
        dst[i] = src[i];
    dst[i] = '\0';
}

static const char *fs_spoof_lookup(const char *real_fstype)
{
    uint32_t i;
    for (i = 0; i < FS_SPOOF_TABLE_LEN; i++) {
        if (fs_streq(spoof_table[i], real_fstype, 32))
            return spoof_table[(i + 1) % FS_SPOOF_TABLE_LEN];
    }
    return (const char *)0;
}

int dp_fs_hide_init(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_fs_hide_state_t); i++)
        d[i] = 0;

    sec_random_bytes(state.key, DP_KEY_LEN);
    state.armed = 1;

    return DP_OK;
}

int dp_fs_hide_add(uint8_t disk_idx, uint8_t part_idx,
                    const char *real_fstype)
{
    uint32_t i, n;
    const char *hidden;

    if (real_fstype == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    if (state.count >= DP_MAX_PARTITIONS)
        return DP_ERR_NO_SPACE;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].active &&
            state.map[i].disk_idx == disk_idx &&
            state.map[i].part_idx == part_idx) {
            return DP_OK;
        }
    }

    hidden = fs_spoof_lookup(real_fstype);
    if (hidden == 0)
        return DP_ERR_BAD_PARAM;

    state.map[state.count].disk_idx = disk_idx;
    state.map[state.count].part_idx = part_idx;

    n = fs_slen(real_fstype);
    if (n >= 32) n = 31;
    for (i = 0; i < n; i++)
        state.map[state.count].real_fstype[i] = real_fstype[i];
    state.map[state.count].real_fstype[n] = '\0';

    n = fs_slen(hidden);
    if (n >= 32) n = 31;
    for (i = 0; i < n; i++)
        state.map[state.count].hidden_fstype[i] = hidden[i];
    state.map[state.count].hidden_fstype[n] = '\0';

    state.map[state.count].active = 1;
    state.count++;

    return DP_OK;
}

int dp_fs_hide_spoof(uint8_t disk_idx, uint8_t part_idx,
                      const char *hidden_fstype)
{
    uint32_t i, n;

    if (hidden_fstype == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].disk_idx == disk_idx &&
            state.map[i].part_idx == part_idx &&
            state.map[i].active) {
            n = fs_slen(hidden_fstype);
            if (n >= 32) n = 31;
            for (uint32_t j = 0; j < n; j++)
                state.map[i].hidden_fstype[j] = hidden_fstype[j];
            state.map[i].hidden_fstype[n] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_fs_hide_get(uint8_t disk_idx, uint8_t part_idx,
                    char *out_fstype)
{
    uint32_t i, j;

    if (out_fstype == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].disk_idx == disk_idx &&
            state.map[i].part_idx == part_idx &&
            state.map[i].active) {
            for (j = 0; state.map[i].hidden_fstype[j]; j++)
                out_fstype[j] = state.map[i].hidden_fstype[j];
            out_fstype[j] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_fs_hide_get_real(const char *hidden_fstype, char *out_real)
{
    uint32_t i, j;

    if (hidden_fstype == 0 || out_real == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].active &&
            fs_streq(state.map[i].hidden_fstype, hidden_fstype, 32)) {
            for (j = 0; state.map[i].real_fstype[j]; j++)
                out_real[j] = state.map[i].real_fstype[j];
            out_real[j] = '\0';
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_fs_hide_count(void)
{
    return (int)state.count;
}

void dp_fs_hide_reset(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_fs_hide_state_t); i++)
        d[i] = 0;
}
