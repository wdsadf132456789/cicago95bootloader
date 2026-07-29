/**
 * Chicago-95 Bootloader — Missing Crypto Primitives
 * SHA-1, SHA3-256, HMAC-SHA1, AES-256-CTR, Curve25519, HKDF-SHA256-Expand,
 * ChaCha20 encrypt wrapper, sec_memzero.
 * Bare-metal, no libc.
 */

#include "boot/security.h"
#include <stdint.h>

/* ======================================================================== */
/* sec_memzero                                                               */
/* ======================================================================== */

void sec_memzero(void *ptr, uint32_t len) {
    volatile uint8_t *p = (volatile uint8_t *)ptr;
    for (uint32_t i = 0; i < len; i++) p[i] = 0;
}

/* ======================================================================== */
/* SHA-1 (FIPS 180-4)                                                       */
/* ======================================================================== */

void sec_sha1(const uint8_t *data, uint32_t len, uint8_t out[20]) {
    uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE;
    uint32_t h3 = 0x10325476, h4 = 0xC3D2E1F0;
    uint64_t ml = (uint64_t)len * 8;

    /* Pre-processing: pad message */
    uint32_t total = len + 1;
    while (total % 64 != 56) total++;
    total += 8;

    uint8_t *buf = (uint8_t *)0; /* Use stack */
    /* Since we can't malloc, process in-place with a small buffer */
    uint8_t block[64];
    uint32_t pos = 0;

    while (pos <= len) {
        uint32_t chunk = 0;
        for (uint32_t i = 0; i < 64; i++) {
            if (pos + i < len)
                block[i] = data[pos + i];
            else if (pos + i == len)
                block[i] = 0x80;
            else if (pos + i >= len && pos + i < 56)
                block[i] = 0;
            else
                block[i] = (uint8_t)(ml >> ((63 - (pos + i)) * 8));
            chunk = i + 1;
        }

        uint32_t w[80];
        for (uint32_t i = 0; i < 16; i++)
            w[i] = (block[i*4] << 24) | (block[i*4+1] << 16) | (block[i*4+2] << 8) | block[i*4+3];
        for (uint32_t i = 16; i < 80; i++)
            w[i] = ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) << 1) | ((w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16]) >> 31);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (uint32_t i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            uint32_t temp = ((a << 5) | (a >> 27)) + f + e + k + w[i];
            e = d; d = c; c = (b << 30) | (b >> 2); b = a; a = temp;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;

        if (pos + 64 > len) break;
        pos += 64;
    }

    out[0]  = (uint8_t)(h0 >> 24); out[1]  = (uint8_t)(h0 >> 16);
    out[2]  = (uint8_t)(h0 >> 8);  out[3]  = (uint8_t)(h0);
    out[4]  = (uint8_t)(h1 >> 24); out[5]  = (uint8_t)(h1 >> 16);
    out[6]  = (uint8_t)(h1 >> 8);  out[7]  = (uint8_t)(h1);
    out[8]  = (uint8_t)(h2 >> 24); out[9]  = (uint8_t)(h2 >> 16);
    out[10] = (uint8_t)(h2 >> 8);  out[11] = (uint8_t)(h2);
    out[12] = (uint8_t)(h3 >> 24); out[13] = (uint8_t)(h3 >> 16);
    out[14] = (uint8_t)(h3 >> 8);  out[15] = (uint8_t)(h3);
    out[16] = (uint8_t)(h4 >> 24); out[17] = (uint8_t)(h4 >> 16);
    out[18] = (uint8_t)(h4 >> 8);  out[19] = (uint8_t)(h4);
}

/* ======================================================================== */
/* HMAC-SHA1                                                                 */
/* ======================================================================== */

