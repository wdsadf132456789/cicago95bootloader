#include <stdint.h>
#include <string.h>
#include "security/pgp.h"
#include "boot/security.h"
#include "boot/ring0_init.h"

static void pgp_memset(void *dst, int val, uint32_t len) {
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t i = 0; i < len; i++) d[i] = (uint8_t)val;
}

pgp_key_t pgp_pubkeys[PGP_MAX_KEYS];
uint32_t  pgp_pubkey_count = 0;
pgp_key_t pgp_seckeys[PGP_MAX_KEYS];
uint32_t  pgp_seckey_count = 0;

static const uint8_t SHA256_DER_PREFIX[] = {
    0x30, 0x31, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, 0x05,
    0x00, 0x04, 0x20
};
#define SHA256_DER_LEN 19

static const uint8_t SHA512_DER_PREFIX[] = {
    0x30, 0x51, 0x30, 0x0D, 0x06, 0x09, 0x60, 0x86,
    0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, 0x05,
    0x00, 0x04, 0x40
};
#define SHA512_DER_LEN 19

static const uint8_t SHA1_DER_PREFIX[] = {
    0x30, 0x21, 0x30, 0x09, 0x06, 0x05, 0x2B, 0x0E,
    0x03, 0x02, 0x1A, 0x05, 0x00, 0x04, 0x14
};
#define SHA1_DER_LEN 15

void pgp_init(void) {
    pgp_pubkey_count = 0;
    pgp_seckey_count = 0;
    for (uint32_t i = 0; i < PGP_MAX_KEYS; i++) {
        pgp_memset(&pgp_pubkeys[i], 0, sizeof(pgp_key_t));
        pgp_memset(&pgp_seckeys[i], 0, sizeof(pgp_key_t));
    }
}

/* ---- RSA operations ---- */

int pgp_rsa_keygen(pgp_rsa_key_t *key, uint32_t bits) {
    if (bits != 2048) return PGP_ERR_PARAM;
    pgp_mpi_t one, p1, q1, phi, e, gcd_val;
    pgp_mpi_set_u32(&one, 1);
    pgp_mpi_set_u32(&e, 65537);
    pgp_memset(key, 0, sizeof(pgp_rsa_key_t));
    for (int attempt = 0; attempt < 100; attempt++) {
        pgp_mpi_rand(&key->p, 1024);
        key->p.limbs[key->p.count - 1] |= (1U << 31);
        key->p.limbs[0] |= 1;
        if (!pgp_mpi_is_prime(&key->p, 8)) continue;
        pgp_mpi_rand(&key->q, 1024);
        key->q.limbs[key->q.count - 1] |= (1U << 31);
        key->q.limbs[0] |= 1;
        if (!pgp_mpi_is_prime(&key->q, 8)) continue;
        if (pgp_mpi_compare(&key->p, &key->q) == 0) continue;
        pgp_mpi_mul(&key->n, &key->p, &key->q);
        if (key->n.count == 0) continue;
        pgp_mpi_sub(&p1, &key->p, &one);
        pgp_mpi_sub(&q1, &key->q, &one);
        pgp_mpi_mul(&phi, &p1, &q1);
        pgp_mpi_gcd(&gcd_val, &e, &phi);
        if (!pgp_mpi_is_one(&gcd_val)) continue;
        if (pgp_mpi_inv_mod(&key->d, &e, &phi) != 0) continue;
        key->e = e;
        break;
    }
    if (pgp_mpi_is_zero(&key->d)) return PGP_ERR_KEY;
    pgp_mpi_sub(&p1, &key->p, &one);
    pgp_mpi_mod(&key->dp, &key->d, &p1);
    pgp_mpi_sub(&q1, &key->q, &one);
    pgp_mpi_mod(&key->dq, &key->d, &q1);
    pgp_mpi_inv_mod(&key->qinv, &key->q, &key->p);
    return PGP_OK;
}

