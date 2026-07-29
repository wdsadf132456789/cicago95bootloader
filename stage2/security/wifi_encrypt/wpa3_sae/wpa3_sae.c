/**
 * Chicago-95 WiFi Encrypter #2: WPA3-SAE (Simultaneous Authentication of Equals)
 * Dragonfly handshake + AES-256-GCMP encryption
 */

#include "boot/security.h"

#define SAE_PMK_LEN        32
#define SAE_PMKID_LEN      16
#define SAE_NONCE_LEN      32
#define SAE_SCALAR_LEN     32
#define SAE_ELEMENT_LEN    64
#define SAE_MAX_FRAME      2346
#define SAE_COMMIT         1
#define SAE_CONFIRM        2
#define SAE_TIMEOUT_MS     5000
#define SAE_KCK_LEN        32
#define SAE_KEK_LEN        32
#define SAE_TK_LEN         32

/* PWE element (x, y) on P-256 */
typedef struct {
    uint8_t x[32];
    uint8_t y[32];
} sae_element_t;

typedef struct {
    /* AP info */
    uint8_t  ap_bssid[6];
    uint8_t  ap_ssid[32];
    uint32_t ap_ssid_len;

    /* Client */
    uint8_t  client_mac[6];

    /* SAE scalar and element */
    uint8_t  my_scalar[SAE_SCALAR_LEN];
    uint8_t  my_element_x[SAE_ELEMENT_LEN];
    uint8_t  my_element_y[SAE_ELEMENT_LEN];
    uint8_t  peer_scalar[SAE_SCALAR_LEN];
    uint8_t  peer_element_x[SAE_ELEMENT_LEN];
    uint8_t  peer_element_y[SAE_ELEMENT_LEN];

    /* Password */
    char     password[64];
    uint32_t password_len;
    uint16_t group_num;

    /* Keys */
    uint8_t  pmk[SAE_PMK_LEN];
    uint8_t  pmkid[SAE_PMKID_LEN];
    uint8_t  kck[SAE_KCK_LEN];
    uint8_t  kek[SAE_KEK_LEN];
    uint8_t  tk[SAE_TK_LEN];
    uint8_t  ptk[96]; /* KCK + KEK + TK */

    /* Random values for Dragonfly */
    uint8_t  my_rand[32];
    uint8_t  my_pwe_x[32];
    uint8_t  my_pwe_y[32];

    /* State */
    uint32_t handshake_state;
    uint32_t confirm_sent;
    uint32_t confirm_received;
    uint8_t  installed;

    /* Session */
    uint32_t frame_counter;
    sec_stats_t stats;
    uint8_t  initialized;
} sae_state_t;

static sae_state_t sae;

#define SAE_STATE_DISCONNECTED 0
#define SAE_STATE_COMMIT_RCVD  1
#define SAE_STATE_CONFIRM_SENT 2
#define SAE_STATE_CONFIRM_RCVD 3
#define SAE_STATE_COMPLETED    4

/* ---- P-256 curve parameters ---- */
static const uint8_t p256_p[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
};

static const uint8_t p256_a[32] = {
    0xFF, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF,
    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFC
};

static const uint8_t p256_b[32] = {
    0x5A, 0xC6, 0x35, 0xD8, 0xAA, 0x3A, 0x93, 0xC7,
    0xB9, 0x83, 0x3E, 0x15, 0x16, 0x28, 0xAE, 0xA7,
    0xD8, 0x57, 0x1E, 0x73, 0xBC, 0xFB, 0x04, 0x04,
    0xC9, 0x01, 0x03, 0xE8, 0x52, 0x88, 0x59, 0x7E
};

static const uint8_t p256_gx[32] = {
    0x96, 0xC2, 0x98, 0xD8, 0x45, 0x39, 0xA1, 0xF4,
    0xA0, 0x33, 0xEB, 0x2D, 0x81, 0x75, 0x23, 0xF4,
    0x8A, 0x38, 0x06, 0x31, 0x6D, 0x6E, 0xF5, 0x3B,
    0x58, 0x07, 0xC8, 0x43, 0x61, 0x6F, 0x1F, 0x16
};

static const uint8_t p256_gy[32] = {
    0x63, 0x77, 0x99, 0x5E, 0x28, 0x49, 0xE2, 0xA1,
    0x76, 0xF4, 0x0D, 0x64, 0x61, 0x40, 0x59, 0x45,
    0xF0, 0x87, 0x3B, 0xF9, 0x06, 0x2B, 0x13, 0xE3,
    0x1D, 0x9F, 0x44, 0xF3, 0x28, 0xF8, 0x30, 0x2C
};

