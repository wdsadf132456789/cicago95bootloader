/**
 * Chicago-95 RFC 4 stream cipher (RC4)
 * Legacy compatibility cipher used by some older protocols
 * Implements key scheduling algorithm (KSA) and pseudo-random generation (PRGA)
 */

#include <stdint.h>

typedef struct {
    uint8_t  S[256];
    uint8_t  i;
    uint8_t  j;
} rfc4_ctx_t;

/* Initialize RC4 context with key */
void rfc4_init(rfc4_ctx_t *ctx, const uint8_t *key, uint32_t key_len) {
    /* KSA: Key Scheduling Algorithm */
    for (int i = 0; i < 256; i++)
        ctx->S[i] = (uint8_t)i;

    uint8_t j = 0;
    for (int i = 0; i < 256; i++) {
        j = j + ctx->S[i] + key[i % key_len];
        /* Swap */
        uint8_t tmp = ctx->S[i];
        ctx->S[i] = ctx->S[j];
        ctx->S[j] = tmp;
    }
    ctx->i = 0;
    ctx->j = 0;
}

/* Generate next keystream byte */
static uint8_t rfc4_next_byte(rfc4_ctx_t *ctx) {
    ctx->i++;
    ctx->j += ctx->S[ctx->i];

    uint8_t tmp = ctx->S[ctx->i];
    ctx->S[ctx->i] = ctx->S[ctx->j];
    ctx->S[ctx->j] = tmp;

    return ctx->S[(ctx->S[ctx->i] + ctx->S[ctx->j]) & 0xFF];
}

/* Encrypt/decrypt (RC4 is symmetric: same operation for both) */
void rfc4_crypt(rfc4_ctx_t *ctx, const uint8_t *in, uint8_t *out, uint32_t len) {
    for (uint32_t k = 0; k < len; k++)
        out[k] = in[k] ^ rfc4_next_byte(ctx);
}

/* Convenience: init + crypt in one call */
void rfc4_crypt_key(const uint8_t *key, uint32_t key_len,
                    const uint8_t *in, uint8_t *out, uint32_t len) {
    rfc4_ctx_t ctx;
    rfc4_init(&ctx, key, key_len);
    rfc4_crypt(&ctx, in, out, len);
}

/* Drop N bytes of keystream (for improved security) */
void rfc4_drop(rfc4_ctx_t *ctx, uint32_t n) {
    for (uint32_t i = 0; i < n; i++)
        rfc4_next_byte(ctx);
}
