/**
 * Chicago-95 Bootloader — DISK-10 Disk Fingerprint Rotator
 * Maintains a persistent cross-boot fingerprint that rotates each boot.
 * The fingerprint is derived from a master key + boot counter using
 * HKDF, making it deterministic but unpredictable without the key.
 * Each boot produces a different fingerprint, so lsblk/blkid cannot
 * correlate the same physical disk across reboots.
 */

#include "boot/security.h"
#include <stdint.h>

static struct {
    uint8_t   master_key[32];     /* stable across boots (from EEPROM/CMOS) */
    uint64_t  boot_counter;       /* incremented each boot */
    uint8_t   current_fp[DISK_UUID_LEN]; /* current boot fingerprint */
    uint8_t   prev_fp[DISK_UUID_LEN];    /* previous boot fingerprint */
    uint32_t  rotations;
    uint32_t  applied;
    uint32_t  reverted;
} fp_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Derive fingerprint from master_key + boot_counter using HKDF-SHA256 */
static void derive_fingerprint(const uint8_t key[32], uint64_t counter,
                               uint8_t fp_out[DISK_UUID_LEN]) {
    uint8_t salt[8];
    uint8_t info[16];
    uint8_t derived[32];

    /* Salt: counter as big-endian bytes */
    for (uint32_t i = 0; i < 8; i++)
        salt[i] = (uint8_t)(counter >> (56 - i * 8));

    /* Info: "CHICAGO95-DISK-FP" (partially) */
    info[0] = 'C'; info[1] = 'H'; info[2] = 'I'; info[3] = 'C';
    info[4] = 'A'; info[5] = 'G'; info[6] = 'O'; info[7] = '9';
    info[8] = '5'; info[9] = '-'; info[10] = 'D'; info[11] = 'I';
    info[12] = 'S'; info[13] = 'K'; info[14] = '-'; info[15] = 'F';

    sec_hkdf_sha256_expand(key, 32, info, 16, derived, 32);

    /* Set version 4 UUID in first 16 bytes of derived */
    uint8_t uuid[DISK_UUID_LEN];
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        uuid[i] = derived[i];
    uuid[6] = (uuid[6] & 0x0F) | 0x40;  /* version 4 */
    uuid[8] = (uuid[8] & 0x3F) | 0x80;  /* variant 1 */

    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        fp_out[i] = uuid[i];

    sec_memzero(derived, sizeof(derived));
    sec_memzero(salt, sizeof(salt));
    sec_memzero(info, sizeof(info));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_fingerprint_init(uint64_t boot_counter) {
    /* Try to load master key from CMOS; if unavailable, generate new */
    sec_random_bytes(fp_state.master_key, 32);
    fp_state.boot_counter = boot_counter;

    /* Save previous fingerprint */
    if (boot_counter > 0) {
        derive_fingerprint(fp_state.master_key, boot_counter - 1,
                           fp_state.prev_fp);
    }

    /* Derive current fingerprint */
    derive_fingerprint(fp_state.master_key, boot_counter, fp_state.current_fp);

    fp_state.rotations = 0;
    fp_state.applied = 0;
    fp_state.reverted = 0;
    return 0;
}

int disk_fingerprint_rotate(void) {
    fp_state.boot_counter++;

    /* Shift: current becomes prev */
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        fp_state.prev_fp[i] = fp_state.current_fp[i];

    /* Derive new */
    derive_fingerprint(fp_state.master_key, fp_state.boot_counter,
                       fp_state.current_fp);

    fp_state.rotations++;
    return 0;
}

int disk_fingerprint_get(uint8_t fingerprint[DISK_UUID_LEN]) {
    if (!fingerprint) return -1;
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        fingerprint[i] = fp_state.current_fp[i];
    return 0;
}

int disk_fingerprint_apply_uuid(const uint8_t real_uuid[DISK_UUID_LEN],
                                uint8_t fake_uuid[DISK_UUID_LEN]) {
    if (!real_uuid || !fake_uuid) return -1;

    /* XOR real_uuid with current fingerprint to produce fake */
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        fake_uuid[i] = real_uuid[i] ^ fp_state.current_fp[i];

    /* Set version 4 in output */
    fake_uuid[6] = (fake_uuid[6] & 0x0F) | 0x40;
    fake_uuid[8] = (fake_uuid[8] & 0x3F) | 0x80;

    fp_state.applied++;
    return 0;
}

int disk_fingerprint_revert_uuid(const uint8_t fake_uuid[DISK_UUID_LEN],
                                 uint8_t real_uuid[DISK_UUID_LEN]) {
    if (!fake_uuid || !real_uuid) return -1;

    /* XOR fake_uuid with current fingerprint to recover real */
    for (uint32_t i = 0; i < DISK_UUID_LEN; i++)
        real_uuid[i] = fake_uuid[i] ^ fp_state.current_fp[i];

    fp_state.reverted++;
    return 0;
}

uint64_t disk_fingerprint_get_boot_count(void) {
    return fp_state.boot_counter;
}

void disk_fingerprint_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = fp_state.applied;
    stats->connections_active = fp_state.reverted;
    stats->drops = fp_state.rotations;
    /* Encode boot counter in bytes_processed */
    stats->bytes_processed = (uint32_t)fp_state.boot_counter;
}
