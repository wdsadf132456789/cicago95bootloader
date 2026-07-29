/**
 * Chicago-95 Crypto: AES-256-GCM
 * Bare-metal AES-256 block cipher + Galois/Counter Mode (GCM)
 */

#include "boot/security.h"

#define AES_BLOCK_SIZE  16
#define AES_256_ROUNDS  14
#define AES_256_KEY_LEN 32

/* AES S-box */
static const uint8_t sbox[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};

/* Round constants */
static const uint8_t rcon[10] = {
    0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1B, 0x36
};

/* GF(2^8) multiplication for GCM */
static uint8_t gfmul(uint8_t a, uint8_t b) {
    uint8_t p = 0;
    for (uint32_t i = 0; i < 8; i++) {
        if (b & 1) p ^= a;
        uint8_t hi = a & 0x80;
        a <<= 1;
        if (hi) a ^= 0x1B;
        b >>= 1;
    }
    return p;
}

/* ---- AES-256 Key Expansion ---- */
void sec_aes256_init(sec_aes256_ctx_t *ctx, const uint8_t key[32]) {
    uint8_t (*rk)[16] = (uint8_t (*)[16])ctx->round_keys;

    /* First 4 words from key */
    for (uint32_t i = 0; i < 4; i++) {
        for (uint32_t j = 0; j < 4; j++)
            rk[i][j] = key[i * 4 + j];
    }

    uint32_t words = 4;
    for (uint32_t i = 4; i < 60; i++) {
        uint8_t temp[4];
        for (uint32_t j = 0; j < 4; j++) temp[j] = rk[(i - 1) >> 2][(i - 1) % 4 * 4 + j];

        if (i % 4 == 0) {
            /* RotWord + SubWord + Rcon */
            uint8_t t = temp[0];
            temp[0] = sbox[temp[1]] ^ rcon[i / 4 - 1];
            temp[1] = sbox[temp[2]];
            temp[2] = sbox[temp[3]];
            temp[3] = sbox[t];
        }

        for (uint32_t j = 0; j < 4; j++)
            rk[i >> 2][(i % 4) * 4 + j] = rk[(i - 4) >> 2][(i % 4) * 4 + j] ^ temp[j];
    }
}

/* ---- AES-256 Encrypt (single block) ---- */
void sec_aes256_encrypt_block(sec_aes256_ctx_t *ctx,
                              const uint8_t in[16], uint8_t out[16]) {
    const uint8_t (*rk)[16] = (const uint8_t (*)[16])ctx->round_keys;
    uint8_t state[16];

    /* AddRoundKey(0) */
    for (uint32_t i = 0; i < 16; i++) state[i] = in[i] ^ rk[0][i];

    for (uint32_t round = 1; round <= 14; round++) {
        /* SubBytes */
        for (uint32_t i = 0; i < 16; i++) state[i] = sbox[state[i]];

        /* ShiftRows */
        uint8_t tmp;
        tmp = state[1]; state[1] = state[5]; state[5] = state[9]; state[9] = state[13]; state[13] = tmp;
        tmp = state[2]; state[2] = state[10]; state[10] = tmp;
        tmp = state[6]; state[6] = state[14]; state[14] = tmp;
        tmp = state[15]; state[15] = state[11]; state[11] = state[7]; state[7] = state[3]; state[3] = tmp;

        /* MixColumns (skip on last round) */
        if (round < 14) {
            for (uint32_t c = 0; c < 4; c++) {
                uint8_t a0 = state[c * 4 + 0];
                uint8_t a1 = state[c * 4 + 1];
                uint8_t a2 = state[c * 4 + 2];
                uint8_t a3 = state[c * 4 + 3];
                state[c * 4 + 0] = gfmul(a0, 2) ^ gfmul(a1, 3) ^ a2 ^ a3;
                state[c * 4 + 1] = a0 ^ gfmul(a1, 2) ^ gfmul(a2, 3) ^ a3;
                state[c * 4 + 2] = a0 ^ a1 ^ gfmul(a2, 2) ^ gfmul(a3, 3);
                state[c * 4 + 3] = gfmul(a0, 3) ^ a1 ^ a2 ^ gfmul(a3, 2);
            }
        }

        /* AddRoundKey */
        for (uint32_t i = 0; i < 16; i++) state[i] ^= rk[round][i];
    }

    for (uint32_t i = 0; i < 16; i++) out[i] = state[i];
}

