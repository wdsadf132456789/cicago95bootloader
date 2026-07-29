#include "john_core.h"
#include "boot/security.h"

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} md5_ctx_t;

typedef struct {
    uint32_t state[5];
    uint64_t count;
    uint8_t buffer[64];
} sha1_ctx_t;

typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} sha256_ctx_t;

typedef struct {
    uint32_t state[4];
    uint64_t count;
    uint8_t buffer[64];
} md4_ctx_t;

static uint32_t rotl32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }
static uint32_t rotr32(uint32_t v, int n) { return (v >> n) | (v << (32 - n)); }

static const uint32_t md5_t[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee,
    0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
    0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa,
    0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed,
    0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
    0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05,
    0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039,
    0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
    0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391
};

static const int md5_s[64] = {
    7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
    5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20,
    4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
    6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21
};

static uint32_t md5_f(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
static uint32_t md5_g(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
static uint32_t md5_h(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
static uint32_t md5_i(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

static void md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a, b, c, d, f, g, temp;
    uint32_t x[16];
    int i;

    for (i = 0; i < 16; i++)
        x[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
                ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);

    a = state[0]; b = state[1]; c = state[2]; d = state[3];

    for (i = 0; i < 64; i++) {
        if (i < 16) {
            f = md5_f(b, c, d);
            g = i;
        } else if (i < 32) {
            f = md5_g(b, c, d);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = md5_h(b, c, d);
            g = (3 * i + 5) % 16;
        } else {
            f = md5_i(b, c, d);
            g = (7 * i) % 16;
        }
        temp = d;
        d = c;
        c = b;
        b = b + rotl32(a + f + md5_t[i] + x[g], md5_s[i]);
        a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md5_init(md5_ctx_t *ctx) {
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void md5_update(md5_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i, index, part_len;

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        for (i = 0; i < part_len; i++)
            ctx->buffer[index + i] = data[i];
        md5_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64)
            md5_transform(ctx->state, data + i);
        index = 0;
    } else {
        i = 0;
    }
    for (; i < len; i++)
        ctx->buffer[index + i] = data[i];
}

static void md5_final(uint8_t digest[16], md5_ctx_t *ctx) {
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    uint32_t index, pad_len, i;

    for (i = 0; i < 8; i++)
        bits[i] = (uint8_t)(ctx->count >> (i * 8));

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    md5_update(ctx, padding, pad_len);
    md5_update(ctx, bits, 8);

    for (i = 0; i < 16; i++)
        digest[i] = (uint8_t)(ctx->state[i >> 2] >> ((i & 3) * 8));
}

static void md4_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t a, b, c, d, x[16];
    int i;

    for (i = 0; i < 16; i++)
        x[i] = (uint32_t)block[i*4] | ((uint32_t)block[i*4+1] << 8) |
                ((uint32_t)block[i*4+2] << 16) | ((uint32_t)block[i*4+3] << 24);

    a = state[0]; b = state[1]; c = state[2]; d = state[3];

    for (i = 0; i < 16; i += 4) {
        a = rotl32(a + ((b & c) | (~b & d)) + x[i], 3);
        d = rotl32(d + ((a & b) | (~a & c)) + x[i+1], 7);
        c = rotl32(c + ((d & a) | (~d & b)) + x[i+2], 11);
        b = rotl32(b + ((c & d) | (~c & a)) + x[i+3], 19);
    }
    for (i = 0; i < 4; i++) {
        a = rotl32(a + ((b & c) | (b & d) | (c & d)) + x[i] + 0x5a827999, 3);
        d = rotl32(d + ((a & b) | (a & c) | (b & c)) + x[i+4] + 0x5a827999, 5);
        c = rotl32(c + ((d & a) | (d & b) | (a & b)) + x[i+8] + 0x5a827999, 9);
        b = rotl32(b + ((c & d) | (c & a) | (d & a)) + x[i+12] + 0x5a827999, 13);
    }
    for (i = 0; i < 4; i++) {
        a = rotl32(a + (b ^ c ^ d) + x[i] + 0x6ed9eba1, 3);
        d = rotl32(d + (a ^ b ^ c) + x[i+8] + 0x6ed9eba1, 9);
        c = rotl32(c + (d ^ a ^ b) + x[i+4] + 0x6ed9eba1, 11);
        b = rotl32(b + (c ^ d ^ a) + x[i+12] + 0x6ed9eba1, 15);
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
}

static void md4_init(md4_ctx_t *ctx) {
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
}

static void md4_update(md4_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i, index, part_len;

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        for (i = 0; i < part_len; i++)
            ctx->buffer[index + i] = data[i];
        md4_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64)
            md4_transform(ctx->state, data + i);
        index = 0;
    } else {
        i = 0;
    }
    for (; i < len; i++)
        ctx->buffer[index + i] = data[i];
}

static void md4_final(uint8_t digest[16], md4_ctx_t *ctx) {
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    uint32_t index, pad_len, i;

    for (i = 0; i < 8; i++)
        bits[i] = (uint8_t)(ctx->count >> (i * 8));

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    md4_update(ctx, padding, pad_len);
    md4_update(ctx, bits, 8);

    for (i = 0; i < 16; i++)
        digest[i] = (uint8_t)(ctx->state[i >> 2] >> ((i & 3) * 8));
}

static void sha1_init(sha1_ctx_t *ctx) {
    ctx->count = 0;
    ctx->state[0] = 0x67452301;
    ctx->state[1] = 0xefcdab89;
    ctx->state[2] = 0x98badcfe;
    ctx->state[3] = 0x10325476;
    ctx->state[4] = 0xc3d2e1f0;
}

static void sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t a, b, c, d, e, t, x[80];
    uint32_t temp;
    int i;

    for (i = 0; i < 16; i++)
        x[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
                ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];

    for (i = 16; i < 80; i++)
        x[i] = rotl32(x[i-3] ^ x[i-8] ^ x[i-14] ^ x[i-16], 1);

    a = state[0]; b = state[1]; c = state[2]; d = state[3]; e = state[4];

    for (i = 0; i < 80; i++) {
        if (i < 20) {
            t = (b & c) | (~b & d);
            temp = 0x5a827999;
        } else if (i < 40) {
            t = b ^ c ^ d;
            temp = 0x6ed9eba1;
        } else if (i < 60) {
            t = (b & c) | (b & d) | (c & d);
            temp = 0x8f1bbcdc;
        } else {
            t = b ^ c ^ d;
            temp = 0xca62c1d6;
        }
        temp = rotl32(a, 5) + t + e + temp + x[i];
        e = d; d = c; c = rotl32(b, 30); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c;
    state[3] += d; state[4] += e;
}

static void sha1_update(sha1_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i, index, part_len;

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        for (i = 0; i < part_len; i++)
            ctx->buffer[index + i] = data[i];
        sha1_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64)
            sha1_transform(ctx->state, data + i);
        index = 0;
    } else {
        i = 0;
    }
    for (; i < len; i++)
        ctx->buffer[index + i] = data[i];
}