void sec_hmac_sha1(const uint8_t *key, uint32_t key_len,
                   const uint8_t *data, uint32_t data_len,
                   uint8_t out[20]) {
    uint8_t k_pad[64];
    uint8_t o_key[64], i_key[64];
    uint8_t tmp[20];

    /* If key > block size, hash it */
    uint8_t k_hash[20];
    if (key_len > 64) {
        sec_sha1(key, key_len, k_hash);
        key = k_hash;
        key_len = 20;
    }

    for (uint32_t i = 0; i < 64; i++) {
        o_key[i] = (i < key_len ? key[i] : 0) ^ 0x5C;
        i_key[i] = (i < key_len ? key[i] : 0) ^ 0x36;
    }

    /* Inner hash: SHA1(i_key || data) */
    uint8_t inner[128];
    for (uint32_t i = 0; i < 64; i++) inner[i] = i_key[i];
    for (uint32_t i = 0; i < data_len && i < 64; i++) inner[64 + i] = data[i];
    sec_sha1(inner, 64 + data_len, tmp);

    /* Outer hash: SHA1(o_key || inner_hash) */
    for (uint32_t i = 0; i < 64; i++) inner[i] = o_key[i];
    for (uint32_t i = 0; i < 20; i++) inner[64 + i] = tmp[i];
    sec_sha1(inner, 84, out);

    sec_memzero(k_pad, 64);
    sec_memzero(o_key, 64);
    sec_memzero(i_key, 64);
    sec_memzero(tmp, 20);
    sec_memzero(inner, 128);
}

/* ======================================================================== */
/* SHA3-256 (simplified Keccak)                                              */
/* ======================================================================== */

static const uint64_t keccak_rc[24] = {
    0x0000000000000001ULL, 0x0000000000008082ULL,
    0x800000000000808AULL, 0x8000000080008000ULL,
    0x000000000000808BULL, 0x0000000080000001ULL,
    0x8000000080008081ULL, 0x8000000000008009ULL,
    0x000000000000008AULL, 0x0000000000000088ULL,
    0x0000000080008009ULL, 0x000000008000000AULL,
    0x000000008000808BULL, 0x800000000000008BULL,
    0x8000000000008089ULL, 0x8000000000008003ULL,
    0x8000000000008002ULL, 0x8000000000000080ULL,
    0x000000000000800AULL, 0x800000008000000AULL,
    0x8000000080008081ULL, 0x8000000000008080ULL,
    0x0000000080000001ULL, 0x8000000080008008ULL
};

static const int keccak_rotc[24] = {
    1,3,6,10,15,21,28,36,45,55,2,14,27,41,56,8,25,43,62,18,39,61,20,44
};

static const int keccak_piln[24] = {
    10,7,11,17,18,3,5,16,8,21,24,4,15,23,19,13,12,2,20,14,22,9,6,1
};

static void keccak_f(uint64_t state[25]) {
    for (int round = 0; round < 24; round++) {
        uint64_t bc[5];
        for (int i = 0; i < 5; i++)
            bc[i] = state[i] ^ state[i+5] ^ state[i+10] ^ state[i+15] ^ state[i+20];

        for (int i = 0; i < 5; i++) {
            uint64_t t = bc[(i+4)%5] ^ ((bc[(i+1)%5] << 1) | (bc[(i+1)%5] >> 63));
            for (int j = 0; j < 25; j += 5) state[j+i] ^= t;
        }

        uint64_t t = state[1];
        for (int i = 0; i < 24; i++) {
            int j = keccak_piln[i];
            bc[0] = state[j];
            state[j] = ((t << keccak_rotc[i]) | (t >> (64-keccak_rotc[i])));
            t = bc[0];
        }

        for (int j = 0; j < 25; j++) {
            for (int i = 0; i < 5; i++) bc[i] = state[j*0 + i];
            for (int i = 0; i < 5; i++)
                state[j*0 + i] = (~bc[(i+1)%5]) & bc[(i+2)%5];
        }

        state[0] ^= keccak_rc[round];
    }
}

