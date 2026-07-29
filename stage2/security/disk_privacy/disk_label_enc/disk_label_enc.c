/**
 * Chicago-95 Bootloader - Volume Label Encrypter
 * XOR-based volume label obfuscation for disk privacy
 */

#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_label_enc_state_t state;

static void label_enc_xor(const uint8_t *key, uint32_t key_len,
                           uint8_t disk_idx, uint8_t part_idx,
                           const char *in, char *out, uint32_t len) {
    uint8_t mod = disk_idx + part_idx;
    for (uint32_t i = 0; i < len; i++) {
        uint8_t kb = key[(i + mod) % key_len];
        out[i] = in[i] ^ (kb ^ mod);
    }
}

int dp_label_enc_init(void) {
    for (uint32_t i = 0; i < sizeof(dp_label_enc_state_t); i++)
        ((uint8_t *)&state)[i] = 0;

    sec_random_bytes(state.key, DP_KEY_LEN);
    state.armed = 1;

    return DP_OK;
}

int dp_label_enc_add(uint8_t disk_idx, uint8_t part_idx,
                     const char *real_label) {
    if (!real_label || !state.armed)
        return DP_ERR_BAD_PARAM;
    if (state.count >= DP_MAX_PARTITIONS)
        return DP_ERR_NO_SPACE;

    uint32_t idx = state.count;
    state.map[idx].disk_idx = disk_idx;
    state.map[idx].part_idx = part_idx;

    uint32_t len = 0;
    while (len < DP_LABEL_LEN - 1 && real_label[len])
        len++;

    for (uint32_t i = 0; i < len; i++)
        state.map[idx].real_label[i] = real_label[i];
    state.map[idx].real_label[len] = '\0';

    label_enc_xor(state.key, DP_KEY_LEN, disk_idx, part_idx,
                  state.map[idx].real_label,
                  state.map[idx].enc_label, len);
    state.map[idx].enc_label[len] = '\0';

    state.map[idx].active = 1;
    state.count++;

    return DP_OK;
}

int dp_label_enc_get_enc(uint8_t disk_idx, uint8_t part_idx,
                         char *out_label) {
    if (!out_label || !state.armed)
        return DP_ERR_BAD_PARAM;

    for (uint32_t i = 0; i < state.count; i++) {
        if (state.map[i].active &&
            state.map[i].disk_idx == disk_idx &&
            state.map[i].part_idx == part_idx) {
            uint32_t len = 0;
            while (len < DP_LABEL_LEN - 1 && state.map[i].enc_label[len])
                len++;
            for (uint32_t j = 0; j <= len; j++)
                out_label[j] = state.map[i].enc_label[j];
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_label_enc_get_real(const char *enc_label, char *out_real) {
    if (!enc_label || !out_real || !state.armed)
        return DP_ERR_BAD_PARAM;

    uint32_t enc_len = 0;
    while (enc_len < DP_LABEL_LEN - 1 && enc_label[enc_len])
        enc_len++;

    for (uint32_t i = 0; i < state.count; i++) {
        if (!state.map[i].active)
            continue;

        uint32_t map_len = 0;
        while (map_len < DP_LABEL_LEN - 1 && state.map[i].enc_label[map_len])
            map_len++;

        if (map_len != enc_len)
            continue;

        int match = 1;
        for (uint32_t j = 0; j < enc_len; j++) {
            if (state.map[i].enc_label[j] != enc_label[j]) {
                match = 0;
                break;
            }
        }

        if (match) {
            for (uint32_t j = 0; j <= enc_len; j++)
                out_real[j] = state.map[i].real_label[j];
            return DP_OK;
        }
    }

    return DP_ERR_NOT_FOUND;
}

int dp_label_enc_count(void) {
    return (int)state.count;
}

void dp_label_enc_reset(void) {
    for (uint32_t i = 0; i < sizeof(dp_label_enc_state_t); i++)
        ((uint8_t *)&state)[i] = 0;
}