static void sha1_final(uint8_t digest[20], sha1_ctx_t *ctx) {
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    uint32_t index, pad_len, i;

    for (i = 0; i < 8; i++)
        bits[i] = (uint8_t)(ctx->count >> ((7 - i) * 8));

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha1_update(ctx, padding, pad_len);
    sha1_update(ctx, bits, 8);

    for (i = 0; i < 20; i++)
        digest[i] = (uint8_t)(ctx->state[i >> 2] >> ((3 - (i & 3)) * 8));
}

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t a, b, c, d, e, f, g, h, t1, t2, w[64];
    int i;

    for (i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[i*4] << 24) | ((uint32_t)block[i*4+1] << 16) |
                ((uint32_t)block[i*4+2] << 8) | (uint32_t)block[i*4+3];
    for (i = 16; i < 64; i++)
        w[i] = (rotr32(w[i-2],17) ^ rotr32(w[i-2],19) ^ (w[i-2]>>10)) +
               w[i-7] +
               (rotr32(w[i-15],7) ^ rotr32(w[i-15],18) ^ (w[i-15]>>3)) +
               w[i-16];

    a = state[0]; b = state[1]; c = state[2]; d = state[3];
    e = state[4]; f = state[5]; g = state[6]; h = state[7];

    for (i = 0; i < 64; i++) {
        t1 = h + (rotr32(e,6) ^ rotr32(e,11) ^ rotr32(e,25)) +
             ((e & f) ^ (~e & g)) + sha256_k[i] + w[i];
        t2 = (rotr32(a,2) ^ rotr32(a,13) ^ rotr32(a,22)) +
             ((a & b) ^ (a & c) ^ (b & c));
        h = g; g = f; f = e; e = d + t1;
        d = c; c = b; b = a; a = t1 + t2;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void sha256_init_ctx(sha256_ctx_t *ctx) {
    static const uint32_t h0[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };
    int i;
    ctx->count = 0;
    for (i = 0; i < 8; i++) ctx->state[i] = h0[i];
}

static void sha256_update_ctx(sha256_ctx_t *ctx, const uint8_t *data, uint32_t len) {
    uint32_t i, index, part_len;

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    ctx->count += (uint64_t)len << 3;
    part_len = 64 - index;

    if (len >= part_len) {
        for (i = 0; i < part_len; i++)
            ctx->buffer[index + i] = data[i];
        sha256_transform(ctx->state, ctx->buffer);
        for (i = part_len; i + 63 < len; i += 64)
            sha256_transform(ctx->state, data + i);
        index = 0;
    } else {
        i = 0;
    }
    for (; i < len; i++)
        ctx->buffer[index + i] = data[i];
}

static void sha256_final_ctx(uint8_t digest[32], sha256_ctx_t *ctx) {
    static const uint8_t padding[64] = { 0x80 };
    uint8_t bits[8];
    uint32_t index, pad_len, i;

    for (i = 0; i < 8; i++)
        bits[i] = (uint8_t)(ctx->count >> ((7 - i) * 8));

    index = (uint32_t)((ctx->count >> 3) & 0x3f);
    pad_len = (index < 56) ? (56 - index) : (120 - index);
    sha256_update_ctx(ctx, padding, pad_len);
    sha256_update_ctx(ctx, bits, 8);

    for (i = 0; i < 32; i++)
        digest[i] = (uint8_t)(ctx->state[i >> 2] >> ((3 - (i & 3)) * 8));
}

static const uint32_t blowfish_pbox[18] = {
    0x243f6a88, 0x85a308d3, 0x13198a2e, 0x03707344,
    0xa4093822, 0x299f31d0, 0x082efa98, 0xec4e6c89,
    0x452821e6, 0x38d01377, 0xbe5466cf, 0x34e90c6c,
    0xcac69111, 0xc0ac29b7, 0xc97c50dd, 0x3f84d5b5,
    0xb5470917, 0x9216d5d9
};

static const uint32_t blowfish_sbox[4][256] = {
    { 0xd1310ba6, 0x98dfb5ac, 0x2ffd72db, 0xd01adfb7, 0xb8e1afed, 0x6a267e96, 0xba7c9045, 0xf12c7f99,
      0x24a19947, 0xb3916cf7, 0x0801f2e2, 0x858efc16, 0x636920d8, 0x71574e69, 0xa458fea3, 0xf4933d7e,
      0x0d95748f, 0x728eb65f, 0xf8e55f25, 0x07b6e27c, 0xd6b2e2a6, 0x103df556, 0x539356b7, 0xe78bd0c0,
      0xd798b574, 0x02030314, 0x3a152000, 0x406d0c35, 0x56d45569, 0xc0e375e0, 0xf97d5062, 0x4e51a47c,
      0xa6db5387, 0xb0c40f4d, 0x068d3a76, 0x7c21d49f, 0x8c39db19, 0xe97a40a2, 0xf0c44b7a, 0x6bcf4285,
      0xf78b5fc3, 0xe08c9b16, 0x470df8e6, 0x8a197e4a, 0x87c846d2, 0x3e76d935, 0xb96db02a, 0x29b4dbf1,
      0x6ebc5c82, 0xc2fb1365, 0xbf4f25d7, 0x3fcf5e02, 0xd4a20068, 0xd4d22c80, 0x283b770f, 0x9c0b1937,
      0xbcb874c3, 0xa6f81720, 0xe061c805, 0x37dcf540, 0xb5c6d3de, 0x748f6357, 0xc4e52ca1, 0xfb37e098,
      0x1c5ad473, 0x5b058378, 0x89f4219a, 0xf0ec6c24, 0x602c5116, 0x88f23309, 0xbfb4f884, 0xf5d12727,
      0x80a8a317, 0x2128c879, 0xf7a51c4f, 0x97b0a6e7, 0xb9283542, 0xe1475c49, 0x6cde53f2, 0xf687c534,
      0xb3b86e99, 0xb353d250, 0x32c49670, 0x8039e08f, 0xb8917aa9, 0x2b34c733, 0x2c915600, 0xc60b9806,
      0xd61b2af3, 0xe15df2a6, 0x82e45fed, 0xd3ef1065, 0x82040b0c, 0x43a4236a, 0x0ba56de0, 0x9e0a6c60,
      0x3db5580c, 0x87346037, 0x1e826e99, 0xfb378070, 0xe6e8dfc1, 0x7a8e9370, 0xc4b8d788, 0xd3619035,
      0xa7f86e69, 0x62ba4d09, 0xeae65750, 0x81f326ab, 0xf5fcf343, 0x63070c1f, 0x19436dbb, 0xe3d8e4c4,
      0xd9c4f54a, 0xb2c9c285, 0x812c3e5a, 0x4c6e8f36, 0xc1550d94, 0x30fa719c, 0x19af4256, 0xe8656a48,
      0xc0cbd042, 0x7ec15207, 0xb6d051a2, 0x358d79dc, 0x98e6df97, 0xca6bba43, 0x26dd7dde, 0x68c34c40 },
    { 0x70b897e0, 0x4807f2ff, 0xd2452d2b, 0x96b44c34, 0x127544f6, 0xe2bfcc07, 0xd85561a3, 0x813866d0,
      0x087ca42f, 0x0b1d5c31, 0x62324cd3, 0xf48983aa, 0x23c65a72, 0x10d9308a, 0xd12a618c, 0x3872c0e0,
      0x1a139455, 0x9e8679eb, 0xfb390ddc, 0xa6bcdf60, 0xc4d9c498, 0xc0c50f46, 0x713e5ad2, 0x41699d4b,
      0xb55e7176, 0x3db85714, 0xd2f15e94, 0x2f41cd08, 0xa1854734, 0x8de8d755, 0x80c85587, 0x3c2b2181,
      0x1a99b1d3, 0xed7cb310, 0x95532107, 0x57fcadf5, 0xb7c8dba4, 0x872b2082, 0xd4e82302, 0x2fb2d824,
      0xbcc095f2, 0x67cd349f, 0x24c4c215, 0xc89346d9, 0x8b6570a0, 0x110d37b6, 0x61d1e1db, 0x3a6e5088,
      0xd0b99976, 0x98e5bc13, 0x47096e43, 0x56e743d2, 0xc25d498a, 0x32704495, 0x0b26c1d8, 0x7c9d5c09,
      0xdca02e6c, 0x842da1b7, 0x96d4acbd, 0xa7e14466, 0xc4a3a7c1, 0x29e0bb49, 0x42c6d1d2, 0xf16d3387,
      0xb1953250, 0x771807e3, 0xa8d8108c, 0x2876e8b8, 0x5b8ff950, 0x6d7b3818, 0xc1231f52, 0x9504af30,
      0xf6fa2493, 0x175618db, 0xf31474f1, 0x360689d1, 0x946511f6, 0xc3f60f93, 0x3363b874, 0x7c4b8766,
      0x15d8a547, 0x26d449c8, 0xa67e24b4, 0x2fdb9e26, 0x845c6a7b, 0x54b8f0f3, 0xe7c40b16, 0x940d45f1,
      0x88d9a6c4, 0xfb3e0a21, 0x15dda64e, 0x04c00049, 0xfa76285a, 0x784c91f3, 0x0106051b, 0x4f2a3e4f,
      0x3fc98a46, 0xc2e72115, 0x29d6ca84, 0x3d5906a7, 0x65fa2c54, 0x8605336c, 0x98097e3a, 0x2c8fda23,
      0xc5f4a3aa, 0x41f58ced, 0xa4be55d1, 0xe3c29964, 0x674cde56, 0x422c8527, 0x7e94c5b5, 0xd5f4c0fc,
      0xb1a0618b, 0xc8c29939, 0x4b9c443f, 0x04d8deb1, 0x15c9bebd, 0xb1a977de, 0xd399cf2a, 0x5a18c263 },
    { 0xe49c0cd2, 0xfb377076, 0x0e3b578d, 0xb639c88b, 0x4a148d77, 0x17f39d5c, 0x9a473f48, 0x8b4bb33a,
      0x2b87d192, 0xc996a4c5, 0x15c8c25e, 0x0b956a8f, 0x0d86f06e, 0x44b9d562, 0x2e8c0f50, 0x5b4e8dd5,
      0x9c6ef397, 0x825b7182, 0x7d8b9290, 0x0a4c3f14, 0x027b3a78, 0x3a06113c, 0xd60f3dc3, 0xc5b3190a,
      0xa4d9c44f, 0x6dbd9fa5, 0x9a7b1f54, 0xc3f7c894, 0x21d32165, 0xe69c4a6f, 0x34a4973f, 0x1fc5bc5a,
      0x81536148, 0x2be5e038, 0x9b6744f8, 0x55826b54, 0xb5a3bf5a, 0x428e4803, 0x25c9937f, 0xc8462de3,
      0xa7d8c460, 0x3b894277, 0xc503472f, 0x14b4c7da, 0x063854b0, 0xc8793b9f, 0x85674c16, 0x0f3a8b94,
      0x7a5c1147, 0x4e2a3265, 0x96d8533b, 0x33f6c087, 0xc47b4b9a, 0x73403a8d, 0x2ce78f22, 0x224c9f6c,
      0x1e3b6360, 0xd53f1814, 0x8c4697a3, 0x4a4b5472, 0x8dd81b74, 0x3b5a6c5d, 0x61790673, 0x57035580,
      0x6b8e92f2, 0xd76d3799, 0x2c41a529, 0x3e5c8fa3, 0x08760167, 0xb2f53e7d, 0x80894842, 0x4930d005,
      0x0424d49c, 0xd087ab7e, 0x94c94b78, 0xc1a6ffc0, 0xc954f836, 0x70bc4856, 0x48cd4a11, 0xd4454a2f,
      0x7bb33b46, 0xb670b887, 0x040b6b76, 0x3a7c1de3, 0x6ecf4158, 0x5666787d, 0x983da381, 0xb7590a7d,
      0xabd308eb, 0x5e6d7b44, 0x8e39474f, 0xf5464865, 0xd1a5be60, 0x28e5d649, 0x058c1f8a, 0xc364804b,
      0x75a44c92, 0xc50b14fc, 0x6ab50719, 0x41760479, 0xc98e21bd, 0x6577a46e, 0x2c8c1321, 0xb75c190d,
      0xf6b332c9, 0x9f78b81c, 0x4e44b058, 0x2ff003b0, 0xe58a3a3c, 0x1a8d5611, 0x36c5b655, 0x1cd3a858,
      0xfcf89354, 0x270c0193, 0x04f2d84c, 0xd2242f6a, 0xa21d43a4, 0x7f569953, 0x0d9b0ae3, 0x99708472 },
    { 0xb55b2188, 0x569b8eb3, 0x29050b87, 0x789076bd, 0x4ca4b8d0, 0xa1b43152, 0x3e9c6d20, 0xf298afa1,
      0x5d762930, 0x850f05e3, 0x4574b658, 0xb0c260d8, 0xc1b3a5c1, 0x1f459e76, 0x6ab83de5, 0xc456abf0,
      0x4544b7f0, 0x78553238, 0xfee5d629, 0x2fc2cf4f, 0xa5e9d462, 0x4c8bf5eb, 0xb6f9c49f, 0x8c490a1e,
      0x0a9577b2, 0xd84a64fb, 0x6ed78c69, 0xa2dde930, 0x3ef62f7b, 0x3a0cf185, 0xb486152c, 0x3672c862,
      0xc468d826, 0x50a7b2b6, 0x6eb33713, 0x10d8d69e, 0xd8a6eb17, 0x19c05170, 0x1eb3d4f3, 0x59a441d6,
      0xc500d2fc, 0xb1b298e6, 0x0622ff2e, 0x33d8c5f1, 0x26203d6b, 0x43941342, 0x0bcb4e87, 0x14adc043,
      0xe6c05a06, 0x1d9b2389, 0x5fcc0597, 0x87d86ab6, 0x09e42391, 0xc60641f7, 0x68d8b37a, 0x3110d167,
      0xb4b3f9b8, 0x96a3d740, 0x0d35a594, 0x01f5a419, 0xd40f35b6, 0x8b81b499, 0xc4bfe8c2, 0x0cf7360b,
      0x3b4a5499, 0x452dda82, 0x6a48e401, 0x38c33009, 0xc4b57339, 0x237a8b7c, 0x0fb061a6, 0x0e24c4c0,
      0xd9b48a35, 0xd8687980, 0x4a4b5472, 0x98ddc7f8, 0xb0e19586, 0xe326e710, 0xf6d27180, 0x64a2b577,
      0xe507d535, 0x89792b60, 0x3e7b0a6f, 0x443f7105, 0x19934e90, 0xb98ce849, 0x3bc39ca0, 0x20cf9d25,
      0x4c4b68c4, 0xd39cd231, 0xa9179348, 0xb4a07325, 0xc03573f0, 0xa1c08dc4, 0xd3957c14, 0xb4816260,
      0x82857c8f, 0x6f4ab870, 0xc3e51626, 0x27cc2fc1, 0x964c0e84, 0x47d2b609, 0x84e67439, 0x1736a4d2,
      0x88f32957, 0x640b346a, 0x18b33a33, 0x92d45c93, 0xd5b83a32, 0xb2a03776, 0x18297842, 0x0a705843,
      0x53c2d87c, 0xe37049f6, 0x4b4b883e, 0x11a30cbd, 0x3ed37bbf, 0x32a8c1f4, 0x5556afaa, 0xb4e62554 }
};

typedef struct {
    uint32_t p[18];
    uint32_t s[4][256];
} bf_ctx_t;

static uint32_t bf_f(bf_ctx_t *ctx, uint32_t x) {
    uint8_t a = (uint8_t)(x >> 24);
    uint8_t b = (uint8_t)(x >> 16);
    uint8_t c = (uint8_t)(x >> 8);
    uint8_t d = (uint8_t)x;
    return ((ctx->s[0][a] + ctx->s[1][b]) ^ ctx->s[2][c]) + ctx->s[3][d];
}

static void bf_encipher(bf_ctx_t *ctx, uint32_t *xl, uint32_t *xr) {
    uint32_t left = *xl, right = *xr;
    int i;
    for (i = 0; i < 16; i += 2) {
        left ^= ctx->p[i];
        right ^= bf_f(ctx, left);
        right ^= ctx->p[i+1];
        left ^= bf_f(ctx, right);
    }
    left ^= ctx->p[16]; right ^= ctx->p[17];
    *xl = right; *xr = left;
}

static void bf_init(bf_ctx_t *ctx, const uint8_t *key, uint32_t keylen) {
    uint32_t data, left, right;
    int i, j, k;

    for (i = 0; i < 18; i++) ctx->p[i] = blowfish_pbox[i];
    for (i = 0; i < 4; i++)
        for (j = 0; j < 256; j++) ctx->s[i][j] = blowfish_sbox[i][j];

    j = 0;
    for (i = 0; i < 18; i++) {
        data = 0;
        for (k = 0; k < 4; k++)
            data = (data << 8) | key[j++ % keylen];
        ctx->p[i] ^= data;
    }

    left = 0; right = 0;
    for (i = 0; i < 18; i += 2) {
        bf_encipher(ctx, &left, &right);
        ctx->p[i] = left;
        ctx->p[i+1] = right;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j += 2) {
            bf_encipher(ctx, &left, &right);
            ctx->s[i][j] = left;
            ctx->s[i][j+1] = right;
        }
    }
}

