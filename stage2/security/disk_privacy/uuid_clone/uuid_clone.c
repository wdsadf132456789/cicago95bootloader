#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_uuid_clone_state_t state;

int dp_uuid_clone_init(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_uuid_clone_state_t); i++)
        d[i] = 0;

    sec_random_bytes(state.key, DP_KEY_LEN);
    state.armed = 1;

    return DP_OK;
}

int dp_uuid_clone_add(uint8_t src_disk, uint8_t dst_disk,
                      const uint8_t cloned_uuid[DP_UUID_LEN])
{
    uint32_t i;

    if (cloned_uuid == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    if (state.count >= DP_MAX_DISKS)
        return DP_ERR_NO_SPACE;

    state.map[state.count].src_disk = src_disk;
    state.map[state.count].dst_disk = dst_disk;

    for (i = 0; i < DP_UUID_LEN; i++)
        state.map[state.count].cloned_uuid[i] = cloned_uuid[i];

    state.map[state.count].active = 1;
    state.count++;

    return DP_OK;
}

int dp_uuid_clone_get(uint8_t disk, uint8_t out_uuid[DP_UUID_LEN])
{
    uint32_t i, j;

    if (out_uuid == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].dst_disk == disk && state.map[i].active) {
            for (j = 0; j < DP_UUID_LEN; j++)
                out_uuid[j] = state.map[i].cloned_uuid[j];
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_uuid_clone_resolve(uint8_t disk, uint8_t *out_real_disk,
                          uint8_t out_uuid[DP_UUID_LEN])
{
    uint32_t i, j;

    if (out_real_disk == 0 || out_uuid == 0)
        return DP_ERR_BAD_PARAM;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < state.count; i++) {
        if (state.map[i].dst_disk == disk && state.map[i].active) {
            *out_real_disk = state.map[i].src_disk;
            for (j = 0; j < DP_UUID_LEN; j++)
                out_uuid[j] = state.map[i].cloned_uuid[j];
            return DP_OK;
        }
    }

    for (i = 0; i < state.count; i++) {
        if (state.map[i].src_disk == disk && state.map[i].active) {
            *out_real_disk = state.map[i].src_disk;
            for (j = 0; j < DP_UUID_LEN; j++)
                out_uuid[j] = state.map[i].cloned_uuid[j];
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_uuid_clone_count(void)
{
    return (int)state.count;
}

void dp_uuid_clone_reset(void)
{
    uint8_t *d = (uint8_t *)&state;
    uint32_t i;

    for (i = 0; i < sizeof(dp_uuid_clone_state_t); i++)
        d[i] = 0;
}