void pgp_rsa_public(pgp_rsa_key_t *pub, const pgp_rsa_key_t *key) {
    pgp_memset(pub, 0, sizeof(pgp_rsa_key_t));
    pgp_mpi_t tmp_n, tmp_e;
    for (uint32_t i = 0; i < key->n.count; i++) tmp_n.limbs[i] = key->n.limbs[i];
    tmp_n.count = key->n.count;
    for (uint32_t i = 0; i < key->e.count; i++) tmp_e.limbs[i] = key->e.limbs[i];
    tmp_e.count = key->e.count;
    for (uint32_t i = 0; i < tmp_n.count; i++) pub->n.limbs[i] = tmp_n.limbs[i];
    pub->n.count = tmp_n.count;
    for (uint32_t i = 0; i < tmp_e.count; i++) pub->e.limbs[i] = tmp_e.limbs[i];
    pub->e.count = tmp_e.count;
}

static int rsa_pkcs1_sign_hash(const pgp_rsa_key_t *key, const uint8_t *hash, uint32_t hash_len,
                               const uint8_t *der_prefix, uint32_t der_len, pgp_mpi_t *sig) {
    uint8_t em[PGP_RSA_BYTES];
    uint32_t em_len = PGP_RSA_BYTES;
    for (uint32_t i = 0; i < em_len; i++) em[i] = 0;
    em[1] = 0x01;
    uint32_t pad_len = em_len - 3 - der_len - hash_len;
    for (uint32_t i = 2; i < 2 + pad_len; i++) em[i] = 0xFF;
    em[2 + pad_len] = 0x00;
    for (uint32_t i = 0; i < der_len; i++) em[3 + pad_len + i] = der_prefix[i];
    for (uint32_t i = 0; i < hash_len; i++) em[3 + pad_len + der_len + i] = hash[i];
    pgp_mpi_t m;
    for (uint32_t i = 0; i < PGP_MPI_MAX_LIMBS; i++) m.limbs[i] = 0;
    m.count = 0;
    for (uint32_t i = 0; i < em_len; i++) {
        uint32_t limb = (em_len - 1 - i) / 4;
        if (limb >= PGP_MPI_MAX_LIMBS) break;
        m.limbs[limb] |= ((uint32_t)em[i]) << ((3 - (i % 4)) * 8);
    }
    m.count = (em_len + 3) / 4;
    pgp_mpi_pow_mod(sig, &m, &key->d, &key->n);
    return PGP_OK;
}

static int rsa_pkcs1_verify_hash(const pgp_rsa_key_t *key, const uint8_t *hash, uint32_t hash_len,
                                 const uint8_t *der_prefix, uint32_t der_len, const pgp_mpi_t *sig) {
    uint8_t em[PGP_RSA_BYTES];
    uint32_t em_len = PGP_RSA_BYTES;
    pgp_mpi_t m;
    pgp_mpi_pow_mod(&m, sig, &key->e, &key->n);
    pgp_memset(em, 0, em_len);
    for (uint32_t i = 0; i < em_len; i++) {
        uint32_t limb = (em_len - 1 - i) / 4;
        if (limb < m.count)
            em[i] = (m.limbs[limb] >> ((3 - (i % 4)) * 8)) & 0xFF;
    }
    if (em[0] != 0x00 || em[1] != 0x01) return PGP_ERR_SIG;
    uint32_t pos = 2;
    while (pos < em_len && em[pos] == 0xFF) pos++;
    if (pos >= em_len || em[pos] != 0x00) return PGP_ERR_SIG;
    pos++;
    if (pos + der_len + hash_len > em_len) return PGP_ERR_SIG;
    for (uint32_t i = 0; i < der_len; i++) if (em[pos + i] != der_prefix[i]) return PGP_ERR_SIG;
    pos += der_len;
    for (uint32_t i = 0; i < hash_len; i++) if (em[pos + i] != hash[i]) return PGP_ERR_SIG;
    return PGP_OK;
}