static const char bcrypt_base64_chars[] =
    "./ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

static void bf_expandstate(bf_ctx_t *ctx, const uint8_t *salt, uint32_t saltlen,
                            const uint8_t *key, uint32_t keylen) {
    uint32_t data, left, right;
    int i, j, k;

    for (i = 0; i < 18; i++) {
        data = 0;
        for (k = 0; k < 4; k++)
            data = (data << 8) | salt[(i * 4 + k) % saltlen];
        ctx->p[i] ^= data;
    }
    left = 0; right = 0;
    for (i = 0; i < 18; i += 2) {
        bf_encipher(ctx, &left, &right);
        ctx->p[i] = left;
        ctx->p[i+1] = right;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j += 2) {
            bf_encipher(ctx, &left, &right);
            ctx->s[i][j] = left;
            ctx->s[i][j+1] = right;
        }
    }
    for (i = 0; i < 18; i++) {
        data = 0;
        for (k = 0; k < 4; k++)
            data = (data << 8) | key[(i * 4 + k) % keylen];
        ctx->p[i] ^= data;
    }
    left = 0; right = 0;
    for (i = 0; i < 18; i += 2) {
        bf_encipher(ctx, &left, &right);
        ctx->p[i] = left;
        ctx->p[i+1] = right;
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 256; j += 2) {
            bf_encipher(ctx, &left, &right);
            ctx->s[i][j] = left;
            ctx->s[i][j+1] = right;
        }
    }
}

