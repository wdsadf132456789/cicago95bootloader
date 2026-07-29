/**
 * Chicago-95 Bootloader — DISK-9 ATA INQUIRY Scrambler
 * Scrambles ATA IDENTIFY DEVICE data fields (words 27-46 model,
 * words 10-19 serial, words 23-26 firmware rev). The 512-byte
 * IDENTIFY block is what smartctl/lsblk use to identify drives.
 */

#include "boot/security.h"
#include <stdint.h>

/* ATA IDENTIFY offsets (in 16-bit words): */
/* Serial: words 10-19 (20 bytes) */
/* Firmware: words 23-26 (8 bytes)  */
/* Model: words 27-46 (40 bytes)    */

#define IDENT_WORDS  256
#define SERIAL_OFF   10
#define SERIAL_WORDS 10
#define FW_OFF       23
#define FW_WORDS     4
#define MODEL_OFF    27
#define MODEL_WORDS  20

static struct {
    uint8_t  key[32];
    uint32_t scrambled;
    uint32_t unscrambled;
} inquiry_state;

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* Scramble a range of 16-bit words with byte-level XOR + swap */
static void scramble_words(uint16_t data[IDENT_WORDS], uint32_t offset,
                           uint32_t count) {
    uint8_t ks[32];
    sec_chacha20_encrypt(inquiry_state.key, (uint32_t)(offset << 16 | count),
                         (const uint8_t *)&offset, 4, ks, 32);

    for (uint32_t i = 0; i < count; i++) {
        uint16_t w = data[offset + i];
        uint8_t lo = (uint8_t)(w);
        uint8_t hi = (uint8_t)(w >> 8);

        /* XOR with keystream */
        lo ^= ks[(i * 2) % 32];
        hi ^= ks[(i * 2 + 1) % 32];

        /* Byte-swap within word */
        data[offset + i] = (uint16_t)((uint16_t)lo << 8 | hi);
    }

    sec_memzero(ks, sizeof(ks));
}

static void unscramble_words(uint16_t data[IDENT_WORDS], uint32_t offset,
                             uint32_t count) {
    uint8_t ks[32];
    sec_chacha20_encrypt(inquiry_state.key, (uint32_t)(offset << 16 | count),
                         (const uint8_t *)&offset, 4, ks, 32);

    for (uint32_t i = 0; i < count; i++) {
        uint16_t w = data[offset + i];
        uint8_t lo = (uint8_t)(w);
        uint8_t hi = (uint8_t)(w >> 8);

        /* Reverse byte-swap */
        uint8_t orig_lo = hi;
        uint8_t orig_hi = lo;

        /* Reverse XOR */
        orig_lo ^= ks[(i * 2) % 32];
        orig_hi ^= ks[(i * 2 + 1) % 32];

        data[offset + i] = (uint16_t)((uint16_t)orig_lo << 8 | orig_hi);
    }

    sec_memzero(ks, sizeof(ks));
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_inquiry_init(void) {
    sec_random_bytes(inquiry_state.key, 32);
    inquiry_state.scrambled = 0;
    inquiry_state.unscrambled = 0;
    return 0;
}

int disk_inquiry_scramble(uint16_t ident_data[256], uint16_t out[256]) {
    if (!ident_data || !out) return -1;

    for (uint32_t i = 0; i < IDENT_WORDS; i++)
        out[i] = ident_data[i];

    scramble_words(out, SERIAL_OFF, SERIAL_WORDS);
    scramble_words(out, FW_OFF, FW_WORDS);
    scramble_words(out, MODEL_OFF, MODEL_WORDS);

    inquiry_state.scrambled++;
    return 0;
}

int disk_inquiry_unscramble(uint16_t ident_data[256], uint16_t out[256]) {
    if (!ident_data || !out) return -1;

    for (uint32_t i = 0; i < IDENT_WORDS; i++)
        out[i] = ident_data[i];

    unscramble_words(out, SERIAL_OFF, SERIAL_WORDS);
    unscramble_words(out, FW_OFF, FW_WORDS);
    unscramble_words(out, MODEL_OFF, MODEL_WORDS);

    inquiry_state.unscrambled++;
    return 0;
}

int disk_inquiry_scramble_model(uint16_t ident_data[256]) {
    if (!ident_data) return -1;
    scramble_words(ident_data, MODEL_OFF, MODEL_WORDS);
    inquiry_state.scrambled++;
    return 0;
}

int disk_inquiry_scramble_serial(uint16_t ident_data[256]) {
    if (!ident_data) return -1;
    scramble_words(ident_data, SERIAL_OFF, SERIAL_WORDS);
    inquiry_state.scrambled++;
    return 0;
}

void disk_inquiry_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = inquiry_state.scrambled;
    stats->connections_active = inquiry_state.unscrambled;
}
