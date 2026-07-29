/**
 * Chicago-95 Bootloader — DISK-4 MBR Disk ID Scrambler
 * Scrambles MBR disk signature (offset 440) and partition table entries.
 * Tools like lsblk read the disk ID from this location.
 */

#include "boot/security.h"
#include <stdint.h>

static struct {
    uint8_t  key[32];
    uint32_t scrambled;
    uint32_t unscrambled;
} mbr_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Scramble a 32-bit disk ID using Feistel network with 8 rounds */
static uint32_t feistel_scramble(uint32_t val, const uint8_t key[32]) {
    uint16_t left  = (uint16_t)(val >> 16);
    uint16_t right = (uint16_t)(val);

    for (uint32_t r = 0; r < 8; r++) {
        uint16_t f_out = right;
        /* Feistel function: right ^ key_material ^ round_counter */
        f_out ^= (uint16_t)(key[r * 2] << 8 | key[r * 2 + 1]);
        f_out ^= (uint16_t)(r * 0x9E37);
        uint16_t new_right = left ^ f_out;
        left = right;
        right = new_right;
    }

    return ((uint32_t)left << 16) | right;
}

static uint32_t feistel_unscramble(uint32_t val, const uint8_t key[32]) {
    uint16_t left  = (uint16_t)(val >> 16);
    uint16_t right = (uint16_t)(val);

    for (int32_t r = 7; r >= 0; r--) {
        uint16_t f_out = left;
        f_out ^= (uint16_t)(key[r * 2] << 8 | key[r * 2 + 1]);
        f_out ^= (uint16_t)(r * 0x9E37);
        uint16_t new_left = right ^ f_out;
        right = left;
        left = new_left;
    }

    return ((uint32_t)left << 16) | right;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_mbr_scramble_init(void) {
    sec_random_bytes(mbr_state.key, 32);
    mbr_state.scrambled = 0;
    mbr_state.unscrambled = 0;
    return 0;
}

int disk_mbr_scramble(uint32_t disk_id_in, uint32_t *disk_id_out) {
    if (!disk_id_out) return -1;
    *disk_id_out = feistel_scramble(disk_id_in, mbr_state.key);
    mbr_state.scrambled++;
    return 0;
}

int disk_mbr_unscramble(uint32_t disk_id_in, uint32_t *disk_id_out) {
    if (!disk_id_out) return -1;
    *disk_id_out = feistel_unscramble(disk_id_in, mbr_state.key);
    mbr_state.unscrambled++;
    return 0;
}

int disk_mbr_scramble_part_entry(uint8_t entry_in[16], uint8_t entry_out[16]) {
    if (!entry_in || !entry_out) return -1;

    /* Scramble LBA start (bytes 8-11) and LBA end (bytes 12-15) */
    uint32_t lba_start, lba_end;
    lba_start = entry_in[8] | (entry_in[9] << 8) |
                (entry_in[10] << 16) | (entry_in[11] << 24);
    lba_end   = entry_in[12] | (entry_in[13] << 8) |
                (entry_in[14] << 16) | (entry_in[15] << 24);

    uint32_t scrambled_start = feistel_scramble(lba_start, mbr_state.key);
    uint32_t scrambled_end   = feistel_scramble(lba_end, mbr_state.key);

    for (uint32_t i = 0; i < 16; i++) entry_out[i] = entry_in[i];

    entry_out[8]  = (uint8_t)(scrambled_start);
    entry_out[9]  = (uint8_t)(scrambled_start >> 8);
    entry_out[10] = (uint8_t)(scrambled_start >> 16);
    entry_out[11] = (uint8_t)(scrambled_start >> 24);
    entry_out[12] = (uint8_t)(scrambled_end);
    entry_out[13] = (uint8_t)(scrambled_end >> 8);
    entry_out[14] = (uint8_t)(scrambled_end >> 16);
    entry_out[15] = (uint8_t)(scrambled_end >> 24);

    return 0;
}

int disk_mbr_unscramble_part_entry(uint8_t entry_in[16], uint8_t entry_out[16]) {
    if (!entry_in || !entry_out) return -1;

    uint32_t lba_start, lba_end;
    lba_start = entry_in[8] | (entry_in[9] << 8) |
                (entry_in[10] << 16) | (entry_in[11] << 24);
    lba_end   = entry_in[12] | (entry_in[13] << 8) |
                (entry_in[14] << 16) | (entry_in[15] << 24);

    uint32_t real_start = feistel_unscramble(lba_start, mbr_state.key);
    uint32_t real_end   = feistel_unscramble(lba_end, mbr_state.key);

    for (uint32_t i = 0; i < 16; i++) entry_out[i] = entry_in[i];

    entry_out[8]  = (uint8_t)(real_start);
    entry_out[9]  = (uint8_t)(real_start >> 8);
    entry_out[10] = (uint8_t)(real_start >> 16);
    entry_out[11] = (uint8_t)(real_start >> 24);
    entry_out[12] = (uint8_t)(real_end);
    entry_out[13] = (uint8_t)(real_end >> 8);
    entry_out[14] = (uint8_t)(real_end >> 16);
    entry_out[15] = (uint8_t)(real_end >> 24);

    return 0;
}

void disk_mbr_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = mbr_state.scrambled;
    stats->connections_active = mbr_state.unscrambled;
}