static void bcrypt_encode(const uint8_t *cdata, uint32_t clen, char *out) {
    uint32_t i, j = 0;
    uint32_t c1, c2, c3;
    for (i = 0; i + 2 < clen; i += 3) {
        c1 = cdata[i];
        c2 = cdata[i+1];
        c3 = cdata[i+2];
        out[j++] = bcrypt_base64_chars[(c1 >> 2) & 0x3F];
        out[j++] = bcrypt_base64_chars[((c1 << 4) | (c2 >> 4)) & 0x3F];
        out[j++] = bcrypt_base64_chars[((c2 << 2) | (c3 >> 6)) & 0x3F];
        out[j++] = bcrypt_base64_chars[c3 & 0x3F];
    }
}

static void bcrypt_hash(const uint8_t *pass, uint32_t passlen,
                         const uint8_t *salt, uint32_t saltlen,
                         uint32_t rounds, char *out) {
    bf_ctx_t ctx;
    uint32_t i;
    uint32_t left, right;

    bf_init(&ctx, pass, passlen);
    for (i = 0; i < rounds; i++)
        bf_expandstate(&ctx, salt, saltlen, pass, passlen);

    left = 0x4f727068;
    right = 0x65616e42;
    for (i = 0; i < 64; i++)
        bf_encipher(&ctx, &left, &right);

    left = 0; right = 0;
    for (i = 0; i < 64; i++)
        bf_encipher(&ctx, &left, &right);

    {
        uint8_t ctext[24];
        ctext[0]  = (uint8_t)(left >> 24);  ctext[1]  = (uint8_t)(left >> 16);
        ctext[2]  = (uint8_t)(left >> 8);   ctext[3]  = (uint8_t)left;
        ctext[4]  = (uint8_t)(right >> 24); ctext[5]  = (uint8_t)(right >> 16);
        ctext[6]  = (uint8_t)(right >> 8);  ctext[7]  = (uint8_t)right;
        ctext[8]  = (uint8_t)(left >> 24);  ctext[9]  = (uint8_t)(left >> 16);
        ctext[10] = (uint8_t)(left >> 8);   ctext[11] = (uint8_t)left;
        ctext[12] = (uint8_t)(right >> 24); ctext[13] = (uint8_t)(right >> 16);
        ctext[14] = (uint8_t)(right >> 8);  ctext[15] = (uint8_t)right;
        ctext[16] = (uint8_t)(left >> 24);  ctext[17] = (uint8_t)(left >> 16);
        ctext[18] = (uint8_t)(left >> 8);   ctext[19] = (uint8_t)left;
        ctext[20] = (uint8_t)(right >> 24); ctext[21] = (uint8_t)(right >> 16);
        ctext[22] = (uint8_t)(right >> 8);  ctext[23] = (uint8_t)right;
        bcrypt_encode(ctext, 23, out);
        out[59] = '\0';
    }

    sec_memzero(&ctx, sizeof(ctx));
}