int pgp_rsa_sign(const pgp_rsa_key_t *key, const uint8_t *hash, uint32_t hash_len, pgp_mpi_t *sig) {
    const uint8_t *der; uint32_t der_len;
    if (hash_len == 32) { der = SHA256_DER_PREFIX; der_len = SHA256_DER_LEN; }
    else if (hash_len == 64) { der = SHA512_DER_PREFIX; der_len = SHA512_DER_LEN; }
    else if (hash_len == 20) { der = SHA1_DER_PREFIX; der_len = SHA1_DER_LEN; }
    else return PGP_ERR_PARAM;
    return rsa_pkcs1_sign_hash(key, hash, hash_len, der, der_len, sig);
}

int pgp_rsa_verify(const pgp_rsa_key_t *key, const uint8_t *hash, uint32_t hash_len, const pgp_mpi_t *sig) {
    const uint8_t *der; uint32_t der_len;
    if (hash_len == 32) { der = SHA256_DER_PREFIX; der_len = SHA256_DER_LEN; }
    else if (hash_len == 64) { der = SHA512_DER_PREFIX; der_len = SHA512_DER_LEN; }
    else if (hash_len == 20) { der = SHA1_DER_PREFIX; der_len = SHA1_DER_LEN; }
    else return PGP_ERR_PARAM;
    return rsa_pkcs1_verify_hash(key, hash, hash_len, der, der_len, sig);
}

int pgp_rsa_encrypt(const pgp_rsa_key_t *key, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len) {
    if (in_len > PGP_RSA_BYTES - 11) return PGP_ERR_PARAM;
    uint8_t em[PGP_RSA_BYTES];
    uint32_t em_len = PGP_RSA_BYTES;
    em[0] = 0x00; em[1] = 0x02;
    for (uint32_t i = 2; i < em_len - in_len - 1; i++) {
        uint8_t r; do { sec_random_bytes(&r, 1); } while (r == 0);
        em[i] = r;
    }
    em[em_len - in_len - 1] = 0x00;
    for (uint32_t i = 0; i < in_len; i++) em[em_len - in_len + i] = in[i];
    pgp_mpi_t m;
    pgp_mpi_zero(&m);
    for (uint32_t i = 0; i < em_len; i++) {
        uint32_t limb = (em_len - 1 - i) / 4;
        if (limb >= PGP_MPI_MAX_LIMBS) break;
        m.limbs[limb] |= ((uint32_t)em[i]) << ((3 - (i % 4)) * 8);
    }
    m.count = (em_len + 3) / 4;
    pgp_mpi_t c;
    pgp_mpi_pow_mod(&c, &m, &key->e, &key->n);
    pgp_mpi_to_bytes(&c, out, out_len);
    return PGP_OK;
}

int pgp_rsa_decrypt(const pgp_rsa_key_t *key, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len) {
    pgp_mpi_t c;
    pgp_mpi_from_bytes(&c, in, in_len);
    pgp_mpi_t m;
    pgp_mpi_pow_mod(&m, &c, &key->d, &key->n);
    uint8_t em[PGP_RSA_BYTES];
    uint32_t em_len = PGP_RSA_BYTES;
    pgp_memset(em, 0, em_len);
    for (uint32_t i = 0; i < em_len; i++) {
        uint32_t limb = (em_len - 1 - i) / 4;
        if (limb < m.count)
            em[i] = (m.limbs[limb] >> ((3 - (i % 4)) * 8)) & 0xFF;
    }
    if (em[0] != 0x00 || em[1] != 0x02) return PGP_ERR_DECRYPT;
    uint32_t pos = 2;
    while (pos < em_len && em[pos] != 0x00) pos++;
    if (pos >= em_len - 1) return PGP_ERR_DECRYPT;
    pos++;
    uint32_t data_len = em_len - pos;
    if (data_len > *out_len) return PGP_ERR_DECRYPT;
    for (uint32_t i = 0; i < data_len; i++) out[i] = em[pos + i];
    *out_len = data_len;
    return PGP_OK;
}