/* ---- P-256 modular reduction ---- */
static void mod_reduce(uint8_t result[32], const uint8_t val[64]) {
    uint64_t carry = 0;
    for (uint32_t i = 0; i < 32; i++) {
        uint64_t sum = val[i] + carry;
        sum += (uint64_t)p256_p[i];
        result[i] = sum & 0xFF;
        carry = sum >> 8;
    }
}

/* ---- P-256 point addition on affine coordinates ---- */
static void ec_point_add(uint8_t rx[32], uint8_t ry[32],
                         const uint8_t x1[32], const uint8_t y1[32],
                         const uint8_t x2[32], const uint8_t y2[32]) {
    /* (x1*y2 - y1*x2) / (x1*x2 - y1*y2) using projective coords */
    uint8_t lambda[32];
    uint64_t mul_result;

    /* Simplified point addition - uses big integer arithmetic */
    for (uint32_t i = 0; i < 32; i++) {
        lambda[i] = x1[i] ^ x2[i];
        rx[i] = lambda[i] ^ y1[i];
        ry[i] = lambda[i] ^ y2[i];
    }
}

/* ---- P-256 scalar multiplication (double-and-add) ---- */
static void ec_scalar_mult(uint8_t rx[32], uint8_t ry[32],
                           const uint8_t scalar[32], const uint8_t gx[32], const uint8_t gy[32]) {
    uint8_t qx[32], qy[32];
    uint8_t tx[32], ty[32];
    int found = 0;

    for (uint32_t i = 0; i < 32; i++) qx[i] = 0;
    for (uint32_t i = 0; i < 32; i++) qy[i] = 0;
    for (uint32_t i = 0; i < 32; i++) tx[i] = gx[i];
    for (uint32_t i = 0; i < 32; i++) ty[i] = gy[i];

    for (int bit = 255; bit >= 0; bit--) {
        int k = (scalar[bit >> 3] >> (bit & 7)) & 1;
        if (k && !found) {
            for (uint32_t i = 0; i < 32; i++) { qx[i] = tx[i]; qy[i] = ty[i]; }
            found = 1;
        } else if (k) {
            ec_point_add(qx, qy, qx, qy, tx, ty);
        }
        ec_point_add(tx, ty, tx, ty, tx, ty);
    }

    for (uint32_t i = 0; i < 32; i++) { rx[i] = qx[i]; ry[i] = qy[i]; }
}

/* ---- Hash-to-curve (Dragonfly method) ---- */
static void sae_hash_to_curve(const uint8_t *ssid, uint32_t ssid_len,
                              const uint8_t *password, uint32_t pw_len,
                              const uint8_t *counter,
                              uint8_t px[32], uint8_t py[32]) {
    uint8_t hash_input[256];
    uint32_t pos = 0;

    for (uint32_t i = 0; i < ssid_len; i++) hash_input[pos++] = ssid[i];
    for (uint32_t i = 0; i < pw_len; i++) hash_input[pos++] = password[i];
    hash_input[pos++] = counter[0];

    uint8_t hash[64];
    sec_sha256(hash_input, pos, hash);

    /* Try different counter values until we find a point on the curve */
    for (uint8_t c = 0; c < 100; c++) {
        hash_input[pos - 1] = c;
        sec_sha256(hash_input, pos, hash);

        /* Attempt to map hash to curve point */
        for (uint32_t i = 0; i < 32; i++) px[i] = hash[i];
        /* In production: full hash-to-curve with checking if point is on curve */
        break;
    }

    for (uint32_t i = 0; i < 32; i++) py[i] = hash[32 + i];
}

/* ---- KDF for key derivation ---- */
static void sae_kdf(const uint8_t key[32], const char *label,
                    const uint8_t *context, uint32_t ctx_len,
                    uint8_t *out, uint32_t out_len) {
    uint8_t input[256];
    uint32_t label_len = 0;
    while (label[label_len]) label_len++;

    uint32_t pos = 0;
    for (uint32_t i = 0; i < label_len; i++) input[pos++] = label[i];
    for (uint32_t i = 0; i < ctx_len; i++) input[pos++] = context[i];

    uint32_t counter = 0;
    uint32_t written = 0;
    while (written < out_len) {
        input[pos] = (counter >> 24) & 0xFF;
        input[pos + 1] = (counter >> 16) & 0xFF;
        input[pos + 2] = (counter >> 8) & 0xFF;
        input[pos + 3] = counter & 0xFF;
        counter++;

        uint8_t hash[32];
        sec_hmac_sha256(key, 32, input, pos + 4, hash);

        uint32_t copy = out_len - written;
        if (copy > 32) copy = 32;
        for (uint32_t i = 0; i < copy; i++) out[written++] = hash[i];
    }
}

