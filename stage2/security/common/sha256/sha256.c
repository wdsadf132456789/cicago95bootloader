/**
 * Chicago-95 Crypto: SHA-256 and SHA-512
 * Bare-metal hash implementations
 */

#include "boot/security.h"

/* SHA-256 constants */
static const uint32_t sha256_k[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

#define ROR32(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
#define CH(x,y,z)   (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x,y,z)  (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0(x)       (ROR32(x,2) ^ ROR32(x,13) ^ ROR32(x,22))
#define EP1(x)       (ROR32(x,6) ^ ROR32(x,11) ^ ROR32(x,25))
#define SIG0(x)       (ROR32(x,7) ^ ROR32(x,18) ^ ((x) >> 3))
#define SIG1(x)       (ROR32(x,17) ^ ROR32(x,19) ^ ((x) >> 10))

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (uint32_t i = 0; i < 16; i++) {
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
               ((uint32_t)block[i*4+2] << 8) | block[i*4+3];
    }
    for (uint32_t i = 16; i < 64; i++)
        w[i] = SIG1(w[i-2]) + w[i-7] + SIG0(w[i-15]) + w[i-16];

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];

    for (uint32_t i = 0; i < 64; i++) {
        uint32_t t1 = h + EP1(e) + CH(e,f,g) + sha256_k[i] + w[i];
        uint32_t t2 = EP0(a) + MAJ(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

/* ---- SHA-256 ---- */
void sec_sha256(const uint8_t *data, uint32_t len, uint8_t out[32]) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint8_t buffer[64];
    uint32_t buf_len = 0;
    uint64_t total_len = 0;

    for (uint32_t i = 0; i < len; i++) {
        buffer[buf_len++] = data[i];
        if (buf_len == 64) {
            sha256_transform(state, buffer);
            buf_len = 0;
            total_len += 64;
        }
    }

    total_len += buf_len;

    /* Padding */
    buffer[buf_len++] = 0x80;
    if (buf_len > 56) {
        while (buf_len < 64) buffer[buf_len++] = 0;
        sha256_transform(state, buffer);
        buf_len = 0;
    }
    while (buf_len < 56) buffer[buf_len++] = 0;

    uint64_t bit_len = total_len * 8;
    for (uint32_t i = 0; i < 8; i++)
        buffer[56 + i] = (bit_len >> (56 - i * 8)) & 0xFF;

    sha256_transform(state, buffer);

    for (uint32_t i = 0; i < 8; i++)
        out[i*4] = (state[i] >> 24) & 0xFF;
    out[1] = (state[0] >> 16) & 0xFF; out[2] = (state[0] >> 8) & 0xFF; out[3] = state[0] & 0xFF;
    out[4] = (state[1] >> 24) & 0xFF; out[5] = (state[1] >> 16) & 0xFF; out[6] = (state[1] >> 8) & 0xFF; out[7] = state[1] & 0xFF;
    out[8] = (state[2] >> 24) & 0xFF; out[9] = (state[2] >> 16) & 0xFF; out[10] = (state[2] >> 8) & 0xFF; out[11] = state[2] & 0xFF;
    out[12] = (state[3] >> 24) & 0xFF; out[13] = (state[3] >> 16) & 0xFF; out[14] = (state[3] >> 8) & 0xFF; out[15] = state[3] & 0xFF;
    out[16] = (state[4] >> 24) & 0xFF; out[17] = (state[4] >> 16) & 0xFF; out[18] = (state[4] >> 8) & 0xFF; out[19] = state[4] & 0xFF;
    out[20] = (state[5] >> 24) & 0xFF; out[21] = (state[5] >> 16) & 0xFF; out[22] = (state[5] >> 8) & 0xFF; out[23] = state[5] & 0xFF;
    out[24] = (state[6] >> 24) & 0xFF; out[25] = (state[6] >> 16) & 0xFF; out[26] = (state[6] >> 8) & 0xFF; out[27] = state[6] & 0xFF;
    out[28] = (state[7] >> 24) & 0xFF; out[29] = (state[7] >> 16) & 0xFF; out[30] = (state[7] >> 8) & 0xFF; out[31] = state[7] & 0xFF;
}

/* ---- SHA-256 streaming ---- */
void sec_sha256_init(sec_sha256_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->buf_len = 0;
    ctx->total_len = 0;
}

void sec_sha256_update(sec_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buf_len++] = data[i];
        if (ctx->buf_len == 64) {
            sha256_transform(ctx->state, ctx->buffer);
            ctx->buf_len = 0;
            ctx->total_len += 64;
        }
    }
    ctx->total_len += len;
}

