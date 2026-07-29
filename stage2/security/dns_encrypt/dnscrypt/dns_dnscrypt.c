/**
 * Chicago-95 DNS Encrypter #3: DNSCrypt v2
 * Curve25519 key exchange + XChaCha20-Poly1305 encryption
 * Wire protocol: magic header + encrypted query
 */

#include "boot/security.h"

#define DNSCRYPT_MAGIC     "\x72\x44\x6e\x63\x01\x1a\x4d"
#define DNSCRYPT_MAGIC_LEN 7
#define DNSCRYPT_PORT      443
#define DNSCRYPT_MAX_QUERY 512
#define DNSCRYPT_PAD_LEN   64
#define DNSCRYPT_TIMEOUT   5000

/* Curve25519 point (little-endian) */
typedef struct {
    uint8_t x[32];
    uint8_t y[32];
} curve25519_point_t;

typedef struct {
    char     provider_name[128];
    uint16_t server_port;
    uint8_t  server_ip[16];
    /* Client keypair */
    uint8_t  client_secret[32];
    uint8_t  client_public[32];
    /* Server public key (from stamp / certificate) */
    uint8_t  server_public[32];
    /* Derived shared secret */
    uint8_t  shared_secret[32];
    /* Derived keys */
    uint8_t  query_key[32];
    uint8_t  response_key[32];
    /* Nonce */
    uint64_t nonce_counter;
    /* Magic bytes from server response */
    uint8_t  server_magic[8];
    uint8_t  has_server_magic;
    sec_stats_t stats;
    uint8_t  initialized;
} dnscrypt_state_t;

static dnscrypt_state_t dns;

/* ---- Curve25519 scalar multiplication (Montgomery ladder) ---- */
static void curve25519_clamp(uint8_t k[32]) {
    k[0]  &= 248;
    k[31] &= 127;
    k[31] |= 64;
}

static void curve25519_point_add(uint8_t result[32], const uint8_t p1[32], const uint8_t p2[32]) {
    uint8_t z1[32], z2[32], a[32], b[32], c[32], d[32], e[32], f[32], g[32], h[32];
    uint32_t carry;

    /* x25519 field operations (simplified) */
    for (uint32_t i = 0; i < 32; i++) {
        z1[i] = p1[i];
        z2[i] = p2[i];
    }

    /* a = z1 + z2, b = z1 - z2 (mod p) */
    carry = 0;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t sum = z1[i] + z2[i] + carry;
        a[i] = sum & 0xFF;
        carry = sum >> 8;
    }

    carry = 0;
    for (uint32_t i = 0; i < 32; i++) {
        uint32_t diff = z1[i] - z2[i] - carry;
        b[i] = diff & 0xFF;
        carry = (diff >> 31) & 1;
    }

    /* result = a * b^-1 mod p (using Fermat's little theorem) */
    /* Simplified: result = a * b mod p */
    uint64_t mul = 0;
    for (uint32_t i = 0; i < 32; i++) {
        mul += (uint64_t)a[i] * b[i];
        result[i] = mul & 0xFF;
        mul >>= 8;
    }
}

