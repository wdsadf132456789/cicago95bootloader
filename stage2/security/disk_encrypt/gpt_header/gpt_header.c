/**
 * Chicago-95 Bootloader — DISK-3 GPT Header Encrypter
 * Encrypts/obfuscates GPT disk GUIDs and partition GUIDs.
 * Operates on in-memory GPT structures; the encrypted GUIDs are
 * what tools like lsblk/blkid would read.
 */

#include "boot/security.h"
#include <stdint.h>

static struct {
    uint8_t  key[32];
    uint8_t  nonce[12];
    uint64_t encrypted_count;
    uint64_t decrypted_count;
} gpt_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Encrypt a GUID using ChaCha20-Poly1305 (only the 16-byte GUID, no AAD) */
static void encrypt_guid(const uint8_t key[32], const uint8_t nonce[12],
                         const uint8_t guid_in[DISK_UUID_LEN],
                         uint8_t guid_out[DISK_UUID_LEN]) {
    uint8_t tag[16];
    sec_chacha20_poly1305_flat_encrypt(
        key, nonce,
        NULL, 0,
        guid_in, DISK_UUID_LEN,
        guid_out,
        tag
    );
    /* Append first 16 bytes of tag to make it GUID-compatible */
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        guid_out[i] ^= tag[i];
    sec_memzero(tag, sizeof(tag));
}

static void decrypt_guid(const uint8_t key[32], const uint8_t nonce[12],
                         const uint8_t guid_in[DISK_UUID_LEN],
                         uint8_t guid_out[DISK_UUID_LEN]) {
    uint8_t tag[16];
    uint8_t temp[DISK_UUID_LEN];
    /* Reverse the XOR tag step */
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        temp[i] = guid_in[i] ^ guid_out[i];
    sec_chacha20_poly1305_flat_decrypt(
        key, nonce,
        NULL, 0,
        guid_in, DISK_UUID_LEN,
        guid_out,
        tag
    );
    sec_memzero(tag, sizeof(tag));
    sec_memzero(temp, sizeof(temp));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_gpt_encrypt_init(void) {
    sec_random_bytes(gpt_state.key, 32);
    sec_random_bytes(gpt_state.nonce, 12);
    gpt_state.encrypted_count = 0;
    gpt_state.decrypted_count = 0;
    return 0;
}

int disk_gpt_encrypt_disk_guid(const uint8_t guid_in[DISK_UUID_LEN],
                               uint8_t guid_out[DISK_UUID_LEN]) {
    if (!guid_in || !guid_out) return -1;
    encrypt_guid(gpt_state.key, gpt_state.nonce, guid_in, guid_out);
    gpt_state.encrypted_count++;
    return 0;
}

int disk_gpt_encrypt_part_guid(const uint8_t guid_in[DISK_UUID_LEN],
                               uint8_t guid_out[DISK_UUID_LEN]) {
    if (!guid_in || !guid_out) return -1;
    /* Derive per-partition nonce using part_num embedded in nonce */
    uint8_t part_nonce[12];
    for (uint32_t i = 0; i < 12; i++)
        part_nonce[i] = gpt_state.nonce[i] ^ (uint8_t)(gpt_state.encrypted_count >> (i % 8));
    encrypt_guid(gpt_state.key, part_nonce, guid_in, guid_out);
    sec_memzero(part_nonce, sizeof(part_nonce));
    gpt_state.encrypted_count++;
    return 0;
}

int disk_gpt_encrypt_entry(const disk_gpt_entry_t *in, disk_gpt_entry_t *out) {
    if (!in || !out) return -1;
    disk_gpt_encrypt_disk_guid(in->disk_guid, out->disk_guid);
    disk_gpt_encrypt_part_guid(in->part_guid, out->part_guid);
    for (uint32_t i = 0; i < 8; i++) {
        out->first_lba[i] = in->first_lba[i] ^ gpt_state.key[i];
        out->last_lba[i] = in->last_lba[i] ^ gpt_state.key[i + 8];
    }
    out->part_num = in->part_num;
    return 0;
}

int disk_gpt_decrypt_entry(const disk_gpt_entry_t *in, disk_gpt_entry_t *out) {
    if (!in || !out) return -1;
    /* Reverse disk/partition GUID */
    uint8_t temp_guid[DISK_UUID_LEN];
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        temp_guid[i] = in->disk_guid[i];
    decrypt_guid(gpt_state.key, gpt_state.nonce, temp_guid, out->disk_guid);
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        temp_guid[i] = in->part_guid[i];
    decrypt_guid(gpt_state.key, gpt_state.nonce, temp_guid, out->part_guid);
    /* Reverse LBA obfuscation */
    for (uint32_t i = 0; i < 8; i++) {
        out->first_lba[i] = in->first_lba[i] ^ gpt_state.key[i];
        out->last_lba[i] = in->last_lba[i] ^ gpt_state.key[i + 8];
    }
    out->part_num = in->part_num;
    gpt_state.decrypted_count++;
    sec_memzero(temp_guid, sizeof(temp_guid));
    return 0;
}

void disk_gpt_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = (uint32_t)gpt_state.encrypted_count;
    stats->bytes_processed = (uint32_t)(gpt_state.encrypted_count * DISK_UUID_LEN);
    stats->connections_active = (uint32_t)gpt_state.decrypted_count;
}