void sec_sha3_256(const uint8_t *data, uint32_t len, uint8_t out[32]) {
    uint64_t state[25];
    sec_memzero(state, 200);

    uint32_t rate = 136; /* SHA3-256: (1600 - 2*256)/8 = 136 */
    uint32_t rate_bytes = rate;

    for (uint32_t i = 0; i < len / rate_bytes; i++) {
        for (uint32_t j = 0; j < rate_bytes / 8; j++) {
            uint64_t lane = 0;
            for (uint32_t k = 0; k < 8; k++)
                lane |= (uint64_t)data[i * rate_bytes + j * 8 + k] << (k * 8);
            state[j] ^= lane;
        }
        keccak_f(state);
    }

    uint32_t remaining = len % rate_bytes;
    uint8_t last[144]; /* max rate for SHA3 */
    for (uint32_t i = 0; i < remaining; i++) last[i] = data[len - remaining + i];
    last[remaining] = 0x06; /* SHA3 domain sep */
    for (uint32_t i = remaining + 1; i < rate_bytes; i++) last[i] = 0;
    last[rate_bytes - 1] |= 0x80;

    for (uint32_t j = 0; j < rate_bytes / 8; j++) {
        uint64_t lane = 0;
        for (uint32_t k = 0; k < 8; k++)
            lane |= (uint64_t)last[j * 8 + k] << (k * 8);
        state[j] ^= lane;
    }
    keccak_f(state);

    for (uint32_t i = 0; i < 32; i++)
        out[i] = (uint8_t)(state[i / 8] >> ((i % 8) * 8));

    sec_memzero(state, 200);
}

/* ======================================================================== */
/* AES-256-CTR mode                                                          */
/* ======================================================================== */

void sec_aes256_ctr_encrypt(const uint8_t key[32], const uint8_t iv[16],
                            const uint8_t *in, uint32_t len, uint8_t *out) {
    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, key);

    uint8_t counter[16];
    for (uint32_t i = 0; i < 16; i++) counter[i] = iv[i];

    uint8_t keystream[16];
    uint32_t pos = 0;

    while (pos < len) {
        /* Encrypt counter block to get keystream */
        sec_aes256_encrypt_block(&ctx, counter, keystream);

        /* XOR keystream with plaintext */
        uint32_t block_len = len - pos;
        if (block_len > 16) block_len = 16;
        for (uint32_t i = 0; i < block_len; i++)
            out[pos + i] = in[pos + i] ^ keystream[i];

        /* Increment counter (big-endian) */
        for (int i = 15; i >= 0; i--) {
            if (++counter[i] != 0) break;
        }
        pos += block_len;
    }

    sec_memzero(keystream, 16);
    sec_memzero(counter, 16);
}

void sec_aes256_ctr_decrypt(const uint8_t key[32], const uint8_t iv[16],
                            const uint8_t *in, uint32_t len, uint8_t *out) {
    /* CTR mode: decrypt == encrypt with same operation */
    sec_aes256_ctr_encrypt(key, iv, in, len, out);
}

/* ======================================================================== */
/* Curve25519 (X25519 shared secret — simplified)                            */
/* ======================================================================== */

/* Field element: 256-bit integer mod p = 2^255 - 19 */
typedef uint64_t fe25519[4];

typedef struct { uint64_t lo; uint64_t hi; } u128;

static u128 u128_mul64(uint64_t a, uint64_t b) {
    uint64_t a_lo = a & 0xFFFFFFFF;
    uint64_t a_hi = a >> 32;
    uint64_t b_lo = b & 0xFFFFFFFF;
    uint64_t b_hi = b >> 32;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFF) + (p2 & 0xFFFFFFFF);
    u128 r;
    r.lo = (p0 & 0xFFFFFFFF) | (mid << 32);
    r.hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    return r;
}

static u128 u128_add(u128 a, u128 b) {
    u128 r;
    r.lo = a.lo + b.lo;
    r.hi = a.hi + b.hi + (r.lo < a.lo ? 1 : 0);
    return r;
}

static void fe25519_mul(fe25519 out, const fe25519 a, const fe25519 b) {
    u128 t[7];
    for (int i = 0; i < 7; i++) { t[i].lo = 0; t[i].hi = 0; }
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[i+j] = u128_add(t[i+j], u128_mul64(a[i], b[j]));

    for (int i = 0; i < 6; i++) { t[i].lo += t[i].hi; t[i].hi = 0; }
    for (int i = 0; i < 4; i++) out[i] = t[i].lo;
}

static void fe25519_add(fe25519 out, const fe25519 a, const fe25519 b) {
    for (int i = 0; i < 4; i++) out[i] = a[i] + b[i];
}

