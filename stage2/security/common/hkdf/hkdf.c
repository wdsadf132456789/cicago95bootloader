/**
 * Chicago-95 Crypto: HMAC, HKDF, PBKDF2
 * Key derivation and authentication functions
 */

#include "boot/security.h"

/* ---- HMAC-SHA256 ---- */
void sec_hmac_sha256(const uint8_t *key, uint32_t key_len,
                     const uint8_t *data, uint32_t data_len,
                     uint8_t out[32]) {
    uint8_t k_pad[64];
    uint8_t o_key[64], i_key[64];
    uint8_t hash_tmp[32];

    /* If key > block size, hash it first */
    if (key_len > 64) {
        sec_sha256(key, key_len, hash_tmp);
        for (uint32_t i = 0; i < 32; i++) k_pad[i] = hash_tmp[i];
        for (uint32_t i = 32; i < 64; i++) k_pad[i] = 0;
        key_len = 32;
    } else {
        for (uint32_t i = 0; i < 64; i++) k_pad[i] = (i < key_len) ? key[i] : 0;
    }

    /* XOR with ipad and opad */
    for (uint32_t i = 0; i < 64; i++) {
        i_key[i] = k_pad[i] ^ 0x36;
        o_key[i] = k_pad[i] ^ 0x5C;
    }

    /* Inner hash: SHA256(ipad || data) */
    sec_sha256_ctx_t inner;
    sec_sha256_init(&inner);
    sec_sha256_update(&inner, i_key, 64);
    sec_sha256_update(&inner, data, data_len);
    sec_sha256_final(&inner, hash_tmp);

    /* Outer hash: SHA256(opad || inner_hash) */
    sec_sha256_ctx_t outer;
    sec_sha256_init(&outer);
    sec_sha256_update(&outer, o_key, 64);
    sec_sha256_update(&outer, hash_tmp, 32);
    sec_sha256_final(&outer, out);
}

/* ---- HMAC-SHA512 ---- */
void sec_hmac_sha512(const uint8_t key[64],
                     const uint8_t *msg, uint32_t msg_len,
                     uint8_t out[64]) {
    uint8_t k_pad[128];
    uint8_t o_key[128], i_key[128];
    uint8_t hash_tmp[64];

    if (64 > 128) {
        sec_sha512(key, 64, hash_tmp);
        for (uint32_t i = 0; i < 64; i++) k_pad[i] = hash_tmp[i];
        for (uint32_t i = 64; i < 128; i++) k_pad[i] = 0;
    } else {
        for (uint32_t i = 0; i < 128; i++) k_pad[i] = (i < 64) ? key[i] : 0;
    }

    for (uint32_t i = 0; i < 128; i++) {
        i_key[i] = k_pad[i] ^ 0x36;
        o_key[i] = k_pad[i] ^ 0x5C;
    }

    /* Inner: SHA512(ipad || data) */
    uint8_t inner_buf[256];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < 128; i++) inner_buf[pos++] = i_key[i];
    for (uint32_t i = 0; i < msg_len; i++) inner_buf[pos++] = msg[i];
    sec_sha512(inner_buf, pos, hash_tmp);

    /* Outer: SHA512(opad || inner) */
    uint8_t outer_buf[256];
    pos = 0;
    for (uint32_t i = 0; i < 128; i++) outer_buf[pos++] = o_key[i];
    for (uint32_t i = 0; i < 64; i++) outer_buf[pos++] = hash_tmp[i];
    sec_sha512(outer_buf, pos, out);
}