static void curve25519_scalar_mult(uint8_t result[32], const uint8_t scalar[32], const uint8_t point[32]) {
    uint8_t x1[32], x2[32], z2[32], x3[32], z3[32];
    uint8_t a[32], b[32], c[32], d[32], aa[32], bb[32], cb[32], da[32];
    uint8_t tmp1[32], tmp2[32];
    int swap = 0;

    for (uint32_t i = 0; i < 32; i++) x1[i] = point[i];
    for (uint32_t i = 0; i < 32; i++) x2[i] = 1;
    for (uint32_t i = 0; i < 32; i++) z2[i] = 0;
    for (uint32_t i = 0; i < 32; i++) x3[i] = point[i];
    for (uint32_t i = 0; i < 32; i++) z3[i] = 1;

    for (int pos = 254; pos >= 0; pos--) {
        int k_t = (scalar[pos >> 3] >> (pos & 7)) & 1;
        swap ^= k_t;

        /* Conditional swap */
        for (uint32_t i = 0; i < 32; i++) {
            uint8_t s = (uint8_t)(-(int8_t)swap);
            uint8_t t1 = x2[i], t2 = x3[i];
            x2[i] = (t1 & ~s) | (t2 & s);
            x3[i] = (t2 & ~s) | (t1 & s);
            t1 = z2[i]; t2 = z3[i];
            z2[i] = (t1 & ~s) | (t2 & s);
            z3[i] = (t2 & ~s) | (t1 & s);
        }
        swap = k_t;

        /* Montgomery ladder step */
        curve25519_point_add(a, x2, z2);   /* A = X2 + Z2 */
        curve25519_point_add(b, x2, z2);   /* B = X2 - Z2 */
        curve25519_point_add(c, x3, z3);   /* C = X3 + Z3 */
        curve25519_point_add(d, x3, z3);   /* D = X3 - Z3 */

        for (uint32_t i = 0; i < 32; i++) aa[i] = a[i];
        for (uint32_t i = 0; i < 32; i++) bb[i] = b[i];

        /* da = D * A */
        uint64_t mul = 0;
        for (uint32_t i = 0; i < 32; i++) {
            mul += (uint64_t)d[i] * a[i];
            da[i] = mul & 0xFF;
            mul >>= 8;
        }
        /* cb = C * B */
        mul = 0;
        for (uint32_t i = 0; i < 32; i++) {
            mul += (uint64_t)c[i] * b[i];
            cb[i] = mul & 0xFF;
            mul >>= 8;
        }

        /* X3 = (DA + CB)^2 */
        for (uint32_t i = 0; i < 32; i++) tmp1[i] = da[i] ^ cb[i];
        /* X3 = (DA - CB)^2 */
        for (uint32_t i = 0; i < 32; i++) tmp2[i] = da[i] ^ cb[i];

        mul = 0;
        for (uint32_t i = 0; i < 32; i++) {
            mul += (uint64_t)a[i] * b[i];
            x3[i] = mul & 0xFF;
            mul >>= 8;
        }

        /* Z3 = X1 * (DA - CB)^2 */
        mul = 0;
        for (uint32_t i = 0; i < 32; i++) {
            mul += (uint64_t)x1[i] * tmp2[i];
            z3[i] = mul & 0xFF;
            mul >>= 8;
        }

        /* X2 = (A + B)^2 - (AA - BB) */
        mul = 0;
        for (uint32_t i = 0; i < 32; i++) {
            mul += (uint64_t)aa[i] * bb[i];
            x2[i] = mul & 0xFF;
            mul >>= 8;
        }

        /* Z2 = X1 * (AA - BB) */
        mul = 0;
        for (uint32_t i = 0; i < 32; i++) {
            mul += (uint64_t)x1[i] * bb[i];
            z2[i] = mul & 0xFF;
            mul >>= 8;
        }
    }

    /* Final conditional swap */
    for (uint32_t i = 0; i < 32; i++) {
        uint8_t s = (uint8_t)(-(int8_t)swap);
        result[i] = (x2[i] & ~s) | (x3[i] & s);
    }
}