/* ---- AES-256 CTR mode encryption (used by GCM) ---- */
static void aes256_ctr(sec_aes256_ctx_t *ctx, const uint8_t nonce[12],
                       const uint8_t *in, uint8_t *out, uint32_t len) {
    uint8_t counter[16];
    uint8_t keystream[16];

    for (uint32_t i = 0; i < 12; i++) counter[i] = nonce[i];
    counter[12] = counter[13] = counter[14] = 0;
    counter[15] = 1;

    uint32_t blocks = (len + 15) / 16;
    for (uint32_t b = 0; b < blocks; b++) {
        sec_aes256_encrypt_block(ctx, counter, keystream);
        uint32_t start = b * 16;
        uint32_t end = start + 16;
        if (end > len) end = len;
        for (uint32_t i = start; i < end; i++)
            out[i] = in[i] ^ keystream[i - start];

        /* Increment counter */
        for (int i = 15; i >= 0; i--) {
            if (++counter[i]) break;
        }
    }
}

/* ---- GCM GHASH (Galois hash) ---- */
static void ghash(uint8_t result[16], const uint8_t *h_in, const uint8_t *data, uint32_t len) {
    uint8_t y[16] = {0};
    uint8_t h[16];
    for (uint32_t i = 0; i < 16; i++) h[i] = h_in[i];

    uint32_t blocks = (len + 15) / 16;
    for (uint32_t b = 0; b < blocks; b++) {
        for (uint32_t i = 0; i < 16; i++) {
            uint32_t idx = b * 16 + i;
            y[i] ^= (idx < len) ? data[idx] : 0;
        }
        /* GF multiply Y by H */
        uint8_t z[16] = {0};
        for (uint32_t i = 0; i < 128; i++) {
            int bit = (y[i >> 3] >> (7 - (i % 8))) & 1;
            if (bit) {
                for (uint32_t j = 0; j < 16; j++) z[j] ^= h[j];
            }
            /* Shift H right */
            uint8_t carry = 0;
            for (int j = 15; j >= 0; j--) {
                uint8_t new_carry = h[j] & 1;
                h[j] = (h[j] >> 1) | (carry << 7);
                carry = new_carry;
            }
            if (carry) h[0] ^= 0xE1;
        }
        for (uint32_t i = 0; i < 16; i++) y[i] = z[i];
    }

    for (uint32_t i = 0; i < 16; i++) result[i] = y[i];
}