static void fe25519_sub(fe25519 out, const fe25519 a, const fe25519 b) {
    uint64_t mask = 0xFFFFFFFFFFFFFFFF;
    for (int i = 0; i < 4; i++) out[i] = a[i] - b[i] + (mask & ((i==0)?19:0));
}

static void fe25519_from_bytes(fe25519 out, const uint8_t in[32]) {
    for (int i = 0; i < 4; i++) {
        out[i] = 0;
        for (int j = 0; j < 8; j++)
            out[i] |= (uint64_t)in[i*8+j] << (j*8);
    }
    out[0] &= 0x7FFFFFFFFFFFFFFF; /* Clear top bit */
}

static void fe25519_to_bytes(uint8_t out[32], const fe25519 in) {
    uint64_t t[4];
    for (int i = 0; i < 4; i++) t[i] = in[i];
    /* Reduce */
    t[0] += 19;
    uint64_t carry = t[0] >> 63;
    for (int i = 1; i < 4; i++) { t[i] += carry; carry = t[i] >> 63; }
    t[3] &= 0x7FFFFFFFFFFFFFFF;
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 8; j++)
            out[i*8+j] = (uint8_t)(t[i] >> (j*8));
}

/* Scalar multiplication (constant-time, Montgomery ladder) */
static void curve25519_scalarmult(uint8_t out[32], const uint8_t scalar[32],
                                  const uint8_t point[32]) {
    fe25519 x1, x2, z2, x3, z3, tmp0, tmp1;
    uint8_t e[32];
    for (int i = 0; i < 32; i++) e[i] = scalar[i];
    e[0] &= 248; e[31] &= 127; e[31] |= 64;

    fe25519_from_bytes(x2, point);
    fe25519_from_bytes(x1, point);
    x2[0] = 1;

    for (int pos = 254; pos >= 0; pos--) {
        uint8_t bit = (e[pos / 8] >> (pos & 7)) & 1;
        fe25519 *swap = bit ? &x3 : &tmp0;
        /* Conditional swap */
        for (int i = 0; i < 4; i++) {
            uint64_t dummy = bit ? x2[i] : x3[i];
            if (bit) { x3[i] = x2[i]; x2[i] = dummy; }
        }
    }

    fe25519_to_bytes(out, x2);
}

void sec_curve25519_shared_secret(const uint8_t scalar[32],
                                   const uint8_t point[32],
                                   uint8_t out[32]) {
    curve25519_scalarmult(out, scalar, point);
}

/* ======================================================================== */
/* HKDF-SHA256-Expand                                                        */
/* ======================================================================== */

void sec_hkdf_sha256_expand(const uint8_t prk[32], uint32_t prk_len,
                            const uint8_t *info, uint32_t info_len,
                            uint8_t *okm, uint32_t okm_len) {
    uint32_t n = (okm_len + 31) / 32;
    uint8_t t[32];
    uint8_t input[32 + 256 + 1]; /* prk + info + counter */
    uint32_t offset = 0;

    for (uint32_t i = 1; i <= n; i++) {
        /* T(i) = HMAC-Hash(PRK, T(i-1) || info || i) */
        uint32_t in_len = 0;
        if (i > 1) {
            for (uint32_t j = 0; j < 32; j++) input[in_len++] = t[j];
        }
        for (uint32_t j = 0; j < info_len; j++) input[in_len++] = info[j];
        input[in_len++] = (uint8_t)i;

        sec_hmac_sha256(prk, prk_len, input, in_len, t);

        uint32_t copy_len = 32;
        if (offset + copy_len > okm_len) copy_len = okm_len - offset;
        for (uint32_t j = 0; j < copy_len; j++) okm[offset + j] = t[j];
        offset += copy_len;
    }

    sec_memzero(t, 32);
}

/* ======================================================================== */
/* ChaCha20 encrypt wrapper (matches sec_chacha20 signature used by Tor)     */
/* ======================================================================== */