/* ---- PGP key management ---- */

static void compute_fingerprint(const pgp_key_t *key, uint8_t *fp) {
    sec_sha256_ctx_t ctx;
    sec_sha256_init(&ctx);
    uint8_t pkt[4] = { 0x99, (uint8_t)((PGP_RSA_BYTES + 6) >> 8), (uint8_t)(PGP_RSA_BYTES + 6), 0 };
    pkt[0] = 0x99;
    uint32_t mpi_len_n, mpi_len_e;
    uint8_t n_buf[PGP_RSA_BYTES + 2];
    pgp_mpi_to_bytes(&key->rsa.n, n_buf, &mpi_len_n);
    uint8_t e_buf[8];
    pgp_mpi_to_bytes(&key->rsa.e, e_buf, &mpi_len_e);
    uint32_t total_len = 1 + 4 + mpi_len_n + mpi_len_e;
    pkt[1] = (total_len >> 8) & 0xFF;
    pkt[2] = total_len & 0xFF;
    pkt[3] = key->algo;
    uint8_t ver[5] = { 4, 0, 0, 0, 0 };
    uint32_t ts = key->timestamp;
    ver[1] = (ts >> 24) & 0xFF; ver[2] = (ts >> 16) & 0xFF;
    ver[3] = (ts >> 8) & 0xFF; ver[4] = ts & 0xFF;
    sec_sha256_update(&ctx, pkt, 3);
    sec_sha256_update(&ctx, ver, 5);
    sec_sha256_update(&ctx, n_buf, mpi_len_n);
    sec_sha256_update(&ctx, e_buf, mpi_len_e);
    sec_sha256_final(&ctx, fp);
}

void pgp_key_id_from_fingerprint(const uint8_t *fp, uint8_t *kid) {
    for (uint32_t i = 0; i < 8; i++) kid[i] = fp[12 + i];
}

int pgp_key_gen(pgp_key_t *key, const char *user_id) {
    pgp_memset(key, 0, sizeof(pgp_key_t));
    key->version = 4;
    key->timestamp = (uint32_t)(ring0_ticks() / ring0_state.tsc_per_ms / 1000);
    key->algo = PGP_PUBKEY_ALGO_RSA;
    int ret = pgp_rsa_keygen(&key->rsa, 2048);
    if (ret != 0) return ret;
    key->is_private = 1;
    compute_fingerprint(key, key->fingerprint);
    pgp_key_id_from_fingerprint(key->fingerprint, key->key_id);
    if (user_id) {
        uint32_t ulen = 0;
        while (user_id[ulen] && ulen < 127) { key->user_id[ulen] = (uint8_t)user_id[ulen]; ulen++; }
        key->user_id[ulen] = 0;
        key->user_id_len = ulen;
    }
    return PGP_OK;
}

int pgp_find_key(const uint8_t *key_id, pgp_key_t **key, int search_secret) {
    uint32_t count = search_secret ? pgp_seckey_count : pgp_pubkey_count;
    pgp_key_t *keys = search_secret ? pgp_seckeys : pgp_pubkeys;
    for (uint32_t i = 0; i < count; i++) {
        int match = 1;
        for (uint32_t j = 0; j < 8; j++)
            if (keys[i].key_id[j] != key_id[j]) { match = 0; break; }
        if (match) { *key = &keys[i]; return PGP_OK; }
    }
    return PGP_ERR_NOT_FOUND;
}

/* ---- PGP packet encoding/decoding ---- */

