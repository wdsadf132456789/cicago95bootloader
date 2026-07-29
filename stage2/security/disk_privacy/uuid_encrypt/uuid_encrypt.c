#include "boot/disk_privacy.h"
#include "boot/security.h"

static dp_uuid_encrypt_state_t state;

static void uuid_xor(const uint8_t a[DP_UUID_LEN], const uint8_t b[DP_UUID_LEN],
                      uint8_t out[DP_UUID_LEN]) {
    for (uint32_t i = 0; i < DP_UUID_LEN; i++)
        out[i] = a[i] ^ b[i];
}

static void uuid_encrypt_block(const uint8_t key[DP_KEY_LEN], uint8_t sector,
                               uint8_t part_idx, uint32_t block_num,
                               const uint8_t in[DP_UUID_LEN],
                               uint8_t out[DP_UUID_LEN]) {
    sec_aes256_ctx_t ctx;
    uint8_t counter[16];
    uint8_t keystream[16];

    counter[0] = sector;
    counter[1] = part_idx;
    counter[2] = (uint8_t)(block_num >> 24);
    counter[3] = (uint8_t)(block_num >> 16);
    counter[4] = (uint8_t)(block_num >> 8);
    counter[5] = (uint8_t)(block_num);
    for (uint32_t i = 6; i < 16; i++) counter[i] = 0;

    sec_aes256_init(&ctx, key);
    sec_aes256_encrypt_block(&ctx, counter, keystream);
    uuid_xor(in, keystream, out);

    for (uint32_t i = 0; i < sizeof(sec_aes256_ctx_t); i++)
        ((uint8_t *)&ctx)[i] = 0;
}

static void derive_enc_uuid(const uint8_t real_uuid[DP_UUID_LEN], uint8_t sector,
                            uint8_t part_idx, uint32_t map_idx,
                            uint8_t enc_uuid[DP_UUID_LEN]) {
    uuid_encrypt_block(state.key, sector, part_idx, map_idx,
                       real_uuid, enc_uuid);
}

static void derive_real_uuid(const uint8_t enc_uuid[DP_UUID_LEN], uint8_t sector,
                             uint8_t part_idx, uint32_t map_idx,
                             uint8_t real_uuid[DP_UUID_LEN]) {
    uuid_encrypt_block(state.key, sector, part_idx, map_idx,
                       enc_uuid, real_uuid);
}

int dp_uuid_encrypt_init(void) {
    uint8_t *d = (uint8_t *)&state;
    for (uint32_t i = 0; i < sizeof(dp_uuid_encrypt_state_t); i++) d[i] = 0;

    sec_random_bytes(state.key, DP_KEY_LEN);
    state.armed = 1;
    return DP_OK;
}

int dp_uuid_encrypt_add(const uint8_t real_uuid[DP_UUID_LEN],
                        uint8_t sector, uint8_t part_idx) {
    if (!real_uuid) return DP_ERR_BAD_PARAM;
    if (!state.armed) return DP_ERR_BAD_PARAM;
    if (state.count >= DP_MAX_MAP) return DP_ERR_NO_SPACE;

    uint32_t idx = state.count;
    for (uint32_t i = 0; i < DP_UUID_LEN; i++)
        state.map[idx].real_uuid[i] = real_uuid[i];

    state.map[idx].sector = sector;
    state.map[idx].part_idx = part_idx;

    derive_enc_uuid(real_uuid, sector, part_idx, idx,
                    state.map[idx].enc_uuid);

    state.count++;
    return DP_OK;
}

int dp_uuid_encrypt_get_enc(const uint8_t real_uuid[DP_UUID_LEN],
                            uint8_t out_enc[DP_UUID_LEN]) {
    if (!real_uuid || !out_enc) return DP_ERR_BAD_PARAM;
    if (!state.armed) return DP_ERR_BAD_PARAM;

    for (uint32_t i = 0; i < state.count; i++) {
        int match = 1;
        for (uint32_t j = 0; j < DP_UUID_LEN; j++) {
            if (state.map[i].real_uuid[j] != real_uuid[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            for (uint32_t j = 0; j < DP_UUID_LEN; j++)
                out_enc[j] = state.map[i].enc_uuid[j];
            return DP_OK;
        }
    }
    return DP_ERR_NOT_FOUND;
}

int dp_uuid_encrypt_get_real(const uint8_t enc_uuid[DP_UUID_LEN],
                             uint8_t out_real[DP_UUID_LEN]) {
    if (!enc_uuid || !out_real) return DP_ERR_BAD_PARAM;
    if (!state.armed) return DP_ERR_BAD_PARAM;

    for (uint32_t i = 0; i < state.count; i++) {
        int match = 1;
        for (uint32_t j = 0; j < DP_UUID_LEN; j++) {
            if (state.map[i].enc_uuid[j] != enc_uuid[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            for (uint32_t j = 0; j < DP_UUID_LEN; j++)
                out_real[j] = state.map[i].real_uuid[j];
            return DP_OK;
        }
    }
    return DP_ERR_NOT_FOUND;
}

int dp_uuid_encrypt_count(void) {
    return (int)state.count;
}

void dp_uuid_encrypt_reset(void) {
    uint8_t *d = (uint8_t *)&state;
    for (uint32_t i = 0; i < sizeof(dp_uuid_encrypt_state_t); i++) d[i] = 0;
}
