#ifndef SEC_PGP_H
#define SEC_PGP_H

#include <stdint.h>

#define PGP_RSA_BITS            2048
#define PGP_RSA_BYTES           (PGP_RSA_BITS / 8)
#define PGP_MPI_MAX_LIMBS       (PGP_RSA_BITS / 32)
#define PGP_KEY_ID_LEN          8
#define PGP_FINGERPRINT_LEN     20
#define PGP_MAX_KEYS            16
#define PGP_MAX_PKT_LEN         4096
#define PGP_SALT_LEN            16

#define PGP_PUBKEY_ALGO_RSA     1
#define PGP_PUBKEY_ALGO_RSA_E   2
#define PGP_PUBKEY_ALGO_RSA_S   3

#define PGP_HASH_SHA1           2
#define PGP_HASH_SHA256         8
#define PGP_HASH_SHA512         10

#define PGP_PKT_PUBKEY          6
#define PGP_PKT_SECKEY          7
#define PGP_PKT_SIG             2
#define PGP_PKT_PKESK           1
#define PGP_PKT_SED             9
#define PGP_PKT_LITERAL         11
#define PGP_PKT_USERID          13

#define PGP_SIG_BINARY          0x00
#define PGP_SIG_TEXT            0x01

#define PGP_ARMOR_LINE_LEN      64

#define PGP_OK                  0
#define PGP_ERR_NOMEM          -1
#define PGP_ERR_PARAM          -2
#define PGP_ERR_KEY            -3
#define PGP_ERR_SIG            -4
#define PGP_ERR_ENCRYPT        -5
#define PGP_ERR_DECRYPT        -6
#define PGP_ERR_FORMAT         -7
#define PGP_ERR_NOT_FOUND      -8

typedef struct {
    uint32_t limbs[PGP_MPI_MAX_LIMBS];
    uint32_t count;
} pgp_mpi_t;

typedef struct {
    pgp_mpi_t n;
    pgp_mpi_t e;
    pgp_mpi_t d;
    pgp_mpi_t p;
    pgp_mpi_t q;
    pgp_mpi_t dp;
    pgp_mpi_t dq;
    pgp_mpi_t qinv;
} pgp_rsa_key_t;

typedef struct {
    uint8_t  key_id[PGP_KEY_ID_LEN];
    uint8_t  fingerprint[PGP_FINGERPRINT_LEN];
    uint32_t timestamp;
    uint8_t  version;
    uint8_t  algo;
    uint8_t  is_private;
    uint8_t  user_id[128];
    uint32_t user_id_len;
    pgp_rsa_key_t rsa;
} pgp_key_t;

typedef struct {
    uint8_t  version;
    uint8_t  sig_type;
    uint8_t  hash_algo;
    uint8_t  key_id[PGP_KEY_ID_LEN];
    uint32_t timestamp;
    pgp_mpi_t sig_val;
    uint8_t  hash[64];
    uint32_t hash_len;
} pgp_signature_t;

typedef struct {
    uint8_t  version;
    uint32_t timestamp;
    uint8_t  algo;
    uint8_t  key_id[PGP_KEY_ID_LEN];
    pgp_mpi_t esk; /* encrypted session key */
} pgp_pkesk_t;

void pgp_mpi_zero(pgp_mpi_t *a);
int  pgp_mpi_is_zero(const pgp_mpi_t *a);
int  pgp_mpi_is_one(const pgp_mpi_t *a);
void pgp_mpi_set_u32(pgp_mpi_t *a, uint32_t v);
int  pgp_mpi_compare(const pgp_mpi_t *a, const pgp_mpi_t *b);
void pgp_mpi_add(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b);
void pgp_mpi_sub(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b);
void pgp_mpi_mul(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b);
void pgp_mpi_div(pgp_mpi_t *q, pgp_mpi_t *rem, const pgp_mpi_t *a, const pgp_mpi_t *b);
void pgp_mpi_mod(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *m);
void pgp_mpi_pow_mod(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *e, const pgp_mpi_t *m);
void pgp_mpi_gcd(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b);
int  pgp_mpi_inv_mod(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *m);
int  pgp_mpi_is_prime(const pgp_mpi_t *a, int rounds);
void pgp_mpi_from_bytes(pgp_mpi_t *a, const uint8_t *buf, uint32_t len);
void pgp_mpi_to_bytes(const pgp_mpi_t *a, uint8_t *buf, uint32_t *len);
void pgp_mpi_rand(pgp_mpi_t *a, uint32_t bits);
void pgp_mpi_lshift(pgp_mpi_t *r, const pgp_mpi_t *a, uint32_t bits);
void pgp_mpi_rshift(pgp_mpi_t *r, const pgp_mpi_t *a, uint32_t bits);
void pgp_mpi_and(pgp_mpi_t *r, const pgp_mpi_t *a, const pgp_mpi_t *b);
void pgp_mpi_set_bit(pgp_mpi_t *a, uint32_t bit);

int  pgp_rsa_keygen(pgp_rsa_key_t *key, uint32_t bits);
void pgp_rsa_public(pgp_rsa_key_t *pub, const pgp_rsa_key_t *key);
int  pgp_rsa_sign(const pgp_rsa_key_t *key, const uint8_t *hash, uint32_t hash_len, pgp_mpi_t *sig);
int  pgp_rsa_verify(const pgp_rsa_key_t *key, const uint8_t *hash, uint32_t hash_len, const pgp_mpi_t *sig);
int  pgp_rsa_encrypt(const pgp_rsa_key_t *key, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len);
int  pgp_rsa_decrypt(const pgp_rsa_key_t *key, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len);

int  pgp_key_gen(pgp_key_t *key, const char *user_id);
int  pgp_key_fingerprint(pgp_key_t *key);
void pgp_key_id_from_fingerprint(const uint8_t *fp, uint8_t *kid);

int  pgp_sign_hash(pgp_signature_t *sig, const pgp_key_t *key, const uint8_t *hash, uint32_t hash_len, uint8_t hash_algo);
int  pgp_verify_hash(const pgp_key_t *key, const uint8_t *hash, uint32_t hash_len, const pgp_signature_t *sig);
int  pgp_sign_message(pgp_signature_t *sig, const pgp_key_t *key, const uint8_t *msg, uint32_t msg_len);
int  pgp_verify_message(const pgp_key_t *key, const uint8_t *msg, uint32_t msg_len, const pgp_signature_t *sig);

int  pgp_encrypt(const pgp_key_t *recipient, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len);
int  pgp_decrypt(const pgp_key_t *key, const uint8_t *in, uint32_t in_len, uint8_t *out, uint32_t *out_len);

void pgp_key_to_pkt(const pgp_key_t *key, uint8_t *buf, uint32_t *len);
int  pgp_key_from_pkt(pgp_key_t *key, const uint8_t *buf, uint32_t len);
void pgp_sig_to_pkt(const pgp_signature_t *sig, uint8_t *buf, uint32_t *len);
int  pgp_sig_from_pkt(pgp_signature_t *sig, const uint8_t *buf, uint32_t len);

extern pgp_key_t pgp_pubkeys[PGP_MAX_KEYS];
extern uint32_t  pgp_pubkey_count;
extern pgp_key_t pgp_seckeys[PGP_MAX_KEYS];
extern uint32_t  pgp_seckey_count;

int  pgp_find_key(const uint8_t *key_id, pgp_key_t **key, int search_secret);
void pgp_init(void);

#endif
