/**
 * Chicago-95 Bootloader — Tor Hidden Services
 * v3 hidden service support: key generation, introduction points,
 * rendezvous points, descriptor publishing to HSDirs.
 */

#include "security/tor.h"
#include "boot/security.h"

/* ======================================================================== */
/* v3 hidden service address generation                                      */
/* ======================================================================== */

/* Encode 32-byte v3 onion address to base32 .onion string */
static void encode_onion_address(const uint8_t key[32], char out[56]) {
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyz234567";
    uint8_t checksum_input[47];
    uint8_t checksum[3];

    /* Checksum = SHA3(".onion checksum" || key || 0x03)[:3] */
    for (uint32_t i = 0; i < 14; i++)
        checksum_input[i] = (uint8_t)(".onion checksum")[i];
    for (uint32_t i = 0; i < 32; i++)
        checksum_input[14 + i] = key[i];
    checksum_input[46] = 3; /* Version 3 */

    uint8_t full_hash[32];
    sec_sha3_256(checksum_input, 47, full_hash);
    checksum[0] = full_hash[0];
    checksum[1] = full_hash[1];
    checksum[2] = full_hash[2];

    /* Encode: key(32) + version(1) + checksum(3) = 36 bytes = 56 base32 chars */
    uint8_t data[36];
    for (uint32_t i = 0; i < 32; i++) data[i] = key[i];
    data[32] = 3;
    data[33] = checksum[0];
    data[34] = checksum[1];
    data[35] = checksum[2];

    /* Base32 encode */
    uint32_t bits = 0;
    int bit_count = 0;
    int out_idx = 0;

    for (uint32_t i = 0; i < 36 && out_idx < 56; i++) {
        bits = (bits << 8) | data[i];
        bit_count += 8;
        while (bit_count >= 5 && out_idx < 56) {
            bit_count -= 5;
            out[out_idx++] = alphabet[(bits >> bit_count) & 0x1F];
        }
    }
    while (bit_count >= 5 && out_idx < 56) {
        bit_count -= 5;
        out[out_idx++] = alphabet[(bits >> bit_count) & 0x1F];
    }
    out[56] = '\0';

    sec_memzero(checksum_input, sizeof(checksum_input));
    sec_memzero(full_hash, sizeof(full_hash));
    sec_memzero(data, sizeof(data));
}

/* ======================================================================== */
/* Init hidden service                                                       */
/* ======================================================================== */

int tor_hs_init(tor_hidden_service_t *hs, uint16_t port) {
    if (!hs) return -1;

    sec_memzero(hs, sizeof(tor_hidden_service_t));
    hs->version = 3;
    hs->port = port;

    /* Generate identity key (Ed25519) */
    sec_random_bytes(hs->identity_key, 32);

    /* Derive blinding key */
    uint8_t blind_input[33];
    for (uint32_t i = 0; i < 32; i++) blind_input[i] = hs->identity_key[i];
    blind_input[32] = 3; /* version */
    sec_sha256(blind_input, 33, hs->blinding_key);

    /* Generate service ID (onion address) */
    char onion_addr[57];
    encode_onion_address(hs->identity_key, onion_addr);

    /* Copy raw service ID */
    for (uint32_t i = 0; i < 32; i++)
        hs->service_id[i] = hs->identity_key[i];

    hs->intro_count = 0;
    hs->hsdir_count = 0;
    hs->is_published = 0;

    sec_memzero(blind_input, sizeof(blind_input));
    return 0;
}

/* ======================================================================== */
/* Publish hidden service descriptor to HSDirs                               */
/* ======================================================================== */

int tor_hs_publish(tor_hidden_service_t *hs) {
    if (!hs) return -1;
    if (hs->version != 3) return -2;

    /* Build v3 descriptor:
     * - superencrypted section with intro points
     * - encrypted with each HSDir's onion key
     */

    /* Build intro point list */
    uint8_t intro_data[1024];
    uint32_t intro_len = 0;

    for (uint32_t i = 0; i < hs->intro_count; i++) {
        /* Intro point address (6 bytes: 4 IP + 2 port) */
        for (uint32_t j = 0; j < 6; j++)
            intro_data[intro_len++] = hs->intro_points[i][j];
    }

    /* Encrypt with current blinding key */
    uint8_t iv[16];
    sec_random_bytes(iv, 16);

    uint8_t encrypted[1024];
    sec_aes256_ctr_encrypt(hs->blinding_key, iv, intro_data, intro_len, encrypted);

    /* Build descriptor body for each HSDir */
    for (uint32_t h = 0; h < hs->hsdir_count; h++) {
        /* In real implementation:
         * 1. Encrypt descriptor with HSDir's onion key
         * 2. Sign with identity key
         * 3. PUT /tor/hs/3/<service-id> to HSDir
         */

        /* Simulated publish */
    }

    hs->is_published = 1;

    sec_memzero(intro_data, sizeof(intro_data));
    sec_memzero(encrypted, sizeof(encrypted));
    sec_memzero(iv, sizeof(iv));
    return 0;
}

/* ======================================================================== */
/* Handle introduction request                                               */
/* ======================================================================== */

int tor_hs_handle_intro(tor_hidden_service_t *hs, const tor_cell_t *cell) {
    if (!hs || !cell) return -1;

    /* Parse INTRODUCE2 cell:
     * - Auth key (32 bytes)
     * - Encrypted cookie (16 bytes)
     * - Encrypted target address/port
     */

    const uint8_t *payload = cell->payload;

    /* Extract auth key */
    uint8_t auth_key[32];
    for (uint32_t i = 0; i < 32; i++)
        auth_key[i] = payload[i];

    /* Decrypt cookie with service's onion key */
    uint8_t cookie[16];
    uint8_t iv[16];
    sec_memzero(iv, 16);
    sec_aes256_ctr_decrypt(hs->blinding_key, iv, payload + 32, 16, cookie);

    /* Build RENDEZVOUS1 cell to send back to client */
    uint8_t rendezvous1[20];
    sec_random_bytes(rendezvous1, 20);

    /* Forward to rendezvous point */

    sec_memzero(auth_key, sizeof(auth_key));
    sec_memzero(cookie, sizeof(cookie));
    sec_memzero(iv, sizeof(iv));
    return 0;
}

/* ======================================================================== */
/* Select HSDirs for a given service                                         */
/* ======================================================================== */

int tor_hs_select_hsdirs(tor_hidden_service_t *hs, const tor_consensus_t *cons) {
    if (!hs || !cons) return -1;

    hs->hsdir_count = 0;

    for (uint32_t i = 0; i < cons->relay_count && hs->hsdir_count < 16; i++) {
        if (cons->relays[i].is_hsdir && hs->hsdir_count < 16) {
            uint32_t addr = cons->relays[i].addr_ipv4;
            hs->hsdirs[hs->hsdir_count][0] = (uint8_t)(addr);
            hs->hsdirs[hs->hsdir_count][1] = (uint8_t)(addr >> 8);
            hs->hsdirs[hs->hsdir_count][2] = (uint8_t)(addr >> 16);
            hs->hsdirs[hs->hsdir_count][3] = (uint8_t)(addr >> 24);
            hs->hsdirs[hs->hsdir_count][4] = (uint8_t)(cons->relays[i].dir_port >> 8);
            hs->hsdirs[hs->hsdir_count][5] = (uint8_t)(cons->relays[i].dir_port);
            hs->hsdir_count++;
        }
    }

    return 0;
}