static const uint8_t des_ip[64] = {
    58,50,42,34,26,18,10,2, 60,52,44,36,28,20,12,4,
    62,54,46,38,30,22,14,6, 64,56,48,40,32,24,16,8,
    57,49,41,33,25,17,9,1, 59,51,43,35,27,19,11,3,
    61,53,45,37,29,21,13,5, 63,55,47,39,31,23,15,7
};

static const uint8_t des_fp[64] = {
    40,8,48,16,56,24,64,32, 39,7,47,15,55,23,63,31,
    38,6,46,14,54,22,62,30, 37,5,45,13,53,21,61,29,
    36,4,44,12,52,20,60,28, 35,3,43,11,51,19,59,27,
    34,2,42,10,50,18,58,26, 33,1,41,9,49,17,57,25
};

static const uint8_t des_e[48] = {
    32,1,2,3,4,5, 4,5,6,7,8,9, 8,9,10,11,12,13,
    12,13,14,15,16,17, 16,17,18,19,20,21, 20,21,22,23,24,25,
    24,25,26,27,28,29, 28,29,30,31,32,1
};

static const uint8_t des_p[32] = {
    16,7,20,21, 29,12,28,17, 1,15,23,26, 5,18,31,10,
    2,8,24,14, 32,27,3,9, 19,13,30,6, 22,11,4,25
};

