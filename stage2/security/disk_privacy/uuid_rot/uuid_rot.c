#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_uuid_rot_state_t state;

static inline uint64_t rdtsc_read(void)
{
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | (uint64_t)lo;
}

int dp_uuid_rot_init(void)
{
    uint64_t now;

    state.current_uuid[0] = 0;
    state.prev_uuid[0] = 0;
    state.last_rotate_tsc = 0;
    state.interval_tsc = 0;
    state.rotate_count = 0;
    state.armed = 0;

    sec_random_bytes(state.current_uuid, DP_UUID_LEN);

    now = rdtsc_read();
    state.last_rotate_tsc = now;
    state.interval_tsc = 10000000000ULL;
    state.armed = 1;

    return DP_OK;
}

int dp_uuid_rot_set_interval(uint64_t interval_tsc)
{
    if (interval_tsc == 0)
        return DP_ERR_BAD_PARAM;

    state.interval_tsc = interval_tsc;
    return DP_OK;
}

int dp_uuid_rot_set_uuid(const uint8_t uuid[DP_UUID_LEN])
{
    uint32_t i;

    if (uuid == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < DP_UUID_LEN; i++)
        state.current_uuid[i] = uuid[i];

    return DP_OK;
}

int dp_uuid_rot_rotate(void)
{
    uint32_t i;

    if (state.armed == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < DP_UUID_LEN; i++)
        state.prev_uuid[i] = state.current_uuid[i];

    sec_random_bytes(state.current_uuid, DP_UUID_LEN);

    state.rotate_count++;
    state.last_rotate_tsc = rdtsc_read();

    return DP_OK;
}

int dp_uuid_rot_get_current(uint8_t out_uuid[DP_UUID_LEN])
{
    uint32_t i;

    if (out_uuid == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < DP_UUID_LEN; i++)
        out_uuid[i] = state.current_uuid[i];

    return DP_OK;
}

int dp_uuid_rot_get_prev(uint8_t out_uuid[DP_UUID_LEN])
{
    uint32_t i;

    if (out_uuid == 0)
        return DP_ERR_BAD_PARAM;

    for (i = 0; i < DP_UUID_LEN; i++)
        out_uuid[i] = state.prev_uuid[i];

    return DP_OK;
}

int dp_uuid_rot_get_count(void)
{
    return (int)state.rotate_count;
}