/* ---- HKDF-SHA256 (RFC 5869) ---- */
void sec_hkdf_sha256(const uint8_t *ikm, uint32_t ikm_len,
                     const uint8_t *salt, uint32_t salt_len,
                     const uint8_t *info, uint32_t info_len,
                     uint8_t *okm, uint32_t okm_len) {
    uint8_t prk[32];

    /* Extract: PRK = HMAC-SHA256(salt, IKM) */
    uint8_t default_salt[32] = {0};
    if (!salt || salt_len == 0) {
        sec_hmac_sha256(default_salt, 32, ikm, ikm_len, prk);
    } else {
        sec_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
    }

    /* Expand: OKM = T(1) || T(2) || ... || T(N) */
    uint8_t t[32];
    uint32_t t_len = 0;
    uint32_t done = 0;
    uint8_t counter = 1;

    while (done < okm_len) {
        uint8_t input[256];
        uint32_t pos = 0;

        if (t_len > 0) {
            for (uint32_t i = 0; i < t_len; i++) input[pos++] = t[i];
        }
        for (uint32_t i = 0; i < info_len; i++) input[pos++] = info[i];
        input[pos++] = counter;

        sec_hmac_sha256(prk, 32, input, pos, t);
        t_len = 32;

        uint32_t copy = okm_len - done;
        if (copy > 32) copy = 32;
        for (uint32_t i = 0; i < copy; i++) okm[done++] = t[i];
        counter++;
    }
}

/* ---- HKDF-SHA512 ---- */
void sec_hkdf_sha512(const uint8_t *ikm, uint32_t ikm_len,
                     const uint8_t *salt, uint32_t salt_len,
                     const uint8_t *info, uint32_t info_len,
                     uint8_t *okm, uint32_t okm_len) {
    uint8_t prk[64];

    uint8_t default_salt[128] = {0};
    if (!salt || salt_len == 0) {
        sec_hmac_sha512(default_salt, ikm, ikm_len, prk);
    } else {
        sec_hmac_sha512(salt, ikm, ikm_len, prk);
    }

    uint8_t t[64];
    uint32_t t_len = 0;
    uint32_t done = 0;
    uint8_t counter = 1;

    while (done < okm_len) {
        uint8_t input[256];
        uint32_t pos = 0;
        if (t_len > 0) {
            for (uint32_t i = 0; i < t_len; i++) input[pos++] = t[i];
        }
        for (uint32_t i = 0; i < info_len; i++) input[pos++] = info[i];
        input[pos++] = counter;

        sec_hmac_sha512(prk, input, pos, t);
        t_len = 64;

        uint32_t copy = okm_len - done;
        if (copy > 64) copy = 64;
        for (uint32_t i = 0; i < copy; i++) okm[done++] = t[i];
        counter++;
    }
}

/* ---- PBKDF2-SHA1 (for WPA2-Personal) ---- */
static void pbkdf2_sha1_block(const uint8_t *password, uint32_t pw_len,
                               const uint8_t *salt, uint32_t salt_len,
                               uint32_t iterations, uint32_t block_num,
                               uint8_t out[20]) {
    /* U1 = HMAC-SHA1(password, salt || block_num) */
    uint8_t salt_block[256];
    uint32_t pos = 0;
    for (uint32_t i = 0; i < salt_len; i++) salt_block[pos++] = salt[i];
    salt_block[pos++] = (block_num >> 24) & 0xFF;
    salt_block[pos++] = (block_num >> 16) & 0xFF;
    salt_block[pos++] = (block_num >> 8) & 0xFF;
    salt_block[pos++] = block_num & 0xFF;

    uint8_t u[20], tmp[20];
    /* HMAC-SHA1 for U1 */
    uint8_t key_pad[64];
    uint8_t i_key[64], o_key[64];

    if (pw_len > 64) {
        uint8_t h[32];
        sec_sha256(password, pw_len, h);
        for (uint32_t i = 0; i < 20; i++) key_pad[i] = h[i];
        for (uint32_t i = 20; i < 64; i++) key_pad[i] = 0;
    } else {
        for (uint32_t i = 0; i < 64; i++) key_pad[i] = (i < pw_len) ? password[i] : 0;
    }

    for (uint32_t i = 0; i < 64; i++) {
        i_key[i] = key_pad[i] ^ 0x36;
        o_key[i] = key_pad[i] ^ 0x5C;
    }

    /* Inner hash */
    uint8_t inner[256];
    uint32_t ipos = 0;
    for (uint32_t i = 0; i < 64; i++) inner[ipos++] = i_key[i];
    for (uint32_t i = 0; i < pos; i++) inner[ipos++] = salt_block[i];

    uint8_t inner_hash[32];
    sec_sha256(inner, ipos, inner_hash);

    /* Outer hash */
    uint8_t outer[256];
    uint32_t opos = 0;
    for (uint32_t i = 0; i < 64; i++) outer[opos++] = o_key[i];
    for (uint32_t i = 0; i < 32; i++) outer[opos++] = inner_hash[i];

    uint8_t full_hash[32];
    sec_sha256(outer, opos, full_hash);
    for (uint32_t i = 0; i < 20; i++) u[i] = full_hash[i];
    for (uint32_t i = 0; i < 20; i++) out[i] = u[i];

    /* U2 through Uc */
    for (uint32_t iter = 1; iter < iterations; iter++) {
        /* HMAC-SHA1(password, U_prev) */
        ipos = 0;
        for (uint32_t i = 0; i < 64; i++) inner[ipos++] = i_key[i];
        for (uint32_t i = 0; i < 20; i++) inner[ipos++] = u[i];
        sec_sha256(inner, ipos, inner_hash);
        opos = 0;
        for (uint32_t i = 0; i < 64; i++) outer[opos++] = o_key[i];
        for (uint32_t i = 0; i < 32; i++) outer[opos++] = inner_hash[i];
        sec_sha256(outer, opos, full_hash);
        for (uint32_t i = 0; i < 20; i++) u[i] = full_hash[i];
        for (uint32_t i = 0; i < 20; i++) out[i] ^= u[i];
    }
}