void sec_sha256_final(sec_sha256_ctx_t *ctx, uint8_t out[32]) {
    uint8_t buffer[64];
    uint32_t buf_len = ctx->buf_len;
    uint64_t total_len = ctx->total_len + buf_len;

    buffer[buf_len++] = 0x80;
    if (buf_len > 56) {
        while (buf_len < 64) buffer[buf_len++] = 0;
        sha256_transform(ctx->state, buffer);
        buf_len = 0;
    }
    while (buf_len < 56) buffer[buf_len++] = 0;

    uint64_t bit_len = total_len * 8;
    for (uint32_t i = 0; i < 8; i++)
        buffer[56 + i] = (bit_len >> (56 - i * 8)) & 0xFF;

    sha256_transform(ctx->state, buffer);

    for (uint32_t i = 0; i < 8; i++) {
        out[i*4] = (ctx->state[i] >> 24) & 0xFF;
        out[i*4+1] = (ctx->state[i] >> 16) & 0xFF;
        out[i*4+2] = (ctx->state[i] >> 8) & 0xFF;
        out[i*4+3] = ctx->state[i] & 0xFF;
    }
}

/* ---- SHA-512 ---- */
static const uint64_t sha512_k[80] = {
    0x428a2f98d728ae22ULL,0x7137449123ef65cdULL,0xb5c0fbcfec4d3b2fULL,0xe9b5dba58189dbbcULL,
    0x3956c25bf348b538ULL,0x59f111f1b605d019ULL,0x923f82a4af194f9bULL,0xab1c5ed5da6d8118ULL,
    0xd807aa98a3030242ULL,0x12835b0145706fbeULL,0x243185be4ee4b28cULL,0x550c7dc3d5ffb4e2ULL,
    0x72be5d74f27b896fULL,0x80deb1fe3b1696b1ULL,0x9bdc06a725c71235ULL,0xc19bf174cf692694ULL,
    0xe49b69c1ef17ad40ULL,0x0fc19dc68b8cd5b5ULL,0x240ca1cc77ac9c65ULL,0x2de92c6f592b0275ULL,
    0x4a7484aa6ea6e483ULL,0x5cb0a9dcbd41fbd4ULL,0x76f988da831153b5ULL,0x983e5152ee66dfabULL,
    0xa831c66d2db43210ULL,0xb00327c898fb213fULL,0xbf597fc7bef90728ULL,0xc6e00bf33da88fc2ULL,
    0xd5a79147930aa725ULL,0x06ca6351e003826fULL,0x142929670a0e6e70ULL,0x27b70a8546d22ffcULL,
    0x2e1b21385c26c926ULL,0x4d2c6dfc5ac42aedULL,0x53380d139d95b3dfULL,0x650a73548baf63deULL,
    0x766a0abb3c77b2a8ULL,0x81c2c92e47edaee6ULL,0x92722c851482353bULL,0xa2bfe8a14cf10364ULL,
    0xa81a664bbc423001ULL,0xc24b8b70d0f89791ULL,0xc76c51a30654be30ULL,0xd192e819d6ef5218ULL,
    0xd69906245565a910ULL,0xf40e35855771202aULL,0x106aa07032bbd1b8ULL,0x19a4c116b8d2d0c8ULL,
    0x1e376c085141ab53ULL,0x2748774cdf8eeb99ULL,0x34b0bcb5e19b48a8ULL,0x391c0cb3c5c95a63ULL,
    0x4ed8aa4ae3418acbULL,0x5b9cca4f7763e373ULL,0x682e6ff3d6b2b8a3ULL,0x748f82ee5defb2fcULL,
    0x78a5636f43172f60ULL,0x84c87814a1f0ab72ULL,0x8cc702081a6439ecULL,0x90befffa23631e28ULL,
    0xa4506cebde82bde9ULL,0xbef9a3f7b2c67915ULL,0xc67178f2e372532bULL,0xca273eceea26619cULL,
    0xd186b8c721c0c207ULL,0xeada7dd6cde0eb1eULL,0xf57d4f7fee6ed178ULL,0x06f067aa72176fbaULL,
    0x0a637dc5a2c898a6ULL,0x113f9804bef90daeULL,0x1b710b35131c471bULL,0x28db77f523047d84ULL,
    0x32caab7b40c72493ULL,0x3c9ebe0a15c9bebcULL,0x431d67c49c100d4cULL,0x4cc5d4becb3e42b6ULL,
    0x597f299cfc657e2aULL,0x5fcb6fab3ad6faecULL,0x6c44198c4a475817ULL
};

