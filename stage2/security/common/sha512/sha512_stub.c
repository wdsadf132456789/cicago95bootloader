/**
 * Chicago-95 SHA-512 Stub
 * SHA-512 is fully implemented in stage2/security/common/sha256/sha256.c
 * This file provides the dedicated sha512 module directory entry
 */

#include "boot/security.h"

/* SHA-512 forward declarations from sha256.c */
extern void sec_sha512_init(sec_sha512_ctx_t *ctx);
extern void sec_sha512_update(sec_sha512_ctx_t *ctx, const uint8_t *data, uint32_t len);
extern void sec_sha512_final(sec_sha512_ctx_t *ctx, uint8_t out[64]);

/* Module init (no-op, implementations live in sha256.c) */
int sha512_module_init(void) {
    return 0;
}

/* Convenience: single-call SHA-512 hash */
void sha512_hash(const uint8_t *data, uint32_t len, uint8_t out[64]) {
    sec_sha512_ctx_t ctx;
    sec_sha512_init(&ctx);
    sec_sha512_update(&ctx, data, len);
    sec_sha512_final(&ctx, out);
}
