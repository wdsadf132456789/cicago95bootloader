/**
 * Chicago-95 Bootloader — DISK-1 UUID Randomizer
 * Randomizes filesystem UUIDs at boot. Maintains a key so the original
 * UUID can be restored later. Uses ChaCha20 stream cipher as PRNG
 * and HKDF to derive per-UUID nonces from the master key.
 */

#include "boot/security.h"
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Internal state                                                      */
/* ------------------------------------------------------------------ */
#define UUID_RANDOM_MAX  64   /* max UUIDs remembered per boot */

typedef struct {
    uint8_t  original[DISK_UUID_LEN];
    uint8_t  randomized[DISK_UUID_LEN];
    uint8_t  nonce[12];
} uuid_map_t;

static struct {
    uint8_t   master_key[32];
    uuid_map_t map[UUID_RANDOM_MAX];
    uint32_t   map_count;
    uint32_t   rotated;
    uint32_t   restored;
} uuid_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Derive a 16-byte nonce from master_key + index using HKDF-like expand */
static void derive_nonce(const uint8_t key[32], uint32_t idx, uint8_t nonce[12]) {
    uint8_t info[16];
    uint32_t i;
    for (i = 0; i < 12; i++) info[i] = (uint8_t)i;
    info[12] = (uint8_t)(idx);
    info[13] = (uint8_t)(idx >> 8);
    info[14] = (uint8_t)(idx >> 16);
    info[15] = (uint8_t)(idx >> 24);

    uint8_t derived[32];
    sec_hkdf_sha256_expand(key, 32, info, 16, derived, 32);

    for (i = 0; i < 12; i++) nonce[i] = derived[i];
    sec_memzero(derived, sizeof(derived));
    sec_memzero(info, sizeof(info));
}

/* Generate a cryptographically random UUID v4 from seed+counter */
static void randomize_uuid(const uint8_t key[32], uint32_t counter,
                           uint8_t uuid_out[DISK_UUID_LEN]) {
    uint8_t keystream[DISK_UUID_LEN];
    sec_chacha20_encrypt(key, 0, (const uint8_t *)&counter, 4,
                         keystream, DISK_UUID_LEN);

    /* Set version 4 (bits 4-7 of byte 6) */
    keystream[6] = (keystream[6] & 0x0F) | 0x40;
    /* Set variant 1 (bits 6-7 of byte 8) */
    keystream[8] = (keystream[8] & 0x3F) | 0x80;

    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        uuid_out[i] = keystream[i];

    sec_memzero(keystream, sizeof(keystream));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_uuid_random_init(void) {
    sec_memzero(&uuid_state, sizeof(uuid_state));
    sec_random_bytes(uuid_state.master_key, 32);
    uuid_state.map_count = 0;
    uuid_state.rotated = 0;
    uuid_state.restored = 0;
    return 0;
}

int disk_uuid_randomize(const uint8_t uuid_in[DISK_UUID_LEN],
                        uint8_t uuid_out[DISK_UUID_LEN]) {
    if (!uuid_in || !uuid_out) return -1;
    if (uuid_state.map_count >= UUID_RANDOM_MAX) return -2;

    derive_nonce(uuid_state.master_key, uuid_state.map_count,
                 uuid_state.map[uuid_state.map_count].nonce);

    randomize_uuid(uuid_state.master_key, uuid_state.map_count, uuid_out);

    /* Save mapping */
    uint32_t idx = uuid_state.map_count;
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++) {
        uuid_state.map[idx].original[i] = uuid_in[i];
        uuid_state.map[idx].randomized[i] = uuid_out[i];
    }
    uuid_state.map_count++;

    return 0;
}

int disk_uuid_restore(const uint8_t uuid_in[DISK_UUID_LEN],
                      uint8_t uuid_out[DISK_UUID_LEN]) {
    if (!uuid_in || !uuid_out) return -1;

    for (uint32_t i = 0; i < uuid_state.map_count; i++) {
        int match = 1;
        for (uint32_t j = 0; j < DISK_UUID_LEN; j++) {
            if (uuid_state.map[i].randomized[j] != uuid_in[j]) {
                match = 0;
                break;
            }
        }
        if (match) {
            for (uint32_t j = 0; j < DISK_UUID_LEN; j++)
                uuid_out[j] = uuid_state.map[i].original[j];
            uuid_state.restored++;
            return 0;
        }
    }
    return -3; /* no mapping found */
}

int disk_uuid_random_rotate(void) {
    uint8_t new_key[32];
    sec_random_bytes(new_key, 32);

    /* Re-derive all randomized UUIDs with new key */
    for (uint32_t i = 0; i < uuid_state.map_count; i++) {
        randomize_uuid(new_key, i, uuid_state.map[i].randomized);
    }

    for (uint32_t i = 0; i < 32; i++)
        uuid_state.master_key[i] = new_key[i];

    sec_memzero(new_key, sizeof(new_key));
    uuid_state.rotated++;
    return 0;
}

void disk_uuid_random_get_key(uint8_t key[32]) {
    if (key) {
        for (uint32_t i = 0; i < 32; i++)
            key[i] = uuid_state.master_key[i];
    }
}

void disk_uuid_random_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = uuid_state.map_count;
    stats->bytes_processed = uuid_state.map_count * DISK_UUID_LEN;
    stats->connections_active = uuid_state.rotated;
    stats->drops = uuid_state.restored;
}