static const uint8_t des_sbox[8][64] = {
    {14,4,13,1,2,15,11,8,3,10,6,12,5,9,0,7,
     0,15,7,4,14,2,13,1,10,6,12,11,9,5,3,8,
     4,1,14,8,13,6,2,11,15,12,9,7,3,10,5,0,
     15,12,8,2,4,9,1,7,5,11,3,14,10,0,6,13},
    {15,1,8,14,6,11,3,4,9,7,2,13,12,0,5,10,
     3,13,4,7,15,2,8,14,12,0,1,10,6,9,11,5,
     0,14,7,11,10,4,13,1,5,8,12,6,9,3,2,15,
     13,8,10,1,3,15,4,2,11,6,7,12,0,5,14,9},
    {10,0,9,14,6,3,15,5,1,13,12,7,11,4,2,8,
     13,7,0,9,3,4,6,10,2,8,5,14,12,11,15,1,
     13,6,4,9,8,15,3,0,11,1,2,12,5,10,14,7,
     1,10,13,0,6,9,8,7,4,15,14,3,11,5,2,12},
    {7,13,14,3,0,6,9,10,1,2,8,5,11,12,4,15,
     13,8,11,5,6,15,0,3,4,7,2,12,1,10,14,9,
     10,6,9,0,12,11,7,13,15,1,3,14,5,2,8,4,
     3,15,0,6,10,1,13,8,9,4,5,11,12,7,2,14},
    {2,12,4,1,7,10,11,6,8,5,3,15,13,0,14,9,
     14,11,2,12,4,7,13,1,5,0,15,10,3,9,8,6,
     4,2,1,11,10,13,7,8,15,9,12,5,6,3,0,14,
     11,8,12,7,1,14,2,13,6,15,0,9,10,4,5,3},
    {12,1,10,15,9,2,6,8,0,13,3,4,14,7,5,11,
     10,15,4,2,7,12,9,5,6,1,13,14,0,11,3,8,
     9,14,15,5,2,8,12,3,7,0,4,10,1,13,11,6,
     4,3,2,12,9,5,15,10,11,14,1,7,6,0,8,13},
    {4,11,2,14,15,0,8,13,3,12,9,7,5,10,6,1,
     13,0,11,7,4,9,1,10,14,3,5,12,2,15,8,6,
     1,4,11,13,12,3,7,14,10,15,6,8,0,5,9,2,
     6,11,13,8,1,4,10,7,9,5,0,15,14,2,3,12},
    {13,2,8,4,6,15,11,1,10,9,3,14,5,0,12,7,
     1,15,13,8,10,3,7,4,12,5,6,2,0,14,9,11,
     7,11,4,1,9,12,14,2,0,6,10,13,15,3,5,8,
     2,1,14,7,4,10,8,13,15,12,9,0,3,5,6,11}
};

static const uint8_t des_pc1[56] = {
    57,49,41,33,25,17,9, 1,58,50,42,34,26,18,
    10,2,59,51,43,35,27, 19,11,3,60,52,44,36,
    63,55,47,39,31,23,15, 7,62,54,46,38,30,22,
    14,6,61,53,45,37,29, 21,13,5,28,20,12,4
};

static const uint8_t des_pc2[48] = {
    14,17,11,24,1,5, 3,28,15,6,21,10,
    23,19,12,4,26,8, 16,7,27,20,13,2,
    41,52,31,37,47,55, 30,40,51,45,33,48,
    44,49,39,56,34,53, 46,42,50,36,29,32
};

static const int des_shifts[16] = {1,1,2,2,2,2,2,2,1,2,2,2,2,2,2,1};

static void des_setkey(uint32_t subkeys[16][48], const uint8_t key[8]) {
    uint64_t rawkey = 0;
    uint32_t C = 0, D = 0;
    uint64_t CD;
    int i, b;

    for (i = 0; i < 8; i++)
        rawkey = (rawkey << 8) | key[i];

    for (i = 0; i < 28; i++)
        C |= (uint32_t)((rawkey >> (64 - des_pc1[i])) & 1) << (27 - i);
    for (i = 0; i < 28; i++)
        D |= (uint32_t)((rawkey >> (64 - des_pc1[i+28])) & 1) << (27 - i);

    for (i = 0; i < 16; i++) {
        C = ((C << des_shifts[i]) | (C >> (28 - des_shifts[i]))) & 0x0FFFFFFF;
        D = ((D << des_shifts[i]) | (D >> (28 - des_shifts[i]))) & 0x0FFFFFFF;
        CD = ((uint64_t)C << 28) | D;
        for (b = 0; b < 48; b++) {
            if ((CD >> (56 - des_pc2[b])) & 1)
                subkeys[i][b / 8] |= (1u << (7 - (b % 8)));
        }
    }
}

