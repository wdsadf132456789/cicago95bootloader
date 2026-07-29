/**
 * Chicago-95 Bootloader — DISK-7 Filesystem Label Encrypter
 * Encrypts/rotates filesystem volume labels. FAT12/16/32 store labels
 * at the root directory or BPB offset 43; ext4 stores them in the
 * superblock at offset 120. This module scrambles the label bytes
 * so blkid shows garbage.
 */

#include "boot/security.h"
#include <stdint.h>

#define LABEL_MAX_LEN  64

static struct {
    uint8_t   key[32];
    uint8_t   nonce[12];
    uint32_t  encrypted;
    uint32_t  decrypted;
    uint32_t  rotations;
} label_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* ChaCha20 stream-based XOR encryption for labels */
static void encrypt_label_bytes(const uint8_t *in, uint8_t *out, uint32_t len) {
    uint8_t keystream[64];
    sec_chacha20_encrypt(label_state.key, 0x4C424C01,  /* "LBL1" */
                         label_state.nonce, 12,
                         keystream, len > 64 ? 64 : len);
    for (uint32_t i = 0; i < len && i < LABEL_MAX_LEN; i++)
        out[i] = in[i] ^ keystream[i];
    sec_memzero(keystream, sizeof(keystream));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_label_init(void) {
    sec_random_bytes(label_state.key, 32);
    sec_random_bytes(label_state.nonce, 12);
    label_state.encrypted = 0;
    label_state.decrypted = 0;
    label_state.rotations = 0;
    return 0;
}

int disk_label_encrypt(const uint8_t *label_in, uint32_t label_len,
                       uint8_t *label_out) {
    if (!label_in || !label_out || label_len == 0) return -1;
    if (label_len > LABEL_MAX_LEN) label_len = LABEL_MAX_LEN;
    encrypt_label_bytes(label_in, label_out, label_len);
    label_state.encrypted++;
    return 0;
}

int disk_label_decrypt(const uint8_t *label_in, uint32_t label_len,
                       uint8_t *label_out) {
    if (!label_in || !label_out || label_len == 0) return -1;
    if (label_len > LABEL_MAX_LEN) label_len = LABEL_MAX_LEN;
    encrypt_label_bytes(label_in, label_out, label_len);
    label_state.decrypted++;
    return 0;
}

int disk_label_rotate(void) {
    sec_random_bytes(label_state.key, 32);
    sec_random_bytes(label_state.nonce, 12);
    label_state.rotations++;
    return 0;
}

void disk_label_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = label_state.encrypted;
    stats->connections_active = label_state.decrypted;
    stats->drops = label_state.rotations;
}