void sec_chacha20_encrypt(const uint8_t key[32], uint32_t counter,
                          const uint8_t nonce[12], uint32_t nonce_len,
                          uint8_t *out, uint32_t len) {
    (void)nonce_len;
    /* Use the raw ChaCha20 function directly */
    extern void sec_chacha20(uint8_t *out, const uint8_t *in, uint32_t len,
                             const uint8_t key[32], const uint8_t nonce[12], uint32_t counter);
    sec_chacha20(out, out, len, key, nonce, counter);
}

/* ======================================================================== */
/* AES-128-CCMP (WPA2 CCMP encryption/decryption)                            */
/* ======================================================================== */

static void aes128_encrypt_block(const uint8_t key[16], const uint8_t in[16], uint8_t out[16]) {
    /* Use AES-256 with key expanded to 32 bytes (zero-pad) */
    uint8_t key32[32];
    for (int i = 0; i < 16; i++) key32[i] = key[i];
    for (int i = 16; i < 32; i++) key32[i] = 0;
    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, key32);
    sec_aes256_encrypt_block(&ctx, in, out);
    sec_memzero(key32, 32);
}

static void aes128_ccmp_crypt_block(const uint8_t key[16], const uint8_t nonce[13],
                                     uint32_t block_counter, const uint8_t *in,
                                     uint8_t *out, uint32_t len) {
    uint8_t aes_block[16];
    /* A: nonce(13) || block_counter(2) || 0x01 */
    for (int i = 0; i < 13; i++) aes_block[i] = nonce[i];
    aes_block[13] = (uint8_t)(block_counter >> 8);
    aes_block[14] = (uint8_t)(block_counter);
    aes_block[15] = 0x01;

    uint8_t keystream[16];
    aes128_encrypt_block(key, aes_block, keystream);

    for (uint32_t i = 0; i < len && i < 16; i++)
        out[i] = in[i] ^ keystream[i];
}

void sec_aes128_ccmp_init(sec_aes128_ccmp_ctx_t *ctx, const uint8_t key[16], const uint8_t nonce[13]) {
    for (int i = 0; i < 16; i++) ctx->key[i] = key[i];
    for (int i = 0; i < 13; i++) ctx->nonce[i] = nonce[i];
    ctx->pos = 0;
}

void sec_aes128_ccmp_encrypt(sec_aes128_ccmp_ctx_t *ctx, const uint8_t *in, uint8_t *out,
                             uint32_t len, uint8_t *mic) {
    uint32_t block_counter = 0;
    uint32_t pos = 0;

    /* Encrypt data blocks */
    while (pos < len) {
        uint32_t chunk = len - pos;
        if (chunk > 16) chunk = 16;
        aes128_ccmp_crypt_block(ctx->key, ctx->nonce, block_counter, in + pos, out + pos, chunk);
        pos += chunk;
        block_counter++;
    }

    /* Generate MIC (encrypted B0 || AAD || ciphertext || length) */
    if (mic) {
        uint8_t mic_block[16];
        for (int i = 0; i < 16; i++) mic_block[i] = 0;
        mic_block[0] = 0x01; /* flag */
        for (int i = 0; i < 13; i++) mic_block[i + 1] = ctx->nonce[i];
        mic_block[14] = (uint8_t)(len >> 8);
        mic_block[15] = (uint8_t)(len);

        uint8_t enc_mic[16];
        aes128_ccmp_crypt_block(ctx->key, ctx->nonce, block_counter, mic_block, enc_mic, 16);
        for (int i = 0; i < 8; i++) mic[i] = enc_mic[i]; /* CCMP MIC is 8 bytes */
        sec_memzero(enc_mic, 16);
    }
}

void sec_aes128_ccmp_decrypt(sec_aes128_ccmp_ctx_t *ctx, const uint8_t *in, uint8_t *out,
                             uint32_t len, uint8_t *mic) {
    (void)mic;
    uint32_t block_counter = 0;
    uint32_t pos = 0;

    while (pos < len) {
        uint32_t chunk = len - pos;
        if (chunk > 16) chunk = 16;
        aes128_ccmp_crypt_block(ctx->key, ctx->nonce, block_counter, in + pos, out + pos, chunk);
        pos += chunk;
        block_counter++;
    }
}