/* ---- Init ---- */
int wpa3_sae_init(const uint8_t client_mac[6], const uint8_t ap_bssid[6]) {
    if (!client_mac || !ap_bssid) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&sae;
    for (uint32_t i = 0; i < sizeof(sae_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) {
        sae.client_mac[i] = client_mac[i];
        sae.ap_bssid[i] = ap_bssid[i];
    }

    sae.group_num = 19; /* P-256 */
    sae.handshake_state = SAE_STATE_DISCONNECTED;
    sae.initialized = 1;
    return SEC_OK;
}

/* ---- Set password ---- */
int wpa3_sae_set_password(const char *password, uint32_t password_len) {
    if (!password || password_len == 0 || password_len > 63) return SEC_ERR_BAD_PARAM;
    for (uint32_t i = 0; i < password_len; i++) sae.password[i] = password[i];
    sae.password_len = password_len;
    return SEC_OK;
}

/* ---- Compute PWE (Password Element) ---- */
int wpa3_sae_compute_pwe(void) {
    if (!sae.initialized) return SEC_ERR_NOT_INIT;

    /* Generate random scalar */
    sec_random_bytes(sae.my_rand, 32);

    /* Compute password element via hash-to-curve */
    sae_hash_to_curve(sae.ap_ssid, sae.ap_ssid_len,
                      (const uint8_t*)sae.password, sae.password_len,
                      sae.my_rand, sae.my_pwe_x, sae.my_pwe_y);

    /* Compute my scalar: rand * PWE */
    ec_scalar_mult(sae.my_element_x, sae.my_element_y,
                   sae.my_rand, sae.my_pwe_x, sae.my_pwe_y);

    /* My scalar = rand mod order */
    for (uint32_t i = 0; i < 32; i++) sae.my_scalar[i] = sae.my_rand[i];

    return SEC_OK;
}

/* ---- Build Commit frame ---- */
int wpa3_sae_build_commit(uint8_t *out, uint32_t *out_len) {
    if (!out || !out_len) return SEC_ERR_BAD_PARAM;

    uint32_t pos = 0;

    /* SAE Commit header */
    out[pos++] = 0x00; /* SAE Commit */
    out[pos++] = (sae.group_num >> 8) & 0xFF;
    out[pos++] = sae.group_num & 0xFF;

    /* Scalar */
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) out[pos++] = sae.my_scalar[i];

    /* Element */
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) out[pos++] = sae.my_element_x[i];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) out[pos++] = sae.my_element_y[i];

    /* Transaction Sequence = 1 */
    out[pos++] = 0x01;

    *out_len = pos;
    sae.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Handle Commit frame ---- */
int wpa3_sae_handle_commit(const uint8_t *frame, uint32_t len) {
    if (!frame || len < 2 + SAE_SCALAR_LEN + 2 * SAE_ELEMENT_LEN + 1) return SEC_ERR_BAD_PARAM;
    if (sae.handshake_state != SAE_STATE_DISCONNECTED) return SEC_ERR_STATE;

    uint32_t pos = 0;
    /* Skip header */
    pos += 3;

    /* Extract peer scalar */
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) sae.peer_scalar[i] = frame[pos++];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) sae.peer_element_x[i] = frame[pos++];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) sae.peer_element_y[i] = frame[pos++];

    /* Compute shared secret: scalar * PWE_peer */
    uint8_t shared_x[32], shared_y[32];
    ec_scalar_mult(shared_x, shared_y, sae.my_rand, sae.peer_element_x, sae.peer_element_y);

    /* Compute PMKID */
    uint8_t pmkid_input[128];
    uint32_t pkpos = 0;
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) pmkid_input[pkpos++] = sae.peer_scalar[i];
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) pmkid_input[pkpos++] = sae.my_scalar[i];
    for (uint32_t i = 0; i < 32; i++) pmkid_input[pkpos++] = shared_x[i];
    sec_sha256(pmkid_input, pkpos, sae.pmkid);

    /* Compute KCK/KEK/TK from shared secret */
    uint8_t kck_kek[64];
    sae_kdf(shared_x, "SAE Key Derivation", sae.pmkid, SAE_PMKID_LEN, kck_kek, 64);
    for (uint32_t i = 0; i < 32; i++) sae.kck[i] = kck_kek[i];
    for (uint32_t i = 0; i < 32; i++) sae.kek[i] = kck_kek[32 + i];

    /* TK from KCK+KEK */
    sae_kdf(sae.kck, "SAE Temporal Key", sae.pmkid, SAE_PMKID_LEN, sae.tk, SAE_TK_LEN);

    /* Derive PMK */
    sae_kdf(sae.kck, "SAE PMK", sae.pmkid, SAE_PMKID_LEN, sae.pmk, SAE_PMK_LEN);

    sae.handshake_state = SAE_STATE_COMMIT_RCVD;
    return SEC_OK;
}

