/**
 * Chicago-95 Bootloader — DISK-2 Volume Serial Masker
 * XOR-based serial number obfuscation with key rotation.
 * Serial numbers are 32-byte identifiers exposed by FAT32/exFAT/etc.
 */

#include "boot/security.h"
#include <stdint.h>

#define SERIAL_MASK_MAX  32

static struct {
    uint8_t   key[32];
    uint8_t   round_keys[16][32]; /* derived round keys */
    uint32_t  masked;
    uint32_t  removed;
    uint32_t  key_rotations;
} serial_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Derive 16 round keys from master key using HKDF */
static void derive_round_keys(const uint8_t master[32], uint8_t round_keys[16][32]) {
    uint8_t info[4];
    for (uint32_t r = 0; r < 16; r++) {
        info[0] = (uint8_t)r;
        info[1] = (uint8_t)(r >> 8);
        info[2] = (uint8_t)(r >> 16);
        info[3] = (uint8_t)(r >> 24);
        sec_hkdf_sha256_expand(master, 32, info, 4, round_keys[r], 32);
    }
    sec_memzero(info, sizeof(info));
}

/* XOR serial with round key material, cycling through rounds */
static void xor_serial(const uint8_t in[DISK_SERIAL_LEN], uint8_t out[DISK_SERIAL_LEN],
                       int decrypt) {
    for (uint32_t i = 0; i < DISK_SERIAL_LEN; i++) {
        uint32_t round = i % 16;
        uint32_t key_byte = i % 32;
        out[i] = in[i] ^ serial_state.round_keys[round][key_byte];
    }
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_serial_mask_init(const uint8_t key[32]) {
    if (!key) {
        sec_random_bytes(serial_state.key, 32);
    } else {
        for (uint32_t i = 0; i < 32; i++)
            serial_state.key[i] = key[i];
    }
    derive_round_keys(serial_state.key, serial_state.round_keys);
    serial_state.masked = 0;
    serial_state.removed = 0;
    serial_state.key_rotations = 0;
    return 0;
}

int disk_serial_mask_apply(const uint8_t serial_in[DISK_SERIAL_LEN],
                           uint8_t serial_out[DISK_SERIAL_LEN]) {
    if (!serial_in || !serial_out) return -1;
    xor_serial(serial_in, serial_out, 0);
    serial_state.masked++;
    return 0;
}

int disk_serial_mask_remove(const uint8_t serial_in[DISK_SERIAL_LEN],
                            uint8_t serial_out[DISK_SERIAL_LEN]) {
    if (!serial_in || !serial_out) return -1;
    xor_serial(serial_in, serial_out, 1);
    serial_state.removed++;
    return 0;
}

int disk_serial_mask_rotate(const uint8_t new_key[32]) {
    if (!new_key) return -1;
    for (uint32_t i = 0; i < 32; i++)
        serial_state.key[i] = new_key[i];
    derive_round_keys(serial_state.key, serial_state.round_keys);
    serial_state.key_rotations++;
    return 0;
}

void disk_serial_mask_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = serial_state.masked;
    stats->bytes_processed = serial_state.masked * DISK_SERIAL_LEN;
    stats->connections_active = serial_state.removed;
    stats->drops = serial_state.key_rotations;
}
