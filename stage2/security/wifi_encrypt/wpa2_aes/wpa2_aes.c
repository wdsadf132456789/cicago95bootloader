/**
 * Chicago-95 WiFi Encrypter #1: WPA2-AES-CCMP (802.11i)
 * 4-Way Handshake + AES-128-CCMP encryption
 */

#include "boot/security.h"

#define WPA2_PMK_LEN      32
#define WPA2_PTK_LEN       38
#define WPA2_NONCE_LEN     32
#define WPA2_EAPOL_HDR_LEN 99
#define WPA2_TIMEOUT_MS    5000
#define WPA2_MAX_FRAME     2346

/* 802.1X EAPOL Key frame descriptor */
typedef struct {
    uint8_t  descriptor_type;
    uint16_t key_info;
    uint8_t  key_len[2];
    uint8_t  key_replay[8];
    uint8_t  key_nonce[WPA2_NONCE_LEN];
    uint8_t  key_iv[16];
    uint8_t  key_rsc[8];
    uint8_t  key_id[8];
    uint8_t  key_mic[16];
    uint16_t key_data_len;
} __attribute__((packed)) eapol_key_frame_t;

typedef struct {
    /* AP info */
    uint8_t  ap_bssid[6];
    uint8_t  ap_ssid[32];
    uint32_t ap_ssid_len;
    uint16_t ap_capabilities;

    /* Client */
    uint8_t  client_mac[6];
    uint8_t  pmk[WPA2_PMK_LEN];      /* Pairwise Master Key */

    /* Key hierarchy */
    uint8_t  ptk[WPA2_PTK_LEN];       /* Pairwise Transient Key */
    uint8_t  gtk[32];                  /* Group Temporal Key */
    uint32_t gtk_len;

    /* Nonces */
    uint8_t  anonce[WPA2_NONCE_LEN];  /* AP nonce */
    uint8_t  snonce[WPA2_NONCE_LEN];  /* Station nonce */

    /* State machine */
    uint32_t handshake_state;
    uint32_t replay_counter;
    uint8_t  installed;
    uint8_t  associated;

    /* Session */
    uint32_t session_id;
    sec_stats_t stats;
    uint8_t  initialized;
} wpa2_state_t;

static wpa2_state_t wpa2;

/* ---- 4-Way Handshake state machine ---- */
#define WPA2_STATE_DISCONNECTED 0
#define WPA2_STATE_ASSOCIATED   1
#define WPA2_STATE_MSG1_RCVD    2
#define WPA2_STATE_MSG2_SENT    3
#define WPA2_STATE_MSG3_RCVD    4
#define WPA2_STATE_MSG4_SENT    5
#define WPA2_STATE_COMPLETED    6

/* ---- PRF-384 (used for PTK derivation) ---- */
static void prf384(const uint8_t key[32], const char *label,
                   const uint8_t *data, uint32_t data_len,
                   uint8_t out[38]) {
    uint8_t input[256];
    uint32_t label_len = 0;
    while (label[label_len]) label_len++;

    /* HMAC-SHA1 iteration: label || data || counter (0, 1, 2) */
    for (uint32_t block = 0; block < 3; block++) {
        uint32_t pos = 0;
        for (uint32_t i = 0; i < label_len; i++) input[pos++] = label[i];
        for (uint32_t i = 0; i < data_len; i++) input[pos++] = data[i];
        input[pos++] = block;

        uint8_t hash[32];
        sec_hmac_sha256(key, 32, input, pos, hash);

        uint32_t copy_len = 13; /* 38 bytes / 3 = 12.67, round up */
        if (block == 2) copy_len = 38 - 26; /* remaining bytes */
        for (uint32_t i = 0; i < copy_len && (block * 13 + i) < 38; i++) {
            out[block * 13 + i] = hash[i];
        }
    }
}

/* ---- KDF for key derivation ---- */
static void wpa2_kdf(const uint8_t pmk[32], const uint8_t *nonce_a,
                     const uint8_t *nonce_b, uint8_t ptk[38]) {
    uint8_t data[12];
    /* Concatenate MAC addresses and nonces */
    for (uint32_t i = 0; i < 6; i++) data[i] = wpa2.client_mac[i];
    for (uint32_t i = 0; i < 6; i++) data[6 + i] = wpa2.ap_bssid[i];

    prf384(pmk, "Pairwise key expansion", data, 12, ptk);
}