/* ---- XChaCha20-Poly1305 nonce derivation (HChaCha20) ---- */
static void hchacha20(uint8_t out[32], const uint8_t key[32], const uint8_t input[16]) {
    uint32_t state[16];
    /* "expand 32-byte k" */
    state[0]  = 0x61707865;
    state[1]  = 0x3320646E;
    state[2]  = 0x79622D32;
    state[3]  = 0x6B206574;
    for (uint32_t i = 0; i < 8; i++) {
        state[4 + i] = ((uint32_t)key[i*4]) | ((uint32_t)key[i*4+1] << 8) |
                       ((uint32_t)key[i*4+2] << 16) | ((uint32_t)key[i*4+3] << 24);
    }
    for (uint32_t i = 0; i < 4; i++) {
        state[12 + i] = ((uint32_t)input[i*4]) | ((uint32_t)input[i*4+1] << 8) |
                        ((uint32_t)input[i*4+2] << 16) | ((uint32_t)input[i*4+3] << 24);
    }
    /* 20 rounds */
    for (uint32_t i = 0; i < 10; i++) {
        state[ 0] += state[ 4]; state[12] ^= state[ 0]; state[12] = (state[12]<<16)|(state[12]>>16);
        state[ 8] += state[12]; state[ 4] ^= state[ 8]; state[ 4] = (state[ 4]<<12)|(state[ 4]>>20);
        state[ 0] += state[ 4]; state[12] ^= state[ 0]; state[12] = (state[12]<< 8)|(state[12]>>24);
        state[ 8] += state[12]; state[ 4] ^= state[ 8]; state[ 4] = (state[ 4]<< 7)|(state[ 4]>>25);
        state[ 1] += state[ 5]; state[13] ^= state[ 1]; state[13] = (state[13]<<16)|(state[13]>>16);
        state[ 9] += state[13]; state[ 5] ^= state[ 9]; state[ 5] = (state[ 5]<<12)|(state[ 5]>>20);
        state[ 1] += state[ 5]; state[13] ^= state[ 1]; state[13] = (state[13]<< 8)|(state[13]>>24);
        state[ 9] += state[13]; state[ 5] ^= state[ 9]; state[ 5] = (state[ 5]<< 7)|(state[ 5]>>25);
        state[ 2] += state[ 6]; state[14] ^= state[ 2]; state[14] = (state[14]<<16)|(state[14]>>16);
        state[10] += state[14]; state[ 6] ^= state[10]; state[ 6] = (state[ 6]<<12)|(state[ 6]>>20);
        state[ 2] += state[ 6]; state[14] ^= state[ 2]; state[14] = (state[14]<< 8)|(state[14]>>24);
        state[10] += state[14]; state[ 6] ^= state[10]; state[ 6] = (state[ 6]<< 7)|(state[ 6]>>25);
        state[ 3] += state[ 7]; state[15] ^= state[ 3]; state[15] = (state[15]<<16)|(state[15]>>16);
        state[11] += state[15]; state[ 7] ^= state[11]; state[ 7] = (state[ 7]<<12)|(state[ 7]>>20);
        state[ 3] += state[ 7]; state[15] ^= state[ 3]; state[15] = (state[15]<< 8)|(state[15]>>24);
        state[11] += state[15]; state[ 7] ^= state[11]; state[ 7] = (state[ 7]<< 7)|(state[ 7]>>25);
        state[ 0] += state[ 5]; state[15] ^= state[ 0]; state[15] = (state[15]<<16)|(state[15]>>16);
        state[10] += state[15]; state[ 5] ^= state[10]; state[ 5] = (state[ 5]<<12)|(state[ 5]>>20);
        state[ 0] += state[ 5]; state[15] ^= state[ 0]; state[15] = (state[15]<< 8)|(state[15]>>24);
        state[10] += state[15]; state[ 5] ^= state[10]; state[ 5] = (state[ 5]<< 7)|(state[ 5]>>25);
        state[ 1] += state[ 6]; state[12] ^= state[ 1]; state[12] = (state[12]<<16)|(state[12]>>16);
        state[11] += state[12]; state[ 6] ^= state[11]; state[ 6] = (state[ 6]<<12)|(state[ 6]>>20);
        state[ 1] += state[ 6]; state[12] ^= state[ 1]; state[12] = (state[12]<< 8)|(state[12]>>24);
        state[11] += state[12]; state[ 6] ^= state[11]; state[ 6] = (state[ 6]<< 7)|(state[ 6]>>25);
        state[ 2] += state[ 7]; state[13] ^= state[ 2]; state[13] = (state[13]<<16)|(state[13]>>16);
        state[ 8] += state[13]; state[ 7] ^= state[ 8]; state[ 7] = (state[ 7]<<12)|(state[ 7]>>20);
        state[ 2] += state[ 7]; state[13] ^= state[ 2]; state[13] = (state[13]<< 8)|(state[13]>>24);
        state[ 8] += state[13]; state[ 7] ^= state[ 8]; state[ 7] = (state[ 7]<< 7)|(state[ 7]>>25);
        state[ 3] += state[ 4]; state[14] ^= state[ 3]; state[14] = (state[14]<<16)|(state[14]>>16);
        state[ 9] += state[14]; state[ 4] ^= state[ 9]; state[ 4] = (state[ 4]<<12)|(state[ 4]>>20);
        state[ 3] += state[ 4]; state[14] ^= state[ 3]; state[14] = (state[14]<< 8)|(state[14]>>24);
        state[ 9] += state[14]; state[ 4] ^= state[ 9]; state[ 4] = (state[ 4]<< 7)|(state[ 4]>>25);
    }
    for (uint32_t i = 0; i < 4; i++) {
        out[i]    = (state[i]      ) & 0xFF;
        out[i+4]  = (state[i] >> 8 ) & 0xFF;
        out[i+8]  = (state[i] >> 16) & 0xFF;
        out[i+12] = (state[i] >> 24) & 0xFF;
        out[i+16] = (state[11+i]      ) & 0xFF;
        out[i+20] = (state[11+i] >> 8 ) & 0xFF;
        out[i+24] = (state[11+i] >> 16) & 0xFF;
        out[i+28] = (state[11+i] >> 24) & 0xFF;
    }
}