static void des_block(const uint8_t *input, uint8_t *output, uint32_t subkeys[16][48]) {
    uint64_t block = 0;
    uint32_t left, right;
    int i, round;

    for (i = 0; i < 64; i++) {
        if ((input[(des_ip[i]-1)/8] >> (7 - ((des_ip[i]-1) % 8))) & 1)
            block |= (1ULL << (63 - i));
    }

    left = (uint32_t)(block >> 32);
    right = (uint32_t)block;

    for (round = 0; round < 16; round++) {
        uint32_t expanded = 0;
        uint8_t xor_val[48];
        uint8_t sbox_out[32];
        uint32_t permuted = 0;
        uint32_t temp;
        int j;

        for (i = 0; i < 48; i++)
            if ((right >> (32 - des_e[i])) & 1)
                expanded |= (1u << (31 - i));

        for (i = 0; i < 48; i++) {
            xor_val[i] = (uint8_t)(((expanded >> (31 - i)) & 1) ^
                                   ((subkeys[round][i / 8] >> (7 - (i % 8))) & 1));
        }

        for (i = 0; i < 8; i++) {
            uint8_t idx = (xor_val[i*6] << 5) | (xor_val[i*6+1] << 4) |
                          (xor_val[i*6+2] << 3) | (xor_val[i*6+3] << 2) |
                          (xor_val[i*6+4] << 1) | xor_val[i*6+5];
            uint8_t sb = des_sbox[i][idx];
            for (j = 0; j < 4; j++)
                sbox_out[i*4+j] = (uint8_t)((sb >> (3 - j)) & 1);
        }

        for (i = 0; i < 32; i++)
            if (sbox_out[des_p[i]-1])
                permuted |= (1u << (31 - i));

        temp = right;
        right = left ^ permuted;
        left = temp;
    }

    block = ((uint64_t)right << 32) | left;
    sec_memzero(output, 8);
    for (i = 0; i < 64; i++) {
        if ((block >> (64 - des_fp[i])) & 1)
            output[i / 8] |= (uint8_t)(1u << (7 - (i % 8)));
    }
}

static void des_crypt_raw(uint8_t *output, const uint8_t *key) {
    uint32_t subkeys[16][48];
    uint8_t magic_block[8];
    int i;
    uint64_t magic;

    sec_memzero(subkeys, sizeof(subkeys));
    des_setkey(subkeys, key);

    magic = 0x4B47532140232425ULL;
    for (i = 0; i < 8; i++)
        magic_block[i] = (uint8_t)(magic >> (56 - i * 8));

    sec_memzero(output, 8);
    des_block(magic_block, output, subkeys);
    sec_memzero(subkeys, sizeof(subkeys));
}

