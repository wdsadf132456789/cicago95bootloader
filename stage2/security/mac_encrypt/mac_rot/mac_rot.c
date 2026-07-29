/**
 * Chicago-95 MAC Encrypter #4: MAC Rotator
 * Time-based MAC rotation at configurable intervals
 * Maintains a chain of derived MACs from a seed
 */

#include "boot/security.h"

#define MAC_ROT_MAX_SEEDS  16
#define MAC_ROT_MAX_CHAIN  64

typedef struct {
    uint8_t  original_mac[6];
    uint8_t  current_mac[6];
    uint8_t  seed_chain[MAC_ROT_MAX_CHAIN][6];
    uint32_t chain_len;
    uint32_t chain_idx;
    uint32_t interval_ms;     /* Rotation interval */
    uint64_t last_rotate_at;  /* Timestamp of last rotation */
    uint64_t boot_time;       /* TSC at boot */
    uint8_t  auto_rotate;     /* Auto-rotate on tick */
    uint8_t  preserve_unicast;
    uint8_t  preserve_locally;
    uint32_t total_rotations;
    uint8_t  initialized;
    sec_stats_t stats;
} mac_rot_state_t;

static mac_rot_state_t mac_rot;

/* Get TSC timestamp */
static uint64_t get_tsc(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* Estimate TSC frequency (rough: assume ~1GHz) */
static uint64_t ms_to_tsc(uint32_t ms) {
    return (uint64_t)ms * 1000000ULL;
}

/* Derive next MAC in chain using HMAC */
static void derive_next_mac(const uint8_t prev[6], uint8_t next[6], uint32_t idx) {
    uint8_t input[32];
    uint8_t hash[32];

    for (uint32_t i = 0; i < 6; i++) input[i] = prev[i];
    input[6] = (idx >> 24) & 0xFF;
    input[7] = (idx >> 16) & 0xFF;
    input[8] = (idx >> 8) & 0xFF;
    input[9] = idx & 0xFF;
    for (uint32_t i = 10; i < 32; i++) input[i] = 0;

    sec_hkdf_sha256(prev, 6,
                    (const uint8_t*)"Chicago95 MAC Rotate Chain", 27,
                    input, 32, hash, 32);

    for (uint32_t i = 0; i < 6; i++) next[i] = hash[i];

    /* Enforce IEEE 802 bits */
    next[0] |= 0x02;   /* Locally administered */
    next[0] &= ~0x01;  /* Unicast */
}

/* ---- Init ---- */
int mac_rot_init(const uint8_t original_mac[6], uint32_t interval_ms) {
    if (!original_mac) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&mac_rot;
    for (uint32_t i = 0; i < sizeof(mac_rot_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) mac_rot.original_mac[i] = original_mac[i];
    for (uint32_t i = 0; i < 6; i++) mac_rot.current_mac[i] = original_mac[i];

    mac_rot.interval_ms = interval_ms ? interval_ms : 60000; /* Default 1 min */
    mac_rot.boot_time = get_tsc();
    mac_rot.last_rotate_at = mac_rot.boot_time;
    mac_rot.auto_rotate = 1;
    mac_rot.preserve_unicast = 1;
    mac_rot.preserve_locally = 1;

    /* Generate seed chain */
    mac_rot.chain_len = MAC_ROT_MAX_CHAIN;
    for (uint32_t i = 0; i < mac_rot.chain_len; i++) {
        if (i == 0) {
            derive_next_mac(mac_rot.original_mac, mac_rot.seed_chain[0], 0);
        } else {
            derive_next_mac(mac_rot.seed_chain[i - 1], mac_rot.seed_chain[i], i);
        }
    }

    mac_rot.initialized = 1;
    return SEC_OK;
}

/* ---- Get current MAC ---- */
int mac_rot_get(uint8_t out[6]) {
    if (!out) return SEC_ERR_BAD_PARAM;
    if (!mac_rot.initialized) return SEC_ERR_NOT_INIT;

    for (uint32_t i = 0; i < 6; i++) out[i] = mac_rot.current_mac[i];
    return SEC_OK;
}

/* ---- Manual rotate ---- */
int mac_rot_rotate(void) {
    if (!mac_rot.initialized) return SEC_ERR_NOT_INIT;

    mac_rot.chain_idx = (mac_rot.chain_idx + 1) % mac_rot.chain_len;
    for (uint32_t i = 0; i < 6; i++)
        mac_rot.current_mac[i] = mac_rot.seed_chain[mac_rot.chain_idx][i];

    mac_rot.last_rotate_at = get_tsc();
    mac_rot.total_rotations++;

    mac_rot.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Auto-rotate check (call periodically) ---- */
int mac_rot_tick(boot_nic_t *nic) {
    if (!mac_rot.initialized) return SEC_ERR_NOT_INIT;
    if (!mac_rot.auto_rotate) return SEC_OK;

    uint64_t now = get_tsc();
    uint64_t elapsed = now - mac_rot.last_rotate_at;
    uint64_t threshold = ms_to_tsc(mac_rot.interval_ms);

    if (elapsed >= threshold) {
        mac_rot_rotate();
        if (nic) {
            boot_nic_set_mac(nic, mac_rot.current_mac);
        }
    }

    return SEC_OK;
}

/* ---- Apply to NIC ---- */
int mac_rot_apply(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, mac_rot.current_mac);
    mac_rot.stats.connections_opened++;
    return SEC_OK;
}

/* ---- Restore original ---- */
int mac_rot_restore(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, mac_rot.original_mac);
    return SEC_OK;
}

/* ---- Jump to specific chain index ---- */
int mac_rot_jump_to(uint32_t index) {
    if (index >= mac_rot.chain_len) return SEC_ERR_BAD_PARAM;

    mac_rot.chain_idx = index;
    for (uint32_t i = 0; i < 6; i++)
        mac_rot.current_mac[i] = mac_rot.seed_chain[index][i];

    return SEC_OK;
}

void mac_rot_set_interval(uint32_t interval_ms) {
    mac_rot.interval_ms = interval_ms;
}

void mac_rot_set_auto(int enable) {
    mac_rot.auto_rotate = enable ? 1 : 0;
}

void mac_rot_set_preserve(int preserve_unicast, int preserve_locally) {
    mac_rot.preserve_unicast = preserve_unicast ? 1 : 0;
    mac_rot.preserve_locally = preserve_locally ? 1 : 0;
}

void mac_rot_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&mac_rot.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