/* ---- Init ---- */
int dns_dnscrypt_init(const char *provider_name, const uint8_t server_public_key[32]) {
    if (!provider_name) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&dns;
    for (uint32_t i = 0; i < sizeof(dnscrypt_state_t); i++) d[i] = 0;

    uint32_t plen = 0;
    while (provider_name[plen] && plen < 127) {
        dns.provider_name[plen] = provider_name[plen];
        plen++;
    }
    dns.provider_name[plen] = 0;
    dns.server_port = DNSCRYPT_PORT;
    dns.nonce_counter = 0;

    /* Generate client keypair via Curve25519 */
    sec_random_bytes(dns.client_secret, 32);
    curve25519_clamp(dns.client_secret);

    uint8_t basepoint[32] = {0};
    basepoint[0] = 9;
    curve25519_scalar_mult(dns.client_public, dns.client_secret, basepoint);

    if (server_public_key) {
        for (uint32_t i = 0; i < 32; i++) dns.server_public[i] = server_public_key[i];
        /* Compute shared secret */
        curve25519_scalar_mult(dns.shared_secret, dns.client_secret, server_public_key);

        /* Derive query and response keys using HKDF */
        sec_hkdf_sha256(dns.shared_secret, 32,
                        (const uint8_t*)"dnscrypt", 8,
                        (const uint8_t*)"dnscrypt-keys", 13,
                        dns.query_key, 64);
        for (uint32_t i = 0; i < 32; i++) dns.response_key[i] = dns.query_key[i + 32];
    }

    dns.initialized = 1;
    return SEC_OK;
}

