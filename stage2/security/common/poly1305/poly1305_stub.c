/**
 * Chicago-95 Poly1305 Stub
 * Poly1305 is fully implemented in stage2/security/common/chacha20/chacha20.c
 * This file provides the dedicated poly1305 module directory entry
 */

#include "boot/security.h"

/* Poly1305 forward declarations from chacha20.c */
extern void sec_poly1305_mac(const uint8_t key[32], const uint8_t *msg,
                             uint32_t msg_len, uint8_t mac[16]);
extern void sec_poly1305_key_gen(const uint8_t key[32], const uint8_t nonce[12],
                                 uint8_t out[32]);

/* Module init (no-op, implementations live in chacha20.c) */
int poly1305_module_init(void) {
    return 0;
}

/* Convenience: one-shot Poly1305 MAC */
void poly1305_compute(const uint8_t key[32], const uint8_t *msg,
                      uint32_t msg_len, uint8_t mac[16]) {
    sec_poly1305_mac(key, msg, msg_len, mac);
}

/* Key derivation for Poly1305 from ChaCha key */
void poly1305_derive_key(const uint8_t chacha_key[32],
                         const uint8_t nonce[12], uint8_t out[32]) {
    sec_poly1305_key_gen(chacha_key, nonce, out);
}