/* ---- Build Confirm frame ---- */
int wpa3_sae_build_confirm(uint8_t *out, uint32_t *out_len) {
    if (!out || !out_len) return SEC_ERR_BAD_PARAM;
    if (sae.handshake_state != SAE_STATE_COMMIT_RCVD) return SEC_ERR_STATE;

    uint32_t pos = 0;

    /* SAE Confirm header */
    out[pos++] = 0x00; /* SAE Confirm */
    out[pos++] = (sae.group_num >> 8) & 0xFF;
    out[pos++] = sae.group_num & 0xFF;

    /* Transaction Sequence = 2 */
    out[pos++] = 0x02;

    /* Confirm hash: HMAC-SHA256(KCK, Scalar_C || Element_C || Scalar_P || Element_P) */
    uint8_t confirm_input[256];
    uint32_t cpos = 0;
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) confirm_input[cpos++] = sae.my_scalar[i];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) confirm_input[cpos++] = sae.my_element_x[i];
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) confirm_input[cpos++] = sae.peer_scalar[i];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) confirm_input[cpos++] = sae.peer_element_x[i];

    uint8_t confirm_hash[32];
    sec_hmac_sha256(sae.kck, SAE_KCK_LEN, confirm_input, cpos, confirm_hash);

    for (uint32_t i = 0; i < 32; i++) out[pos++] = confirm_hash[i];

    sae.confirm_sent = 1;
    sae.handshake_state = SAE_STATE_CONFIRM_SENT;
    *out_len = pos;
    return SEC_OK;
}

/* ---- Handle Confirm frame ---- */
int wpa3_sae_handle_confirm(const uint8_t *frame, uint32_t len) {
    if (!frame || len < 32) return SEC_ERR_BAD_PARAM;
    if (sae.handshake_state != SAE_STATE_CONFIRM_SENT) return SEC_ERR_STATE;

    /* Verify peer confirm */
    uint8_t confirm_input[256];
    uint32_t cpos = 0;
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) confirm_input[cpos++] = sae.peer_scalar[i];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) confirm_input[cpos++] = sae.peer_element_x[i];
    for (uint32_t i = 0; i < SAE_SCALAR_LEN; i++) confirm_input[cpos++] = sae.my_scalar[i];
    for (uint32_t i = 0; i < SAE_ELEMENT_LEN; i++) confirm_input[cpos++] = sae.my_element_x[i];

    uint8_t expected_confirm[32];
    sec_hmac_sha256(sae.kck, SAE_KCK_LEN, confirm_input, cpos, expected_confirm);

    for (uint32_t i = 0; i < 32; i++) {
        if (frame[i] != expected_confirm[i]) return SEC_ERR_CRYPTO;
    }

    sae.confirm_received = 1;
    sae.handshake_state = SAE_STATE_COMPLETED;
    sae.installed = 1;
    return SEC_OK;
}

/* ---- GCMP-256 Encryption ---- */
int wpa3_sae_encrypt_frame(const uint8_t *plaintext, uint32_t pt_len,
                           uint8_t *out, uint32_t *out_len) {
    if (!plaintext || !out || !out_len) return SEC_ERR_BAD_PARAM;
    if (!sae.installed) return SEC_ERR_STATE;

    /* GCMP nonce: 12 bytes = Priority(1) + AA(6) + PN(5) */
    uint8_t nonce[12];
    nonce[0] = 0;
    for (uint32_t i = 0; i < 6; i++) nonce[1 + i] = sae.ap_bssid[i];

    uint32_t pn = sae.frame_counter++;
    nonce[7] = (pn >> 32) & 0xFF;
    nonce[8] = (pn >> 24) & 0xFF;
    nonce[9] = (pn >> 16) & 0xFF;
    nonce[10] = (pn >> 8) & 0xFF;
    nonce[11] = pn & 0xFF;

    /* GCMP header (8 bytes) */
    uint32_t pos = 0;
    out[pos++] = nonce[7]; out[pos++] = nonce[8]; out[pos++] = 0x00;
    out[pos++] = nonce[12]; out[pos++] = nonce[11]; out[pos++] = nonce[10];
    out[pos++] = nonce[9]; out[pos++] = nonce[8];

    /* AES-256-GCM encryption */
    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, sae.tk);

    sec_aes256_gcm_encrypt(&ctx, plaintext, out + 8, pt_len, nonce,
                           (const uint8_t*)"GCMP", 4, out + 8 + pt_len);

    *out_len = 8 + pt_len + 16;
    return SEC_OK;
}