/* ---- Init ---- */
int wpa2_aes_init(const uint8_t client_mac[6], const uint8_t ap_bssid[6]) {
    if (!client_mac || !ap_bssid) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&wpa2;
    for (uint32_t i = 0; i < sizeof(wpa2_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) {
        wpa2.client_mac[i] = client_mac[i];
        wpa2.ap_bssid[i] = ap_bssid[i];
    }

    wpa2.handshake_state = WPA2_STATE_DISCONNECTED;
    wpa2.session_id = sec_random_u32();
    wpa2.initialized = 1;
    return SEC_OK;
}

/* ---- Set passphrase (PBKDF2 to derive PMK) ---- */
int wpa2_set_passphrase(const char *passphrase, uint32_t passphrase_len,
                        const uint8_t ssid[32], uint32_t ssid_len) {
    if (!passphrase || passphrase_len == 0) return SEC_ERR_BAD_PARAM;

    /* WPA2-Personal: PMK = PBKDF2(SHA1, passphrase, ssid, 4096, 32) */
    sec_pbkdf2_sha1(passphrase, passphrase_len, ssid, ssid_len, 4096, wpa2.pmk, WPA2_PMK_LEN);
    return SEC_OK;
}

/* ---- Set PMK directly ---- */
void wpa2_set_pmk(const uint8_t pmk[32]) {
    for (uint32_t i = 0; i < 32; i++) wpa2.pmk[i] = pmk[i];
}

/* ---- EAPOL frame construction ---- */
static uint32_t build_eapol_key(uint8_t *out, uint16_t key_desc,
                                uint8_t type, uint16_t key_data_len) {
    uint32_t pos = 0;

    /* Ethernet header */
    out[pos++] = 0xFF; out[pos++] = 0xFF; out[pos++] = 0xFF;
    out[pos++] = 0xFF; out[pos++] = 0xFF; out[pos++] = 0xFF;
    for (uint32_t i = 0; i < 6; i++) out[pos++] = wpa2.client_mac[i];
    out[pos++] = 0x88; out[pos++] = 0x8E; /* 802.1X */

    /* EAPOL header */
    out[pos++] = 0x01; /* Version */
    out[pos++] = 0x03; /* Type: Key */
    uint16_t eapol_len = 95 + key_data_len;
    out[pos++] = (eapol_len >> 8) & 0xFF;
    out[pos++] = eapol_len & 0xFF;

    /* Key descriptor */
    out[pos++] = type; /* Descriptor type: RSN */

    /* Key info */
    uint16_t info = key_desc | 0x0008; /* Key MIC present */
    if (type == 2) info |= 0x0002; /* Pairwise */
    out[pos++] = (info >> 8) & 0xFF;
    out[pos++] = info & 0xFF;

    /* Key length */
    out[pos++] = 0x00; out[pos++] = 0x00;

    /* Key replay counter */
    uint64_t rc = wpa2.replay_counter++;
    for (uint32_t i = 0; i < 8; i++) out[pos++] = (rc >> (56 - i * 8)) & 0xFF;

    /* Key nonce (32 bytes) */
    for (uint32_t i = 0; i < 32; i++) out[pos++] = wpa2.snonce[i];

    /* Key IV (16 bytes) - random */
    sec_random_bytes(out + pos, 16); pos += 16;

    /* Key RSC (8 bytes) - 0 */
    for (uint32_t i = 0; i < 8; i++) out[pos++] = 0;

    /* Key ID (8 bytes) - 0 */
    for (uint32_t i = 0; i < 8; i++) out[pos++] = 0;

    /* Key MIC placeholder (16 bytes) */
    pos += 16;

    /* Key data length */
    out[pos++] = (key_data_len >> 8) & 0xFF;
    out[pos++] = key_data_len & 0xFF;

    return pos;
}

/* ---- Message 1: AP -> STA ---- */
int wpa2_handle_msg1(const uint8_t *eapol, uint32_t len) {
    if (!eapol || len < WPA2_EAPOL_HDR_LEN) return SEC_ERR_BAD_PARAM;
    if (wpa2.handshake_state != WPA2_STATE_ASSOCIATED) return SEC_ERR_STATE;

    /* Extract AP nonce */
    const eapol_key_frame_t *frame = (const eapol_key_frame_t*)(eapol + 22);
    for (uint32_t i = 0; i < WPA2_NONCE_LEN; i++) wpa2.anonce[i] = frame->key_nonce[i];

    wpa2.handshake_state = WPA2_STATE_MSG1_RCVD;
    return SEC_OK;
}

/* ---- Message 2: STA -> AP ---- */
int wpa2_build_msg2(uint8_t *out, uint32_t *out_len) {
    if (!out || !out_len) return SEC_ERR_BAD_PARAM;
    if (wpa2.handshake_state != WPA2_STATE_MSG1_RCVD) return SEC_ERR_STATE;

    /* Derive PTK */
    wpa2_kdf(wpa2.pmk, wpa2.snonce, wpa2.anonce, wpa2.ptk);

    uint32_t pos = build_eapol_key(out, 0x0000, 0x02, 0);

    /* Compute MIC over EAPOL frame (without Ethernet header) */
    uint8_t mic[16];
    sec_hmac_sha256(wpa2.ptk, 16, out + 14, pos - 14, mic);
    for (uint32_t i = 0; i < 16; i++) out[pos - 18 + i] = mic[i]; /* Key MIC field */

    wpa2.handshake_state = WPA2_STATE_MSG2_SENT;
    *out_len = pos;
    wpa2.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Message 3: AP -> STA ---- */
int wpa2_handle_msg3(const uint8_t *eapol, uint32_t len) {
    if (!eapol || len < WPA2_EAPOL_HDR_LEN) return SEC_ERR_BAD_PARAM;
    if (wpa2.handshake_state != WPA2_STATE_MSG2_SENT) return SEC_ERR_STATE;

    /* Verify MIC using PTK */
    const eapol_key_frame_t *frame = (const eapol_key_frame_t*)(eapol + 22);
    uint8_t computed_mic[16];
    sec_hmac_sha256(wpa2.ptk, 16, eapol + 14, len - 14, computed_mic);
    for (uint32_t i = 0; i < 16; i++) {
        if (computed_mic[i] != frame->key_mic[i]) return SEC_ERR_CRYPTO;
    }

    /* Extract GTK from encrypted key data */
    uint8_t key_data[WPA2_MAX_FRAME];
    uint32_t key_data_len = frame->key_data_len;

    /* Decrypt key data with PTK */
    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, wpa2.ptk + 32);
    uint8_t iv[16];
    for (uint32_t i = 0; i < 16; i++) iv[i] = frame->key_iv[i];
    sec_aes256_gcm_decrypt(&ctx, eapol + 22 + sizeof(eapol_key_frame_t),
                           key_data, key_data_len, iv,
                           (const uint8_t*)"EAPOL", 5, frame->key_mic);

    /* Parse key data to extract GTK */
    uint32_t kpos = 0;
    while (kpos + 2 < key_data_len) {
        uint8_t ktype = key_data[kpos++];
        uint8_t klen = key_data[kpos++];
        if (ktype == 0xDD && klen >= 24) { /* GTK */
            /* Skip OUI and type */
            for (uint32_t i = 0; i < 24; i++) kpos++;
            for (uint32_t i = 0; i < klen - 24 && i < 32; i++) {
                wpa2.gtk[i] = key_data[kpos++];
            }
            wpa2.gtk_len = klen - 24;
            break;
        }
        kpos += klen;
    }

    wpa2.handshake_state = WPA2_STATE_MSG3_RCVD;
    return SEC_OK;
}

/* ---- Message 4: STA -> AP ---- */
int wpa2_build_msg4(uint8_t *out, uint32_t *out_len) {
    if (!out || !out_len) return SEC_ERR_BAD_PARAM;
    if (wpa2.handshake_state != WPA2_STATE_MSG3_RCVD) return SEC_ERR_STATE;

    uint32_t pos = build_eapol_key(out, 0x0000, 0x02, 0);

    uint8_t mic[16];
    sec_hmac_sha256(wpa2.ptk, 16, out + 14, pos - 14, mic);
    for (uint32_t i = 0; i < 16; i++) out[pos - 18 + i] = mic[i];

    wpa2.handshake_state = WPA2_STATE_COMPLETED;
    wpa2.installed = 1;
    *out_len = pos;
    return SEC_OK;
}

/* ---- CCMP Encryption ---- */
int wpa2_aes_encrypt_frame(const uint8_t *plaintext, uint32_t pt_len,
                           uint8_t *out, uint32_t *out_len, uint16_t *frame_counter) {
    if (!plaintext || !out || !out_len || !frame_counter) return SEC_ERR_BAD_PARAM;
    if (!wpa2.installed) return SEC_ERR_STATE;

    /* CCMP nonce: Priority(1) + AA(6) + PN(6) */
    uint8_t nonce[13];
    nonce[0] = 0; /* Priority */
    for (uint32_t i = 0; i < 6; i++) nonce[1 + i] = wpa2.ap_bssid[i];
    nonce[7] = (*frame_counter >> 40) & 0xFF;
    nonce[8] = (*frame_counter >> 32) & 0xFF;
    nonce[9] = (*frame_counter >> 24) & 0xFF;
    nonce[10] = (*frame_counter >> 16) & 0xFF;
    nonce[11] = (*frame_counter >> 8) & 0xFF;
    nonce[12] = *frame_counter & 0xFF;
    (*frame_counter)++;

    /* CCMP header (8 bytes) */
    uint32_t pos = 0;
    out[pos++] = nonce[7]; /* PN[0] */
    out[pos++] = nonce[8]; /* PN[1] */
    out[pos++] = 0x00;     /* Reserved */
    out[pos++] = nonce[12]; /* PN[5] */
    out[pos++] = nonce[11]; /* PN[4] */
    out[pos++] = nonce[10]; /* PN[3] */
    out[pos++] = nonce[9];  /* PN[2] */
    out[pos++] = nonce[8];  /* PN[0] again? No - this is the CCMP header */

    /* Encrypt with AES-128-CCMP (using AES-CCM mode) */
    sec_aes128_ccmp_ctx_t ctx;
    sec_aes128_ccmp_init(&ctx, wpa2.ptk + 32, nonce);
    sec_aes128_ccmp_encrypt(&ctx, plaintext, out + 8, pt_len, out + 8 + pt_len);

    *out_len = 8 + pt_len + 16; /* CCMP header + ciphertext + MIC */
    return SEC_OK;
}

/* ---- CCMP Decryption ---- */
int wpa2_aes_decrypt_frame(const uint8_t *encrypted, uint32_t enc_len,
                           uint8_t *out, uint32_t *out_len, uint16_t *frame_counter) {
    if (!encrypted || !out || !out_len || !frame_counter) return SEC_ERR_BAD_PARAM;
    if (!wpa2.installed) return SEC_ERR_STATE;
    if (enc_len < 24) return SEC_ERR_BAD_PARAM; /* 8 CCMP header + 16 MIC minimum */

    /* Parse CCMP header */
    const uint8_t *hdr = encrypted;
    uint8_t pn[6];
    pn[0] = hdr[0]; pn[1] = hdr[1];
    pn[5] = hdr[3]; pn[4] = hdr[4];
    pn[3] = hdr[5]; pn[2] = hdr[6];

    /* Reconstruct nonce */
    uint8_t nonce[13];
    nonce[0] = 0;
    for (uint32_t i = 0; i < 6; i++) nonce[1 + i] = wpa2.client_mac[i];
    nonce[7] = pn[5]; nonce[8] = pn[4]; nonce[9] = pn[3];
    nonce[10] = pn[2]; nonce[11] = pn[1]; nonce[12] = pn[0];

    const uint8_t *ciphertext = encrypted + 8;
    uint32_t ct_len = enc_len - 8 - 16;
    const uint8_t *mic = encrypted + enc_len - 16;

    /* Verify MIC */
    sec_aes128_ccmp_ctx_t ctx;
    sec_aes128_ccmp_init(&ctx, wpa2.ptk + 32, nonce);

    uint8_t computed_mic[16];
    sec_aes128_ccmp_decrypt(&ctx, ciphertext, out, ct_len, computed_mic);
    for (uint32_t i = 0; i < 16; i++) {
        if (computed_mic[i] != mic[i]) return SEC_ERR_CRYPTO;
    }

    *out_len = ct_len;
    (*frame_counter)++;
    return SEC_OK;
}

void wpa2_aes_set_keys(const uint8_t ptk[38], const uint8_t gtk[32], uint32_t gtk_len) {
    if (ptk) for (uint32_t i = 0; i < 38; i++) wpa2.ptk[i] = ptk[i];
    if (gtk) {
        for (uint32_t i = 0; i < gtk_len && i < 32; i++) wpa2.gtk[i] = gtk[i];
        wpa2.gtk_len = gtk_len;
    }
}

void wpa2_aes_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&wpa2.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
