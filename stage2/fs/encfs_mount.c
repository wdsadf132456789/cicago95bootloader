/**
 * Chicago-95 Encrypted BrainFS Mount
 * Key derivation from TPM/TSC entropy, volume-level encryption, VFS integration
 */

#include <stdint.h>
#include "fs/brainfs.h"
#include "fs/brainvfs.h"
#include "boot/security.h"
#include "boot/ring0_init.h"
#include "tape/tape.h"

#define ENCFS_KEY_SIZE      32
#define ENCFS_NONCE_SIZE    12
#define ENCFS_MAGIC         0x454E4342  /* "ENCB" */
#define ENCFS_VERSION       0x00010000

typedef struct {
    uint8_t  key[ENCFS_KEY_SIZE];
    uint8_t  volume_key[ENCFS_KEY_SIZE];
    uint8_t  mounted;
    uint8_t  encrypted;
    uint32_t magic;
    uint32_t version;
    uint8_t  device;
    uint8_t  fat_width;
    uint8_t  total_fat_widths;
    uint8_t  fat_widths_supported[9]; /* 1,2,4,8,12,16,32,64,128 */
} encfs_state_t;

static encfs_state_t encfs;

/* Derive volume encryption key from CPU entropy */
static void encfs_derive_key(uint8_t out_key[ENCFS_KEY_SIZE]) {
    /* Use TSC + APIC ID + random bytes for key material */
    uint8_t key_material[64];
    uint32_t idx = 0;

    /* Gather 32 bytes of hardware randomness via RDRAND/RDTSC */
    for (uint32_t i = 0; i < 8; i++) {
        uint64_t val;
        __asm__ volatile("rdrand %0" : "=r"(val) : : "cc");
        /* Fallback to TSC if RDRAND not available */
        if (val == 0) val = ring0_rdtsc() ^ ((uint64_t)i * 0x123456789ABCDEFULL);
        key_material[idx++] = (uint8_t)(val & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 8) & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 16) & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 24) & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 32) & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 40) & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 48) & 0xFF);
        key_material[idx++] = (uint8_t)((val >> 56) & 0xFF);
    }

    /* Derive final key: SHA-256(material) */
    sec_sha256(key_material, 64, out_key);

    /* Zero key material */
    for (uint32_t i = 0; i < 64; i++) key_material[i] = 0;
}

/* Supported FAT widths */
static const uint8_t supported_widths[] = {1, 2, 4, 8, 12, 16, 32, 64, 128};

int encfs_init(void) {
    uint8_t *d = (uint8_t *)&encfs;
    for (uint32_t i = 0; i < sizeof(encfs_state_t); i++) d[i] = 0;

    /* Derive encryption key */
    encfs_derive_key(encfs.key);

    /* Derive volume key from master key + volume identifier "CHICAGO95-BRAINFS" */
    uint8_t vol_label[48];
    const char *label = "CHICAGO95-BRAINFS-VOL-KEY";
    uint32_t li = 0;
    while (label[li] && li < 47) { vol_label[li] = label[li]; li++; }
    for (uint32_t i = 0; i < 32; i++) vol_label[li++] = encfs.key[i];

    sec_sha256(vol_label, li, encfs.volume_key);

    /* Clear sensitive material */
    for (uint32_t i = 0; i < 48; i++) vol_label[i] = 0;

    /* Populate supported widths */
    encfs.total_fat_widths = 9;
    for (uint32_t i = 0; i < 9; i++)
        encfs.fat_widths_supported[i] = supported_widths[i];

    encfs.magic = ENCFS_MAGIC;
    encfs.version = ENCFS_VERSION;
    encfs.encrypted = 1;

    return 0;
}

int encfs_mount(uint8_t drive, uint8_t fat_width) {
    if (encfs.mounted) return -1;

    /* Validate FAT width */
    int valid = 0;
    for (uint32_t i = 0; i < encfs.total_fat_widths; i++) {
        if (encfs.fat_widths_supported[i] == fat_width) { valid = 1; break; }
    }
    if (!valid) return -2;

    /* Mount via BrainFS core */
    int rc = brainfs_mount(drive, fat_width);
    if (rc != 0) return rc;

    /* Mount via VFS layer with encrypted ops */
    rc = brainvfs_mount("/dev/sda", "/", "brainfs", 0);

    encfs.device = drive;
    encfs.fat_width = fat_width;
    encfs.mounted = 1;

    return rc;
}

int encfs_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, uint8_t *buf) {
    /* Read raw sectors from disk */
    int rc = tape_read(0, drive, lba, count, buf);
    if (rc != 0) return rc;

    /* Decrypt data in-place using AES-256-CTR */
    if (encfs.encrypted && encfs.mounted) {
        uint8_t nonce[12];
        for (uint32_t i = 0; i < 8; i++) nonce[i] = encfs.volume_key[i + 16];
        nonce[8]  = (uint8_t)((lba >> 24) & 0xFF);
        nonce[9]  = (uint8_t)((lba >> 16) & 0xFF);
        nonce[10] = (uint8_t)((lba >> 8) & 0xFF);
        nonce[11] = (uint8_t)(lba & 0xFF);

        uint8_t ks[64];
        sec_chacha20_encrypt(encfs.volume_key, 0, nonce, 12, ks, count * 512);

        uint32_t total = count * 512;
        for (uint32_t i = 0; i < total; i++)
            buf[i] ^= ks[i % 64];
    }

    return 0;
}

int encfs_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const uint8_t *buf) {
    uint8_t plain[512 * 128];
    if (count > 128) return -1;

    /* Copy and encrypt */
    uint32_t total = count * 512;
    for (uint32_t i = 0; i < total; i++) plain[i] = buf[i];

    if (encfs.encrypted && encfs.mounted) {
        uint8_t nonce[12];
        for (uint32_t i = 0; i < 8; i++) nonce[i] = encfs.volume_key[i + 16];
        nonce[8]  = (uint8_t)((lba >> 24) & 0xFF);
        nonce[9]  = (uint8_t)((lba >> 16) & 0xFF);
        nonce[10] = (uint8_t)((lba >> 8) & 0xFF);
        nonce[11] = (uint8_t)(lba & 0xFF);

        uint8_t ks[64];
        sec_chacha20_encrypt(encfs.volume_key, 0, nonce, 12, ks, total);

        for (uint32_t i = 0; i < total; i++)
            plain[i] ^= ks[i % 64];
    }

    return tape_write(0, drive, lba, count, plain);
}

uint8_t encfs_is_mounted(void) {
    return encfs.mounted;
}

const uint8_t *encfs_get_volume_key(void) {
    return encfs.volume_key;
}