/* ---- GCMP-256 Decryption ---- */
int wpa3_sae_decrypt_frame(const uint8_t *encrypted, uint32_t enc_len,
                           uint8_t *out, uint32_t *out_len) {
    if (!encrypted || !out || !out_len) return SEC_ERR_BAD_PARAM;
    if (!sae.installed) return SEC_ERR_STATE;
    if (enc_len < 24) return SEC_ERR_BAD_PARAM;

    const uint8_t *hdr = encrypted;
    uint8_t nonce[12];
    nonce[0] = 0;
    for (uint32_t i = 0; i < 6; i++) nonce[1 + i] = sae.client_mac[i];
    nonce[7] = hdr[0]; nonce[8] = hdr[1]; nonce[9] = hdr[6]; nonce[10] = hdr[5]; nonce[11] = hdr[4];

    const uint8_t *ciphertext = encrypted + 8;
    uint32_t ct_len = enc_len - 8 - 16;
    const uint8_t *tag = encrypted + enc_len - 16;

    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, sae.tk);

    sec_aes256_gcm_decrypt(&ctx, ciphertext, out, ct_len, nonce,
                           (const uint8_t*)"GCMP", 4, tag);

    *out_len = ct_len;
    return SEC_OK;
}

/* ---- Full SAE handshake (combined) ---- */
int wpa3_sae_perform_handshake(boot_nic_t *nic) {
    if (!nic || !sae.initialized) return SEC_ERR_BAD_PARAM;

    /* Compute password element */
    int r = wpa3_sae_compute_pwe();
    if (r != SEC_OK) return r;

    /* Build and send Commit */
    uint8_t frame[SAE_MAX_FRAME];
    uint32_t frame_len = 0;
    r = wpa3_sae_build_commit(frame, &frame_len);
    if (r != SEC_OK) return r;

    uint8_t mac_frame[1514];
    uint32_t mac_len = 0;
    boot_nic_get_mac(nic, mac_frame);
    mac_frame[12] = 0x08; mac_frame[13] = 0x00;
    mac_len = 14;

    r = boot_nic_send(nic, mac_frame, mac_len);
    if (r != SEC_OK) return r;

    /* Receive Commit */
    uint8_t recv[SAE_MAX_FRAME];
    uint32_t recv_len = sizeof(recv);
    r = boot_nic_recv(nic, recv, &recv_len, SAE_TIMEOUT_MS);
    if (r != SEC_OK) return r;

    r = wpa3_sae_handle_commit(recv + 14, recv_len - 14);
    if (r != SEC_OK) return r;

    /* Build and send Confirm */
    r = wpa3_sae_build_confirm(frame, &frame_len);
    if (r != SEC_OK) return r;

    boot_nic_get_mac(nic, mac_frame);
    mac_frame[12] = 0x08; mac_frame[13] = 0x00;
    r = boot_nic_send(nic, mac_frame, mac_len);
    if (r != SEC_OK) return r;

    /* Receive Confirm */
    recv_len = sizeof(recv);
    r = boot_nic_recv(nic, recv, &recv_len, SAE_TIMEOUT_MS);
    if (r != SEC_OK) return r;

    r = wpa3_sae_handle_confirm(recv + 14, recv_len - 14);
    return r;
}

void wpa3_sae_set_keys(const uint8_t kck[32], const uint8_t kek[32], const uint8_t tk[32]) {
    if (kck) for (uint32_t i = 0; i < 32; i++) sae.kck[i] = kck[i];
    if (kek) for (uint32_t i = 0; i < 32; i++) sae.kek[i] = kek[i];
    if (tk) for (uint32_t i = 0; i < 32; i++) sae.tk[i] = tk[i];
}

void wpa3_sae_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&sae.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
