/**
 * Chicago-95 Bootloader — DISK-6 Partition Name Encrypter
 * Encrypts/obfuscates partition name strings stored in GPT entries.
 * Each GPT entry has a 72-byte UTF-16LE name at offset 56.
 */

#include "boot/security.h"
#include <stdint.h>

static struct {
    uint8_t  key[32];
    uint8_t  nonce[12];
    uint32_t encrypted;
    uint32_t decrypted;
    uint32_t rotations;
} partname_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* XOR-encrypt a byte stream with rotating key material */
static void xor_name(const uint8_t *in, uint8_t *out, uint32_t len,
                     const uint8_t key[32], const uint8_t nonce[12]) {
    /* Generate keystream by hashing key+nonce+counter with SHA-256 */
    uint8_t block[64];
    uint8_t hash[32];
    uint32_t pos = 0;

    while (pos < len) {
        /* Build input: key || nonce || block_counter */
        uint32_t block_num = pos / 32;
        for (uint32_t i = 0; i < 32; i++) block[i] = key[i];
        for (uint32_t i = 0; i < 12; i++) block[32 + i] = nonce[i];
        block[44] = (uint8_t)block_num;
        block[45] = (uint8_t)(block_num >> 8);
        block[46] = (uint8_t)(block_num >> 16);
        block[47] = (uint8_t)(block_num >> 24);

        sec_sha256(block, 48, hash);

        for (uint32_t i = 0; i < 32 && pos < len; i++, pos++)
            out[pos] = in[pos] ^ hash[i];
    }

    sec_memzero(block, sizeof(block));
    sec_memzero(hash, sizeof(hash));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_partname_init(void) {
    sec_random_bytes(partname_state.key, 32);
    sec_random_bytes(partname_state.nonce, 12);
    partname_state.encrypted = 0;
    partname_state.decrypted = 0;
    partname_state.rotations = 0;
    return 0;
}

int disk_partname_encrypt(const uint8_t *name_in, uint32_t name_len,
                          uint8_t *name_out) {
    if (!name_in || !name_out || name_len == 0) return -1;
    if (name_len > 72) name_len = 72; /* GPT name max */
    xor_name(name_in, name_out, name_len, partname_state.key, partname_state.nonce);
    partname_state.encrypted++;
    return 0;
}

int disk_partname_decrypt(const uint8_t *name_in, uint32_t name_len,
                          uint8_t *name_out) {
    if (!name_in || !name_out || name_len == 0) return -1;
    if (name_len > 72) name_len = 72;
    xor_name(name_in, name_out, name_len, partname_state.key, partname_state.nonce);
    partname_state.decrypted++;
    return 0;
}

int disk_partname_rotate(void) {
    sec_random_bytes(partname_state.key, 32);
    sec_random_bytes(partname_state.nonce, 12);
    partname_state.rotations++;
    return 0;
}

void disk_partname_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = partname_state.encrypted;
    stats->connections_active = partname_state.decrypted;
    stats->drops = partname_state.rotations;
}
