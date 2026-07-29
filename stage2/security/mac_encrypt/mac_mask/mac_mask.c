/**
 * Chicago-95 MAC Encrypter #3: MAC Masker
 * XOR-based MAC address masking with rotating key
 * Deterministic: same key always produces same masked MAC from original
 */

#include "boot/security.h"

#define MAC_MASK_HISTORY 32

typedef struct {
    uint8_t  original_mac[6];
    uint8_t  masked_mac[6];
    uint8_t  mask_key[6];         /* XOR mask key */
    uint8_t  rolling_key[32];     /* Extended key for rotation */
    uint32_t key_version;
    uint8_t  preserve_unicast;    /* Preserve IEEE bits after masking */
    uint8_t  preserve_locally;
    uint32_t rotation_count;
    uint8_t  initialized;
    sec_stats_t stats;
} mac_mask_state_t;

static mac_mask_state_t mac_mask;

/* IEEE 802 bit masks */
#define MAC_BIT_UNICAST          0x01
#define MAC_BIT_LOCALLY_ADMIN    0x02
#define MAC_BIT_MULTICAST        0x01
#define MAC_BIT_GROUP            0x01

/* Apply mask to single byte */
static uint8_t mask_byte(uint8_t val, uint8_t key_byte, uint8_t preserve_mask) {
    uint8_t preserved = val & preserve_mask;
    uint8_t masked = (val ^ key_byte) & ~preserve_mask;
    return preserved | masked;
}

/* Generate mask key from rolling key + version */
static void derive_mask_key(void) {
    /* Use HKDF to derive 6-byte mask from rolling key */
    uint8_t derived[32];
    uint8_t context[8];
    context[0] = (mac_mask.key_version >> 24) & 0xFF;
    context[1] = (mac_mask.key_version >> 16) & 0xFF;
    context[2] = (mac_mask.key_version >> 8) & 0xFF;
    context[3] = mac_mask.key_version & 0xFF;
    context[4] = 'M'; context[5] = 'A'; context[6] = 'C'; context[7] = 'M';

    sec_hkdf_sha256(mac_mask.rolling_key, 32,
                    (const uint8_t*)"Chicago95 MAC Mask", 18,
                    context, 8,
                    derived, 32);

    for (uint32_t i = 0; i < 6; i++) mac_mask.mask_key[i] = derived[i];
}

/* Apply mask to MAC address */
static void apply_mask(const uint8_t in[6], uint8_t out[6]) {
    uint8_t preserve_mask = 0;

    /* Always preserve IEEE-mandated bits */
    if (mac_mask.preserve_unicast) {
        preserve_mask |= MAC_BIT_UNICAST;
    }
    if (mac_mask.preserve_locally) {
        preserve_mask |= MAC_BIT_LOCALLY_ADMIN;
    }

    for (uint32_t i = 0; i < 6; i++) {
        out[i] = mask_byte(in[i], mac_mask.mask_key[i], preserve_mask);
    }
}

/* ---- Init ---- */
int mac_mask_init(const uint8_t original_mac[6], const uint8_t key[32]) {
    if (!original_mac) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&mac_mask;
    for (uint32_t i = 0; i < sizeof(mac_mask_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) mac_mask.original_mac[i] = original_mac[i];

    if (key) {
        for (uint32_t i = 0; i < 32; i++) mac_mask.rolling_key[i] = key[i];
    } else {
        /* Generate random key */
        sec_random_bytes(mac_mask.rolling_key, 32);
    }

    mac_mask.preserve_unicast = 1;
    mac_mask.preserve_locally = 1;
    mac_mask.key_version = 1;

    derive_mask_key();
    apply_mask(mac_mask.original_mac, mac_mask.masked_mac);

    mac_mask.initialized = 1;
    return SEC_OK;
}

/* ---- Get masked MAC ---- */
int mac_mask_get(uint8_t out[6]) {
    if (!out) return SEC_ERR_BAD_PARAM;
    if (!mac_mask.initialized) return SEC_ERR_NOT_INIT;

    for (uint32_t i = 0; i < 6; i++) out[i] = mac_mask.masked_mac[i];
    return SEC_OK;
}

/* ---- Rotate key ---- */
int mac_mask_rotate(const uint8_t new_key[32]) {
    if (!mac_mask.initialized) return SEC_ERR_NOT_INIT;

    if (new_key) {
        for (uint32_t i = 0; i < 32; i++) mac_mask.rolling_key[i] = new_key[i];
    } else {
        /* Derive next key from current */
        uint8_t next_key[32];
        sec_hkdf_sha256(mac_mask.rolling_key, 32,
                        (const uint8_t*)"Chicago95 MAC Rotate", 20,
                        (const uint8_t*)&mac_mask.key_version, 4,
                        next_key, 32);
        for (uint32_t i = 0; i < 32; i++) mac_mask.rolling_key[i] = next_key[i];
    }

    mac_mask.key_version++;
    derive_mask_key();
    apply_mask(mac_mask.original_mac, mac_mask.masked_mac);
    mac_mask.rotation_count++;

    mac_mask.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Unmask (reverse) ---- */
int mac_mask_unmask(const uint8_t masked[6], uint8_t original[6]) {
    if (!masked || !original) return SEC_ERR_BAD_PARAM;
    if (!mac_mask.initialized) return SEC_ERR_NOT_INIT;

    /* XOR is self-inverse: unmask = mask(masked) */
    uint8_t preserve_mask = 0;
    if (mac_mask.preserve_unicast) preserve_mask |= MAC_BIT_UNICAST;
    if (mac_mask.preserve_locally) preserve_mask |= MAC_BIT_LOCALLY_ADMIN;

    for (uint32_t i = 0; i < 6; i++) {
        original[i] = mask_byte(masked[i], mac_mask.mask_key[i], preserve_mask);
    }

    return SEC_OK;
}

/* ---- Apply to NIC ---- */
int mac_mask_apply(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    uint8_t mac[6];
    mac_mask_get(mac);
    boot_nic_set_mac(nic, mac);

    mac_mask.stats.connections_opened++;
    return SEC_OK;
}

/* ---- Restore original ---- */
int mac_mask_restore(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, mac_mask.original_mac);
    return SEC_OK;
}

void mac_mask_set_preserve(int preserve_unicast, int preserve_locally) {
    mac_mask.preserve_unicast = preserve_unicast ? 1 : 0;
    mac_mask.preserve_locally = preserve_locally ? 1 : 0;
}

void mac_mask_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&mac_mask.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