/* ---- AES-256-GCM Encrypt ---- */
void sec_aes256_gcm_encrypt(sec_aes256_ctx_t *ctx,
                            const uint8_t *plaintext, uint8_t *ciphertext,
                            uint32_t len,
                            const uint8_t iv[12],
                            const uint8_t *aad, uint32_t aad_len,
                            uint8_t tag[16]) {
    uint8_t h[16];
    sec_aes256_encrypt_block(ctx, (const uint8_t*)"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", h);

    /* J0 = IV || 0x00000001 */
    uint8_t j0[16];
    for (uint32_t i = 0; i < 12; i++) j0[i] = iv[i];
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;

    /* CTR encrypt plaintext */
    aes256_ctr(ctx, iv, plaintext, ciphertext, len);

    /* GHASH over AAD and ciphertext */
    uint32_t ghash_len = aad_len + len + 16 + 16;
    uint8_t *ghash_input = (uint8_t*)0;
    if (ghash_len <= 4096) {
        uint8_t buf[4096];
        ghash_input = buf;
    }

    uint32_t pos = 0;
    for (uint32_t i = 0; i < aad_len; i++) ghash_input[pos++] = aad[i];
    pos = (pos + 15) & ~15;
    for (uint32_t i = 0; i < len; i++) ghash_input[pos++] = ciphertext[i];
    pos = (pos + 15) & ~15;

    /* Length block: AAD len (64-bit) || CT len (64-bit) */
    uint64_t aad_bit_len = (uint64_t)aad_len * 8;
    uint64_t ct_bit_len = (uint64_t)len * 8;
    ghash_input[pos++] = (aad_bit_len >> 56) & 0xFF;
    ghash_input[pos++] = (aad_bit_len >> 48) & 0xFF;
    ghash_input[pos++] = (aad_bit_len >> 40) & 0xFF;
    ghash_input[pos++] = (aad_bit_len >> 32) & 0xFF;
    ghash_input[pos++] = (aad_bit_len >> 24) & 0xFF;
    ghash_input[pos++] = (aad_bit_len >> 16) & 0xFF;
    ghash_input[pos++] = (aad_bit_len >> 8) & 0xFF;
    ghash_input[pos++] = aad_bit_len & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 56) & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 48) & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 40) & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 32) & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 24) & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 16) & 0xFF;
    ghash_input[pos++] = (ct_bit_len >> 8) & 0xFF;
    ghash_input[pos++] = ct_bit_len & 0xFF;
    pos = (pos + 15) & ~15;

    uint8_t ghash_h[16];
    for (uint32_t i = 0; i < 16; i++) ghash_h[i] = h[i];
    uint8_t s[16];
    ghash(s, ghash_h, ghash_input, pos);

    /* Tag = GHASH ^ AES(J0) */
    uint8_t enc_j0[16];
    sec_aes256_encrypt_block(ctx, j0, enc_j0);
    for (uint32_t i = 0; i < 16; i++) tag[i] = s[i] ^ enc_j0[i];
}

/* ---- AES-256-GCM Decrypt ---- */
int sec_aes256_gcm_decrypt(sec_aes256_ctx_t *ctx,
                           const uint8_t *ciphertext, uint8_t *plaintext,
                           uint32_t len,
                           const uint8_t iv[12],
                           const uint8_t *aad, uint32_t aad_len,
                           const uint8_t tag[16]) {
    uint8_t h[16];
    sec_aes256_encrypt_block(ctx, (const uint8_t*)"\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", h);

    uint8_t j0[16];
    for (uint32_t i = 0; i < 12; i++) j0[i] = iv[i];
    j0[12] = 0; j0[13] = 0; j0[14] = 0; j0[15] = 1;

    /* GHAD first to verify tag */
    uint32_t ghash_len = aad_len + len + 16 + 16;
    uint8_t ghash_input[4096];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < aad_len; i++) ghash_input[pos++] = aad[i];
    pos = (pos + 15) & ~15;
    for (uint32_t i = 0; i < len; i++) ghash_input[pos++] = ciphertext[i];
    pos = (pos + 15) & ~15;

    uint64_t aad_bit_len = (uint64_t)aad_len * 8;
    uint64_t ct_bit_len = (uint64_t)len * 8;
    for (uint32_t i = 0; i < 8; i++) {
        ghash_input[pos++] = (aad_bit_len >> (56 - i * 8)) & 0xFF;
    }
    for (uint32_t i = 0; i < 8; i++) {
        ghash_input[pos++] = (ct_bit_len >> (56 - i * 8)) & 0xFF;
    }
    pos = (pos + 15) & ~15;

    uint8_t ghash_h[16];
    for (uint32_t i = 0; i < 16; i++) ghash_h[i] = h[i];
    uint8_t s[16];
    ghash(s, ghash_h, ghash_input, pos);

    uint8_t enc_j0[16];
    sec_aes256_encrypt_block(ctx, j0, enc_j0);
    uint8_t computed_tag[16];
    for (uint32_t i = 0; i < 16; i++) computed_tag[i] = s[i] ^ enc_j0[i];

    uint8_t diff = 0;
    for (uint32_t i = 0; i < 16; i++) diff |= tag[i] ^ computed_tag[i];
    if (diff) return SEC_ERR_CRYPTO;

    aes256_ctr(ctx, iv, ciphertext, plaintext, len);
    return SEC_OK;
}

void sec_aes256_done(sec_aes256_ctx_t *ctx) {
    uint8_t *d = (uint8_t*)ctx;
    for (uint32_t i = 0; i < sizeof(sec_aes256_ctx_t); i++) d[i] = 0;
}