void sec_pbkdf2_sha1(const uint8_t *password, uint32_t pw_len,
                     const uint8_t *salt, uint32_t salt_len,
                     uint32_t iterations,
                     uint8_t *out, uint32_t out_len) {
    uint32_t blocks = (out_len + 19) / 20;
    uint32_t done = 0;

    for (uint32_t b = 1; b <= blocks; b++) {
        uint8_t block[20];
        pbkdf2_sha1_block(password, pw_len, salt, salt_len, iterations, b, block);
        uint32_t copy = out_len - done;
        if (copy > 20) copy = 20;
        for (uint32_t i = 0; i < copy; i++) out[done++] = block[i];
    }
}

/* ---- PBKDF2-SHA256 ---- */
void sec_pbkdf2_sha256(const uint8_t *password, uint32_t pw_len,
                       const uint8_t *salt, uint32_t salt_len,
                       uint32_t iterations,
                       uint8_t *out, uint32_t out_len) {
    uint32_t blocks = (out_len + 31) / 32;
    uint32_t done = 0;

    for (uint32_t b = 1; b <= blocks; b++) {
        uint8_t salt_block[256];
        uint32_t pos = 0;
        for (uint32_t i = 0; i < salt_len; i++) salt_block[pos++] = salt[i];
        salt_block[pos++] = (b >> 24) & 0xFF;
        salt_block[pos++] = (b >> 16) & 0xFF;
        salt_block[pos++] = (b >> 8) & 0xFF;
        salt_block[pos++] = b & 0xFF;

        uint8_t u[32], t[32];
        sec_hmac_sha256(password, pw_len, salt_block, pos, u);
        for (uint32_t i = 0; i < 32; i++) t[i] = u[i];

        for (uint32_t iter = 1; iter < iterations; iter++) {
            sec_hmac_sha256(password, pw_len, u, 32, u);
            for (uint32_t i = 0; i < 32; i++) t[i] ^= u[i];
        }

        uint32_t copy = out_len - done;
        if (copy > 32) copy = 32;
        for (uint32_t i = 0; i < copy; i++) out[done++] = t[i];
    }
}

/* AES-CCMP implementation lives in crypto.c */

/* ---- Random number generator ---- */
static uint64_t rng_state = 0;
static uint8_t rng_initialized = 0;

void sec_random_bytes(uint8_t *buf, uint32_t len) {
    if (!rng_initialized) {
        uint32_t lo, hi;
        asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
        rng_state = ((uint64_t)hi << 32) | lo;
        rng_initialized = 1;
    }

    for (uint32_t i = 0; i < len; i++) {
        /* xorshift64 */
        rng_state ^= rng_state << 13;
        rng_state ^= rng_state >> 7;
        rng_state ^= rng_state << 17;
        buf[i] = (uint8_t)(rng_state & 0xFF);
    }
}

uint32_t sec_random_u32(void) {
    uint8_t buf[4];
    sec_random_bytes(buf, 4);
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | buf[3];
}