static uint32_t hex_val(char c) {
    if (c >= '0' && c <= '9') return (uint32_t)(c - '0');
    if (c >= 'a' && c <= 'f') return (uint32_t)(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return (uint32_t)(c - 'A' + 10);
    return 0;
}

static uint32_t hash_len_for_type(uint8_t type) {
    switch (type) {
    case JOHN_HASH_MD5:       return 16;
    case JOHN_HASH_SHA1:      return 20;
    case JOHN_HASH_SHA256:    return 32;
    case JOHN_HASH_SHA512:    return 64;
    case JOHN_HASH_NTLM:      return 16;
    case JOHN_HASH_DES_CRYPT: return 11;
    case JOHN_HASH_BCRYPT:    return 24;
    case JOHN_HASH_MD4:       return 16;
    case JOHN_HASH_HMAC_SHA256: return 32;
    case JOHN_HASH_LM:        return 16;
    default:                  return 0;
    }
}

void john_hash_compute(uint8_t type, const char *password, uint32_t pass_len,
                        const uint8_t *salt, uint32_t salt_len,
                        uint16_t iterations, uint8_t *out, uint8_t *out_len) {
    md5_ctx_t md5c;
    sha1_ctx_t sha1c;
    sha256_ctx_t sha256c;
    sec_sha512_ctx_t sha512c;
    md4_ctx_t md4c;
    uint32_t hlen = hash_len_for_type(type);
    (void)iterations;

    sec_memzero(out, 64);
    if (out_len) *out_len = (uint8_t)hlen;

    switch (type) {
    case JOHN_HASH_MD5:
        md5_init(&md5c);
        md5_update(&md5c, (const uint8_t *)password, pass_len);
        md5_final(out, &md5c);
        sec_memzero(&md5c, sizeof(md5c));
        break;

    case JOHN_HASH_SHA1:
        sha1_init(&sha1c);
        sha1_update(&sha1c, (const uint8_t *)password, pass_len);
        sha1_final(out, &sha1c);
        sec_memzero(&sha1c, sizeof(sha1c));
        break;

    case JOHN_HASH_SHA256:
        sha256_init_ctx(&sha256c);
        sha256_update_ctx(&sha256c, (const uint8_t *)password, pass_len);
        sha256_final_ctx(out, &sha256c);
        sec_memzero(&sha256c, sizeof(sha256c));
        break;

    case JOHN_HASH_SHA512:
        sec_sha512_init(&sha512c);
        sec_sha512_update(&sha512c, (const uint8_t *)password, pass_len);
        sec_sha512_final(&sha512c, out);
        sec_memzero(&sha512c, sizeof(sha512c));
        break;

    case JOHN_HASH_NTLM: {
        uint8_t utf16[512];
        uint32_t i, len16 = 0;
        for (i = 0; i < pass_len && len16 < 510; i++) {
            utf16[len16++] = (uint8_t)password[i];
            utf16[len16++] = 0x00;
        }
        md4_init(&md4c);
        md4_update(&md4c, utf16, len16);
        md4_final(out, &md4c);
        sec_memzero(utf16, sizeof(utf16));
        sec_memzero(&md4c, sizeof(md4c));
        break;
    }

    case JOHN_HASH_DES_CRYPT: {
        uint8_t key[8] = {0};
        uint32_t i;
        for (i = 0; i < 8 && i < pass_len; i++)
            key[i] = (uint8_t)password[i];
        des_crypt_raw(out, key);
        break;
    }

    case JOHN_HASH_BCRYPT: {
        char b64_out[64];
        uint32_t i, slen = salt_len;
        if (!salt || slen == 0) {
            salt = (const uint8_t *)"OrpheanBeholderScryDoubt";
            slen = 24;
        }
        sec_memzero(b64_out, sizeof(b64_out));
        bcrypt_hash((const uint8_t *)password, pass_len, salt, slen,
                     iterations > 0 ? iterations : 12, b64_out);
        for (i = 0; i < hlen && b64_out[i]; i++)
            out[i] = (uint8_t)b64_out[i];
        sec_memzero(b64_out, sizeof(b64_out));
        break;
    }

    case JOHN_HASH_LM: {
        uint8_t half[8];
        uint8_t lm_out[16];
        uint32_t subkeys1[16][48];
        uint32_t subkeys2[16][48];
        uint8_t key1[8], key2[8];
        uint32_t i;
        uint8_t magic_block[8];

        sec_memzero(lm_out, 16);
        sec_memzero(subkeys1, sizeof(subkeys1));
        sec_memzero(subkeys2, sizeof(subkeys2));

        sec_memzero(half, 8);
        for (i = 0; i < 7 && i < pass_len; i++)
            half[i] = (uint8_t)(password[i] & 0xDF);
        for (i = pass_len; i < 7; i++)
            half[i] = 0x20;

        sec_memzero(key1, 8);
        des_setkey(subkeys1, half);
        magic_block[0] = 0x4B; magic_block[1] = 0x47; magic_block[2] = 0x53;
        magic_block[3] = 0x21; magic_block[4] = 0x40; magic_block[5] = 0x23;
        magic_block[6] = 0x24; magic_block[7] = 0x25;
        des_block(magic_block, key1, subkeys1);

        sec_memzero(half, 8);
        for (i = 0; i < 7; i++) {
            uint32_t idx = i + 7;
            if (idx < pass_len)
                half[i] = (uint8_t)(password[idx] & 0xDF);
            else
                half[i] = 0x20;
        }

        sec_memzero(key2, 8);
        des_setkey(subkeys2, half);
        des_block(magic_block, key2, subkeys2);

        for (i = 0; i < 8; i++) {
            lm_out[i] = key1[i];
            lm_out[i + 8] = key2[i];
        }
        for (i = 0; i < 16; i++)
            out[i] = lm_out[i];

        sec_memzero(subkeys1, sizeof(subkeys1));
        sec_memzero(subkeys2, sizeof(subkeys2));
        sec_memzero(lm_out, sizeof(lm_out));
        sec_memzero(half, sizeof(half));
        sec_memzero(key1, sizeof(key1));
        sec_memzero(key2, sizeof(key2));
        break;
    }

    case JOHN_HASH_HMAC_SHA256:
        sec_hmac_sha256((const uint8_t *)password, pass_len,
                         (const uint8_t *)password, pass_len, out);
        break;

    case JOHN_HASH_MD4:
        md4_init(&md4c);
        md4_update(&md4c, (const uint8_t *)password, pass_len);
        md4_final(out, &md4c);
        sec_memzero(&md4c, sizeof(md4c));
        break;
    }
}

int john_hash_compare(const john_hash_t *target, const char *password, uint32_t pass_len) {
    uint8_t computed[64];
    uint32_t hash_len;
    uint32_t i;

    sec_memzero(computed, sizeof(computed));
    hash_len = hash_len_for_type(target->type);

    if (hash_len == 0 || hash_len > 64)
        return 0;

    john_hash_compute(target->type, password, pass_len,
                       target->salt, target->salt_len,
                       target->iterations, computed, (void*)0);

    for (i = 0; i < hash_len && i < target->hash_len; i++) {
        if (computed[i] != target->hash[i]) {
            sec_memzero(computed, sizeof(computed));
            return 0;
        }
    }

    sec_memzero(computed, sizeof(computed));
    return 1;
}

void john_hash_to_hex(const uint8_t *hash, uint8_t hash_len, char *hex, uint32_t hex_max) {
    uint32_t i;
    static const char hextab[] = "0123456789abcdef";
    uint32_t needed = (uint32_t)hash_len * 2 + 1;

    if (needed > hex_max) {
        if (hex_max > 0) hex[0] = '\0';
        return;
    }

    for (i = 0; i < hash_len; i++) {
        hex[i * 2]     = hextab[(hash[i] >> 4) & 0x0F];
        hex[i * 2 + 1] = hextab[hash[i] & 0x0F];
    }
    hex[hash_len * 2] = '\0';
}

int john_hash_from_hex(const char *hex, uint32_t hex_len, uint8_t *out, uint8_t *out_max) {
    uint32_t len = 0;
    uint32_t i = 0;
    uint32_t max_out = *out_max;

    while (i + 1 < hex_len && len < max_out) {
        out[len++] = (uint8_t)((hex_val(hex[i]) << 4) | hex_val(hex[i + 1]));
        i += 2;
    }

    *out_max = (uint8_t)len;
    return (len > 0) ? 1 : 0;
}