void pgp_key_to_pkt(const pgp_key_t *key, uint8_t *buf, uint32_t *len) {
    uint32_t pos = 0;
    uint8_t tag = key->is_private ? 0x95 : 0x99;
    buf[pos++] = tag;
    uint8_t n_buf[PGP_RSA_BYTES + 2], e_buf[8], d_buf[PGP_RSA_BYTES + 2];
    uint32_t n_len, e_len, d_len, p_len, q_len, dp_len, dq_len, qinv_len;
    pgp_mpi_to_bytes(&key->rsa.n, n_buf, &n_len);
    pgp_mpi_to_bytes(&key->rsa.e, e_buf, &e_len);
    uint32_t body_len = 8 + n_len + e_len;
    if (key->is_private) {
        pgp_mpi_to_bytes(&key->rsa.d, d_buf, &d_len);
        p_len = q_len = dp_len = dq_len = qinv_len = n_len;
        body_len += 1 + d_len + n_len * 4;
    }
    buf[pos++] = (body_len >> 24) & 0xFF;
    buf[pos++] = (body_len >> 16) & 0xFF;
    buf[pos++] = (body_len >> 8) & 0xFF;
    buf[pos++] = body_len & 0xFF;
    buf[pos++] = 4;
    uint32_t ts = key->timestamp;
    buf[pos++] = (ts >> 24) & 0xFF; buf[pos++] = (ts >> 16) & 0xFF;
    buf[pos++] = (ts >> 8) & 0xFF; buf[pos++] = ts & 0xFF;
    buf[pos++] = key->algo;
    for (uint32_t i = 0; i < n_len; i++) buf[pos++] = n_buf[i];
    for (uint32_t i = 0; i < e_len; i++) buf[pos++] = e_buf[i];
    if (key->is_private) {
        buf[pos++] = 6;
        for (uint32_t i = 0; i < d_len; i++) buf[pos++] = d_buf[i];
        pgp_mpi_to_bytes(&key->rsa.p, d_buf, &p_len);
        for (uint32_t i = 0; i < p_len; i++) buf[pos++] = d_buf[i];
        pgp_mpi_to_bytes(&key->rsa.q, d_buf, &q_len);
        for (uint32_t i = 0; i < q_len; i++) buf[pos++] = d_buf[i];
        pgp_mpi_to_bytes(&key->rsa.dp, d_buf, &dp_len);
        for (uint32_t i = 0; i < dp_len; i++) buf[pos++] = d_buf[i];
        pgp_mpi_to_bytes(&key->rsa.dq, d_buf, &dq_len);
        for (uint32_t i = 0; i < dq_len; i++) buf[pos++] = d_buf[i];
        pgp_mpi_to_bytes(&key->rsa.qinv, d_buf, &qinv_len);
        for (uint32_t i = 0; i < qinv_len; i++) buf[pos++] = d_buf[i];
    }
    if (key->user_id_len > 0) {
        buf[pos++] = 0xBD;
        uint32_t uid_body_len = key->user_id_len;
        buf[pos++] = (uid_body_len >> 24) & 0xFF;
        buf[pos++] = (uid_body_len >> 16) & 0xFF;
        buf[pos++] = (uid_body_len >> 8) & 0xFF;
        buf[pos++] = uid_body_len & 0xFF;
        for (uint32_t i = 0; i < key->user_id_len; i++) buf[pos++] = key->user_id[i];
    }
    *len = pos;
}