#define ROR64(x, n) (((x) >> (n)) | ((x) << (64 - (n))))
#define CH64(x,y,z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ64(x,y,z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define EP0_64(x)     (ROR64(x,28) ^ ROR64(x,34) ^ ROR64(x,39))
#define EP1_64(x)     (ROR64(x,14) ^ ROR64(x,18) ^ ROR64(x,41))
#define SIG0_64(x)    (ROR64(x,1) ^ ROR64(x,8) ^ ((x) >> 7))
#define SIG1_64(x)    (ROR64(x,19) ^ ROR64(x,61) ^ ((x) >> 6))

static void sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t w[80];
    for (uint32_t i = 0; i < 16; i++) {
        w[i] = ((uint64_t)block[i*8] << 56) | ((uint64_t)block[i*8+1] << 48) |
               ((uint64_t)block[i*8+2] << 40) | ((uint64_t)block[i*8+3] << 32) |
               ((uint64_t)block[i*8+4] << 24) | ((uint64_t)block[i*8+5] << 16) |
               ((uint64_t)block[i*8+6] << 8) | block[i*8+7];
    }
    for (uint32_t i = 16; i < 80; i++)
        w[i] = SIG1_64(w[i-2]) + w[i-7] + SIG0_64(w[i-15]) + w[i-16];

    uint64_t a=state[0],b=state[1],c=state[2],d=state[3];
    uint64_t e=state[4],f=state[5],g=state[6],h=state[7];

    for (uint32_t i = 0; i < 80; i++) {
        uint64_t t1 = h + EP1_64(e) + CH64(e,f,g) + sha512_k[i] + w[i];
        uint64_t t2 = EP0_64(a) + MAJ64(a,b,c);
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

void sec_sha512(const uint8_t *data, uint32_t len, uint8_t out[64]) {
    uint64_t state[8] = {
        0x6a09e667f3bcc908ULL, 0xbb67ae8584caa73bULL,
        0x3c6ef372fe94f82bULL, 0xa54ff53a5f1d36f1ULL,
        0x510e527fade682d1ULL, 0x9b05688c2b3e6c1fULL,
        0x1f83d9abfb41bd6bULL, 0x5be0cd19137e2179ULL
    };

    uint8_t buffer[128];
    uint32_t buf_len = 0;
    uint64_t total_len = 0;

    for (uint32_t i = 0; i < len; i++) {
        buffer[buf_len++] = data[i];
        if (buf_len == 128) {
            sha512_transform(state, buffer);
            buf_len = 0;
            total_len += 128;
        }
    }
    total_len += buf_len;

    buffer[buf_len++] = 0x80;
    if (buf_len > 112) {
        while (buf_len < 128) buffer[buf_len++] = 0;
        sha512_transform(state, buffer);
        buf_len = 0;
    }
    while (buf_len < 112) buffer[buf_len++] = 0;

    uint64_t bit_len = total_len * 8;
    for (uint32_t i = 0; i < 8; i++)
        buffer[112 + i] = (bit_len >> (56 - i * 8)) & 0xFF;

    sha512_transform(state, buffer);

    for (uint32_t i = 0; i < 8; i++) {
        out[i*8]   = (state[i] >> 56) & 0xFF;
        out[i*8+1] = (state[i] >> 48) & 0xFF;
        out[i*8+2] = (state[i] >> 40) & 0xFF;
        out[i*8+3] = (state[i] >> 32) & 0xFF;
        out[i*8+4] = (state[i] >> 24) & 0xFF;
        out[i*8+5] = (state[i] >> 16) & 0xFF;
        out[i*8+6] = (state[i] >> 8) & 0xFF;
        out[i*8+7] = state[i] & 0xFF;
    }
}

/* ---- SHA-512 Streaming API ---- */
void sec_sha512_init(sec_sha512_ctx_t *ctx) {
    ctx->state[0] = 0x6a09e667f3bcc908ULL;
    ctx->state[1] = 0xbb67ae8584caa73bULL;
    ctx->state[2] = 0x3c6ef372fe94f82bULL;
    ctx->state[3] = 0xa54ff53a5f1d36f1ULL;
    ctx->state[4] = 0x510e527fade682d1ULL;
    ctx->state[5] = 0x9b05688c2b3e6c1fULL;
    ctx->state[6] = 0x1f83d9abfb41bd6bULL;
    ctx->state[7] = 0x5be0cd19137e2179ULL;
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sec_sha512_update(sec_sha512_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        ctx->buffer[ctx->buf_len++] = data[i];
        if (ctx->buf_len == 128) {
            sha512_transform(ctx->state, ctx->buffer);
            ctx->buf_len = 0;
            ctx->total_len += 128;
        }
    }
}

void sec_sha512_final(sec_sha512_ctx_t *ctx, uint8_t out[64]) {
    ctx->total_len += ctx->buf_len;
    uint64_t bit_len = ctx->total_len * 8;

    uint32_t pad = (ctx->buf_len < 112) ? (112 - ctx->buf_len) : (128 + 112 - ctx->buf_len);
    for (uint32_t i = 0; i < pad; i++)
        ctx->buffer[ctx->buf_len + i] = (i == 0) ? 0x80 : 0x00;

    if (pad >= 16) {
        sha512_transform(ctx->state, ctx->buffer);
        for (uint32_t i = 0; i < 128; i++) ctx->buffer[i] = 0;
    }

    for (uint32_t i = 0; i < 8; i++)
        ctx->buffer[112 + i] = (bit_len >> (56 - i * 8)) & 0xFF;

    sha512_transform(ctx->state, ctx->buffer);

    for (uint32_t i = 0; i < 8; i++) {
        out[i*8]   = (ctx->state[i] >> 56) & 0xFF;
        out[i*8+1] = (ctx->state[i] >> 48) & 0xFF;
        out[i*8+2] = (ctx->state[i] >> 40) & 0xFF;
        out[i*8+3] = (ctx->state[i] >> 32) & 0xFF;
        out[i*8+4] = (ctx->state[i] >> 24) & 0xFF;
        out[i*8+5] = (ctx->state[i] >> 16) & 0xFF;
        out[i*8+6] = (ctx->state[i] >> 8) & 0xFF;
        out[i*8+7] = ctx->state[i] & 0xFF;
    }
}

/* ---- SHA-512/256 (used by ChaCha20-Poly1305 HMAC) ---- */
void sec_sha512_256(const uint8_t *data, uint32_t len, uint8_t out[32]) {
    uint8_t full[64];
    sec_sha512(data, len, full);
    for (uint32_t i = 0; i < 32; i++) out[i] = full[i];
}
