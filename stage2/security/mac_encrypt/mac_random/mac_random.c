/**
 * Chicago-95 MAC Encrypter #1: MAC Randomizer
 * Generates cryptographically random MAC address at boot
 * Preserves locally-administered + unicast bits per IEEE 802
 */

#include "boot/security.h"

#define MAC_RANDOM_HISTORY 16

typedef struct {
    uint8_t  original_mac[6];
    uint8_t  randomized_mac[6];
    uint8_t  history[MAC_RANDOM_HISTORY][6];
    uint32_t history_count;
    uint32_t rotation_count;
    uint8_t  preserve_unicast;   /* Keep multicast bit from original */
    uint8_t  preserve_locally;   /* Keep locally-administered bit */
    uint8_t  initialized;
    sec_stats_t stats;
} mac_random_state_t;

static mac_random_state_t mac_rand;

/* Validate MAC format */
static int mac_is_valid(const uint8_t mac[6]) {
    /* Check not all zeros */
    uint8_t sum = 0;
    for (uint32_t i = 0; i < 6; i++) sum |= mac[i];
    return sum != 0;
}

/* Check if MAC is multicast */
static int mac_is_multicast(const uint8_t mac[6]) {
    return (mac[0] & 0x01) != 0;
}

/* Check if locally administered */
static int mac_is_locally_administered(const uint8_t mac[6]) {
    return (mac[0] & 0x02) != 0;
}

/* Generate random MAC with proper IEEE 802 bits */
static void generate_random_mac(uint8_t out[6]) {
    sec_random_bytes(out, 6);

    /* Set locally-administered bit (bit 1) */
    out[0] |= 0x02;

    /* Clear multicast bit (bit 0) - unicast */
    out[0] &= ~0x01;

    /* Clear group address bit */
    out[0] &= ~0x01;
}

/* Check MAC not in history (avoid collisions) */
static int mac_in_history(const uint8_t mac[6]) {
    for (uint32_t i = 0; i < mac_rand.history_count; i++) {
        int match = 1;
        for (uint32_t j = 0; j < 6; j++) {
            if (mac_rand.history[i][j] != mac[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* Add MAC to history */
static void mac_add_history(const uint8_t mac[6]) {
    if (mac_rand.history_count >= MAC_RANDOM_HISTORY) {
        /* Shift history */
        for (uint32_t i = 0; i < MAC_RANDOM_HISTORY - 1; i++) {
            for (uint32_t j = 0; j < 6; j++)
                mac_rand.history[i][j] = mac_rand.history[i + 1][j];
        }
        mac_rand.history_count = MAC_RANDOM_HISTORY - 1;
    }

    for (uint32_t j = 0; j < 6; j++)
        mac_rand.history[mac_rand.history_count][j] = mac[j];
    mac_rand.history_count++;
}

/* ---- Init ---- */
int mac_random_init(const uint8_t original_mac[6]) {
    if (!original_mac) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&mac_rand;
    for (uint32_t i = 0; i < sizeof(mac_random_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) mac_rand.original_mac[i] = original_mac[i];

    mac_rand.preserve_unicast = 1;
    mac_rand.preserve_locally = 1;

    /* Generate first random MAC */
    generate_random_mac(mac_rand.randomized_mac);

    /* Add original to history */
    mac_add_history(mac_rand.original_mac);
    mac_add_history(mac_rand.randomized_mac);

    mac_rand.initialized = 1;
    return SEC_OK;
}

/* ---- Get randomized MAC ---- */
int mac_random_get(uint8_t out[6]) {
    if (!out) return SEC_ERR_BAD_PARAM;
    if (!mac_rand.initialized) return SEC_ERR_NOT_INIT;

    for (uint32_t i = 0; i < 6; i++) out[i] = mac_rand.randomized_mac[i];
    return SEC_OK;
}

/* ---- Generate new random MAC ---- */
int mac_random_rotate(void) {
    if (!mac_rand.initialized) return SEC_ERR_NOT_INIT;

    /* Save current to history */
    mac_add_history(mac_rand.randomized_mac);

    uint8_t new_mac[6];
    uint32_t attempts = 0;

    do {
        generate_random_mac(new_mac);

        /* Preserve bits from original if configured */
        if (mac_rand.preserve_unicast && !mac_is_multicast(mac_rand.original_mac)) {
            new_mac[0] &= ~0x01;
        }
        if (mac_rand.preserve_locally && mac_is_locally_administered(mac_rand.original_mac)) {
            new_mac[0] |= 0x02;
        }

        attempts++;
    } while (mac_in_history(new_mac) && attempts < 100);

    for (uint32_t i = 0; i < 6; i++) mac_rand.randomized_mac[i] = new_mac[i];
    mac_add_history(new_mac);
    mac_rand.rotation_count++;

    mac_rand.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Apply to NIC ---- */
int mac_random_apply(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    uint8_t mac[6];
    mac_random_get(mac);
    boot_nic_set_mac(nic, mac);

    mac_rand.stats.connections_opened++;
    return SEC_OK;
}

/* ---- Restore original MAC ---- */
int mac_random_restore(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, mac_rand.original_mac);
    return SEC_OK;
}

/* ---- Configuration ---- */
void mac_random_set_preserve(int preserve_unicast, int preserve_locally) {
    mac_rand.preserve_unicast = preserve_unicast ? 1 : 0;
    mac_rand.preserve_locally = preserve_locally ? 1 : 0;
}

void mac_random_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&mac_rand.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