int pgp_key_from_pkt(pgp_key_t *key, const uint8_t *buf, uint32_t len) {
    pgp_memset(key, 0, sizeof(pgp_key_t));
    uint32_t pos = 0;
    if (pos >= len) return PGP_ERR_FORMAT;
    uint8_t tag = buf[pos++];
    key->is_private = (tag == 0x95 || tag == 0xBF);
    if (tag != 0x99 && tag != 0x95) return PGP_ERR_FORMAT;
    if (pos + 4 > len) return PGP_ERR_FORMAT;
    uint32_t body_len = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16) | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
    pos += 4;
    if (pos + body_len > len) return PGP_ERR_FORMAT;
    uint32_t body_end = pos + body_len;
    if (pos >= body_end) return PGP_ERR_FORMAT;
    key->version = buf[pos++];
    if (key->version != 4) return PGP_ERR_FORMAT;
    if (pos + 4 > body_end) return PGP_ERR_FORMAT;
    key->timestamp = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16) | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
    pos += 4;
    if (pos >= body_end) return PGP_ERR_FORMAT;
    key->algo = buf[pos++];
    if (pos + 2 > body_end) return PGP_ERR_FORMAT;
    uint32_t n_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
    uint32_t n_bytes = (n_mpi_len + 7) / 8 + 2;
    if (pos + n_bytes > body_end) return PGP_ERR_FORMAT;
    pgp_mpi_from_bytes(&key->rsa.n, &buf[pos], n_bytes);
    pos += n_bytes;
    if (pos + 2 > body_end) return PGP_ERR_FORMAT;
    uint32_t e_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
    uint32_t e_bytes = (e_mpi_len + 7) / 8 + 2;
    if (pos + e_bytes > body_end) return PGP_ERR_FORMAT;
    pgp_mpi_from_bytes(&key->rsa.e, &buf[pos], e_bytes);
    pos += e_bytes;
    if (key->is_private && pos < body_end) {
        uint32_t s2k_usage = buf[pos++];
        (void)s2k_usage;
        if (pos >= body_end) return PGP_ERR_FORMAT;
        if (pos + 2 > body_end) return PGP_ERR_FORMAT;
        uint32_t d_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
        uint32_t d_bytes = (d_mpi_len + 7) / 8 + 2;
        if (pos + d_bytes > body_end) return PGP_ERR_FORMAT;
        pgp_mpi_from_bytes(&key->rsa.d, &buf[pos], d_bytes);
        pos += d_bytes;
        if (pos + 2 <= body_end) {
            uint32_t p_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
            uint32_t p_bytes = (p_mpi_len + 7) / 8 + 2;
            if (pos + p_bytes > body_end) return PGP_ERR_FORMAT;
            pgp_mpi_from_bytes(&key->rsa.p, &buf[pos], p_bytes);
            pos += p_bytes;
        }
        if (pos + 2 <= body_end) {
            uint32_t q_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
            uint32_t q_bytes = (q_mpi_len + 7) / 8 + 2;
            if (pos + q_bytes > body_end) return PGP_ERR_FORMAT;
            pgp_mpi_from_bytes(&key->rsa.q, &buf[pos], q_bytes);
            pos += q_bytes;
        }
        if (pos + 2 <= body_end) {
            uint32_t dp_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
            uint32_t dp_bytes = (dp_mpi_len + 7) / 8 + 2;
            if (pos + dp_bytes > body_end) return PGP_ERR_FORMAT;
            pgp_mpi_from_bytes(&key->rsa.dp, &buf[pos], dp_bytes);
            pos += dp_bytes;
        }
        if (pos + 2 <= body_end) {
            uint32_t dq_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
            uint32_t dq_bytes = (dq_mpi_len + 7) / 8 + 2;
            if (pos + dq_bytes > body_end) return PGP_ERR_FORMAT;
            pgp_mpi_from_bytes(&key->rsa.dq, &buf[pos], dq_bytes);
            pos += dq_bytes;
        }
        if (pos + 2 <= body_end) {
            uint32_t qinv_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
            uint32_t qinv_bytes = (qinv_mpi_len + 7) / 8 + 2;
            if (pos + qinv_bytes > body_end) return PGP_ERR_FORMAT;
            pgp_mpi_from_bytes(&key->rsa.qinv, &buf[pos], qinv_bytes);
            pos += qinv_bytes;
        }
    }
    compute_fingerprint(key, key->fingerprint);
    pgp_key_id_from_fingerprint(key->fingerprint, key->key_id);
    return PGP_OK;
}

