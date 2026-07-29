/**
 * Chicago-95 Bootloader — DISK-8 SMART Serial Obfuscator
 * Hides ATA drive serial numbers and model strings exposed by
 * ATA IDENTIFY DEVICE and SMART data. smartctl/lsblk would show
 * the obfuscated values instead of real hardware identifiers.
 */

#include "boot/security.h"
#include <stdint.h>

#define SMART_SERIAL_MAX  32
#define SMART_MODEL_MAX   40

static struct {
    uint8_t   key[32];
    uint8_t   nonce[12];
    uint32_t  serials_hidden;
    uint32_t  models_hidden;
    uint32_t  serials_revealed;
    uint32_t  models_revealed;
} smart_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* XOR + byte-swap obfuscation for serial strings */
static void obfuscate_string(const uint8_t *in, uint32_t in_len,
                             uint8_t *out, uint32_t out_max) {
    uint32_t len = in_len < out_max ? in_len : out_max;

    uint8_t ks[32];
    sec_chacha20_encrypt(smart_state.key, 0x534D4152, /* "SMAR" */
                         smart_state.nonce, 12, ks, 32);

    for (uint32_t i = 0; i < len; i++) {
        /* XOR with keystream, then byte-reverse within word pairs */
        out[i] = in[i] ^ ks[i % 32];
        /* Swap adjacent bytes for visual obfuscation */
        if ((i & 1) == 0 && (i + 1) < len) {
            uint8_t tmp = out[i];
            out[i] = out[i + 1];
            out[i + 1] = tmp;
        }
    }

    sec_memzero(ks, sizeof(ks));
}

static void deobfuscate_string(const uint8_t *in, uint32_t in_len,
                               uint8_t *out, uint32_t out_max) {
    uint32_t len = in_len < out_max ? in_len : out_max;

    uint8_t ks[32];
    sec_chacha20_encrypt(smart_state.key, 0x534D4152,
                         smart_state.nonce, 12, ks, 32);

    for (uint32_t i = 0; i < len; i++) {
        out[i] = in[i];
    }
    /* Reverse byte-swap first */
    for (uint32_t i = 0; i < len; i += 2) {
        if ((i + 1) < len) {
            uint8_t tmp = out[i];
            out[i] = out[i + 1];
            out[i + 1] = tmp;
        }
    }
    /* Then XOR unmask */
    for (uint32_t i = 0; i < len; i++)
        out[i] ^= ks[i % 32];

    sec_memzero(ks, sizeof(ks));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_smart_obfuscate_init(void) {
    sec_random_bytes(smart_state.key, 32);
    sec_random_bytes(smart_state.nonce, 12);
    smart_state.serials_hidden = 0;
    smart_state.models_hidden = 0;
    smart_state.serials_revealed = 0;
    smart_state.models_revealed = 0;
    return 0;
}

int disk_smart_hide_serial(const uint8_t serial_in[DISK_SERIAL_LEN],
                           uint8_t serial_out[DISK_SERIAL_LEN]) {
    if (!serial_in || !serial_out) return -1;
    obfuscate_string(serial_in, DISK_SERIAL_LEN, serial_out, DISK_SERIAL_LEN);
    smart_state.serials_hidden++;
    return 0;
}

int disk_smart_reveal_serial(const uint8_t serial_in[DISK_SERIAL_LEN],
                             uint8_t serial_out[DISK_SERIAL_LEN]) {
    if (!serial_in || !serial_out) return -1;
    deobfuscate_string(serial_in, DISK_SERIAL_LEN, serial_out, DISK_SERIAL_LEN);
    smart_state.serials_revealed++;
    return 0;
}

int disk_smart_hide_model(const uint8_t *model_in, uint32_t model_len,
                          uint8_t *model_out) {
    if (!model_in || !model_out || model_len == 0) return -1;
    obfuscate_string(model_in, model_len, model_out, SMART_MODEL_MAX);
    smart_state.models_hidden++;
    return 0;
}

int disk_smart_reveal_model(const uint8_t *model_in, uint32_t model_len,
                            uint8_t *model_out) {
    if (!model_in || !model_out || model_len == 0) return -1;
    deobfuscate_string(model_in, model_len, model_out, SMART_MODEL_MAX);
    smart_state.models_revealed++;
    return 0;
}

void disk_smart_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = smart_state.serials_hidden + smart_state.models_hidden;
    stats->connections_active = smart_state.serials_revealed + smart_state.models_revealed;
}