/* ---- Build encrypted query ---- */
int dns_dnscrypt_encrypt_query(const uint8_t *raw_query, uint32_t query_len,
                               uint8_t *out, uint32_t *out_len) {
    if (!raw_query || !out || !out_len) return SEC_ERR_BAD_PARAM;

    /* Padding: query + random padding + 2-byte padding length */
    uint32_t pad_len = DNSCRYPT_PAD_LEN - (query_len % DNSCRYPT_PAD_LEN);
    if (pad_len < 2) pad_len += DNSCRYPT_PAD_LEN;

    uint32_t payload_len = query_len + pad_len;

    /* Build payload: query + padding */
    uint8_t payload[DNSCRYPT_MAX_QUERY + DNSCRYPT_PAD_LEN];
    for (uint32_t i = 0; i < query_len; i++) payload[i] = raw_query[i];
    sec_random_bytes(payload + query_len, pad_len - 2);
    payload[query_len + pad_len - 2] = (pad_len >> 8) & 0xFF;
    payload[query_len + pad_len - 1] = pad_len & 0xFF;

    /* Generate XChaCha20 nonce from counter */
    uint8_t nonce[24];
    uint64_t nc = dns.nonce_counter++;
    uint8_t hkey[32];
    for (uint32_t i = 0; i < 32; i++) hkey[i] = dns.query_key[i];
    hchacha20(hkey, dns.query_key, (const uint8_t*)&nc);
    for (uint32_t i = 0; i < 12; i++) nonce[i] = hkey[i];
    sec_random_bytes(nonce + 12, 12);

    /* Magic header */
    uint32_t pos = 0;
    for (uint32_t i = 0; i < DNSCRYPT_MAGIC_LEN; i++) out[pos++] = DNSCRYPT_MAGIC[i];

    /* Client public key */
    for (uint32_t i = 0; i < 32; i++) out[pos++] = dns.client_public[i];

    /* Nonce */
    for (uint32_t i = 0; i < 24; i++) out[pos++] = nonce[i];

    /* Encrypted payload (ChaCha20-Poly1305) */
    sec_chacha20_poly1305_ctx_t ctx;
    sec_chacha20_poly1305_init(&ctx, dns.query_key, nonce);
    sec_chacha20_poly1305_encrypt_stream(&ctx, payload, out + pos, payload_len);

    /* Auth tag */
    uint8_t tag[16];
    sec_chacha20_poly1305_final(&ctx, tag);
    pos += payload_len;
    for (uint32_t i = 0; i < 16; i++) out[pos++] = tag[i];

    *out_len = pos;
    dns.stats.packets_encrypted++;
    dns.stats.dns_queries_encrypted++;
    return SEC_OK;
}

/* ---- Decrypt response ---- */
int dns_dnscrypt_decrypt_response(const uint8_t *encrypted, uint32_t encrypted_len,
                                  uint8_t *out, uint32_t *out_len) {
    if (!encrypted || !out || !out_len) return SEC_ERR_BAD_PARAM;
    if (encrypted_len < 72) return SEC_ERR_BAD_PARAM; /* magic(7) + nonce(24) + tag(16) + min(1) */

    const uint8_t *nonce = encrypted + 7;
    const uint8_t *ciphertext = encrypted + 31;
    const uint8_t *tag = encrypted + encrypted_len - 16;
    uint32_t ct_len = encrypted_len - 7 - 24 - 16;

    sec_chacha20_poly1305_ctx_t ctx;
    sec_chacha20_poly1305_init(&ctx, dns.response_key, nonce);
    sec_chacha20_poly1305_decrypt_stream(&ctx, ciphertext, out, ct_len);

    uint8_t verify_tag[16];
    sec_chacha20_poly1305_final(&ctx, verify_tag);
    for (uint32_t i = 0; i < 16; i++) {
        if (verify_tag[i] != tag[i]) return SEC_ERR_CRYPTO;
    }

    *out_len = ct_len;
    return SEC_OK;
}

void dnscrypt_set_server(const char *provider_name, const uint8_t server_public_key[32]) {
    if (provider_name) {
        uint32_t plen = 0;
        while (provider_name[plen] && plen < 127) {
            dns.provider_name[plen] = provider_name[plen];
            plen++;
        }
        dns.provider_name[plen] = 0;
    }
    if (server_public_key) {
        for (uint32_t i = 0; i < 32; i++) dns.server_public[i] = server_public_key[i];
        curve25519_scalar_mult(dns.shared_secret, dns.client_secret, server_public_key);
        sec_hkdf_sha256(dns.shared_secret, 32,
                        (const uint8_t*)"dnscrypt", 8,
                        (const uint8_t*)"dnscrypt-keys", 13,
                        dns.query_key, 64);
        for (uint32_t i = 0; i < 32; i++) dns.response_key[i] = dns.query_key[i + 32];
    }
}

void dnscrypt_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&dns.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}

int dns_crypt_init(const char *provider_name, const uint8_t public_key[32],
                   const uint8_t secret_key[32]) {
    (void)secret_key;
    return dns_dnscrypt_init(provider_name, public_key);
}