void pgp_sig_to_pkt(const pgp_signature_t *sig, uint8_t *buf, uint32_t *len) {
    uint32_t pos = 0;
    buf[pos++] = 0x89;
    uint8_t sig_buf[PGP_RSA_BYTES + 2];
    uint32_t sig_len;
    pgp_mpi_to_bytes(&sig->sig_val, sig_buf, &sig_len);
    uint32_t body_len = 2 + 8 + 2 + sig_len;
    buf[pos++] = (body_len >> 24) & 0xFF;
    buf[pos++] = (body_len >> 16) & 0xFF;
    buf[pos++] = (body_len >> 8) & 0xFF;
    buf[pos++] = body_len & 0xFF;
    buf[pos++] = 4;
    buf[pos++] = sig->sig_type;
    buf[pos++] = sig->hash_algo;
    buf[pos++] = 2;
    uint32_t ts = sig->timestamp;
    buf[pos++] = (ts >> 24) & 0xFF; buf[pos++] = (ts >> 16) & 0xFF;
    buf[pos++] = (ts >> 8) & 0xFF; buf[pos++] = ts & 0xFF;
    for (uint32_t i = 0; i < 8; i++) buf[pos++] = sig->key_id[i];
    for (uint32_t i = 0; i < sig_len; i++) buf[pos++] = sig_buf[i];
    *len = pos;
}

int pgp_sig_from_pkt(pgp_signature_t *sig, const uint8_t *buf, uint32_t len) {
    pgp_memset(sig, 0, sizeof(pgp_signature_t));
    uint32_t pos = 0;
    if (pos >= len || buf[pos] != 0x89) return PGP_ERR_FORMAT;
    pos++;
    if (pos + 4 > len) return PGP_ERR_FORMAT;
    uint32_t body_len = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16) | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
    pos += 4;
    if (pos + body_len > len) return PGP_ERR_FORMAT;
    uint32_t body_end = pos + body_len;
    if (pos >= body_end) return PGP_ERR_FORMAT;
    sig->version = buf[pos++];
    if (sig->version != 4) return PGP_ERR_FORMAT;
    if (pos >= body_end) return PGP_ERR_FORMAT;
    sig->sig_type = buf[pos++];
    if (pos >= body_end) return PGP_ERR_FORMAT;
    sig->hash_algo = buf[pos++];
    if (pos >= body_end) return PGP_ERR_FORMAT;
    uint8_t hash_start_len = buf[pos++];
    if (pos + hash_start_len + 8 > body_end) return PGP_ERR_FORMAT;
    for (uint32_t i = 0; i < hash_start_len; i++) {
        uint8_t h = buf[pos++]; (void)h;
    }
    if (pos + 4 > body_end) return PGP_ERR_FORMAT;
    sig->timestamp = ((uint32_t)buf[pos] << 24) | ((uint32_t)buf[pos+1] << 16) | ((uint32_t)buf[pos+2] << 8) | buf[pos+3];
    pos += 4;
    if (pos + 8 > body_end) return PGP_ERR_FORMAT;
    for (uint32_t i = 0; i < 8; i++) sig->key_id[i] = buf[pos++];
    if (pos + 2 > body_end) return PGP_ERR_FORMAT;
    uint32_t sig_mpi_len = ((uint32_t)buf[pos] << 8) | buf[pos+1];
    uint32_t sig_bytes = (sig_mpi_len + 7) / 8 + 2;
    if (pos + sig_bytes > body_end) return PGP_ERR_FORMAT;
    pgp_mpi_from_bytes(&sig->sig_val, &buf[pos], sig_bytes);
    return PGP_OK;
}

/* ---- PGP signing and verification ---- */

int pgp_sign_hash(pgp_signature_t *sig, const pgp_key_t *key, const uint8_t *hash, uint32_t hash_len, uint8_t hash_algo) {
    pgp_memset(sig, 0, sizeof(pgp_signature_t));
    sig->version = 4;
    sig->sig_type = PGP_SIG_BINARY;
    sig->hash_algo = hash_algo;
    for (uint32_t i = 0; i < 8; i++) sig->key_id[i] = key->key_id[i];
    sig->timestamp = (uint32_t)(ring0_ticks() / ring0_state.tsc_per_ms / 1000);
    if (hash_len > 64) hash_len = 64;
    for (uint32_t i = 0; i < hash_len; i++) sig->hash[i] = hash[i];
    sig->hash_len = hash_len;
    return pgp_rsa_sign(&key->rsa, hash, hash_len, &sig->sig_val);
}

int pgp_verify_hash(const pgp_key_t *key, const uint8_t *hash, uint32_t hash_len, const pgp_signature_t *sig) {
    return pgp_rsa_verify(&key->rsa, hash, hash_len, &sig->sig_val);
}

int pgp_sign_message(pgp_signature_t *sig, const pgp_key_t *key, const uint8_t *msg, uint32_t msg_len) {
    uint8_t hash[32];
    sec_sha256(msg, msg_len, hash);
    return pgp_sign_hash(sig, key, hash, 32, PGP_HASH_SHA256);
}

int pgp_verify_message(const pgp_key_t *key, const uint8_t *msg, uint32_t msg_len, const pgp_signature_t *sig) {
    uint8_t hash[32];
    sec_sha256(msg, msg_len, hash);
    return pgp_verify_hash(key, hash, 32, sig);
}

/* ---- PGP hybrid encryption (RSA + AES-256) ---- */

int pgp_encrypt(const pgp_key_t *recipient, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len) {
    uint8_t session_key[32];
    sec_random_bytes(session_key, 32);
    uint32_t pkesk_len;
    int ret = pgp_rsa_encrypt(&recipient->rsa, session_key, 32, out, &pkesk_len);
    if (ret != 0) return ret;
    uint32_t pos = pkesk_len;
    uint8_t iv[12];
    sec_random_bytes(iv, 12);
    out[pos++] = 0x85;
    uint32_t sed_body = 1 + 12 + in_len + 16;
    out[pos++] = (sed_body >> 24) & 0xFF;
    out[pos++] = (sed_body >> 16) & 0xFF;
    out[pos++] = (sed_body >> 8) & 0xFF;
    out[pos++] = sed_body & 0xFF;
    out[pos++] = 4;
    for (uint32_t i = 0; i < 12; i++) out[pos++] = iv[i];
    sec_aes256_ctx_t aes;
    sec_aes256_init(&aes, session_key);
    sec_aes256_gcm_encrypt(&aes, in, &out[pos], in_len, iv, NULL, 0, &out[pos + in_len]);
    pos += in_len + 16;
    *out_len = pos;
    return PGP_OK;
}

int pgp_decrypt(const pgp_key_t *key, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len) {
    uint32_t pos = 0;
    if (in_len < 258) return PGP_ERR_DECRYPT;
    uint8_t session_key[32];
    uint32_t sk_len = 32;
    int ret = pgp_rsa_decrypt(&key->rsa, &in[pos], PGP_RSA_BYTES + 2, session_key, &sk_len);
    if (ret != 0) return ret;
    pos += PGP_RSA_BYTES + 2;
    if (pos >= in_len || in[pos] != 0x85) return PGP_ERR_FORMAT;
    pos++;
    if (pos + 4 > in_len) return PGP_ERR_FORMAT;
    uint32_t sed_body = ((uint32_t)in[pos] << 24) | ((uint32_t)in[pos+1] << 16) | ((uint32_t)in[pos+2] << 8) | in[pos+3];
    pos += 4;
    if (pos + sed_body > in_len) return PGP_ERR_FORMAT;
    if (in[pos] != 4) return PGP_ERR_FORMAT;
    pos++;
    if (pos + 12 > in_len) return PGP_ERR_FORMAT;
    const uint8_t *iv = &in[pos];
    pos += 12;
    uint32_t data_len = sed_body - 1 - 12 - 16;
    if (pos + data_len + 16 > in_len) return PGP_ERR_FORMAT;
    sec_aes256_ctx_t aes;
    sec_aes256_init(&aes, session_key);
    if (sec_aes256_gcm_decrypt(&aes, &in[pos], out, data_len, iv, NULL, 0, &in[pos + data_len]) != 0)
        return PGP_ERR_DECRYPT;
    *out_len = data_len;
    return PGP_OK;
}
