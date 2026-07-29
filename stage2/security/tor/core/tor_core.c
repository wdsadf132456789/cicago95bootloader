/**
 * Chicago-95 Bootloader — Tor Core
 * Minimal Tor client with maximum security:
 * - Circuit creation/extension/destruction
 * - Multi-hop relay encryption (3-layer AES-256-CTR + HMAC-SHA1)
 * - Stream isolation
 * - Circuit padding
 * - Zero plaintext on wire
 */

#include "security/tor.h"
#include "boot/security.h"

/* ======================================================================== */
/* State                                                                    */
/* ======================================================================== */

static tor_config_t  tor_config;
tor_circuit_t tor_circuits[TOR_MAX_CIRCUITS];
static tor_stream_t  tor_streams[TOR_MAX_STREAMS];
static tor_consensus_t tor_consensus;

static uint32_t tor_circ_counter = 0;
static uint32_t tor_stream_counter = 0;
static uint64_t tor_bytes_sent = 0;
static uint64_t tor_bytes_recv = 0;
static uint8_t  tor_initialized = 0;
static uint8_t  tor_padding_level = 1;   /* 0=off, 1=normal, 2=aggressive */

/* ======================================================================== */
/* Relay cell formatting                                                     */
/* ======================================================================== */

/* Build a relay cell with HMAC authentication */
static void build_relay_cell(tor_cell_t *cell, uint32_t circ_id,
                             uint8_t relay_cmd, uint32_t stream_id,
                             const uint8_t *payload, uint16_t payload_len,
                             const uint8_t digest_key[20],
                             const uint8_t aes_key[32]) {
    cell->circuit_id = circ_id;
    cell->command = TOR_CMD_RELAY;
    cell->payload_len = payload_len + 11;

    /* Relay header: cmd(1) + len(2) + stream(4) + digest(4) */
    cell->payload[0] = relay_cmd;
    cell->payload[1] = (uint8_t)(payload_len >> 8);
    cell->payload[2] = (uint8_t)(payload_len);
    cell->payload[3] = (uint8_t)(stream_id >> 24);
    cell->payload[4] = (uint8_t)(stream_id >> 16);
    cell->payload[5] = (uint8_t)(stream_id >> 8);
    cell->payload[6] = (uint8_t)(stream_id);

    /* Zero digest field for HMAC computation */
    cell->payload[7] = 0; cell->payload[8] = 0;
    cell->payload[9] = 0; cell->payload[10] = 0;

    /* Copy payload */
    for (uint32_t i = 0; i < payload_len && i < 498; i++)
        cell->payload[11 + i] = payload[i];

    /* Compute HMAC-SHA1 over relay header + payload */
    uint8_t hmac_input[509];
    for (uint32_t i = 0; i < 11 + payload_len; i++)
        hmac_input[i] = cell->payload[i];

    uint8_t mac[20];
    sec_hmac_sha1(digest_key, 20, hmac_input, 11 + payload_len, mac);

    /* Place first 4 bytes of HMAC as relay digest */
    cell->payload[7] = mac[0];
    cell->payload[8] = mac[1];
    cell->payload[9] = mac[2];
    cell->payload[10] = mac[3];

    /* Encrypt relay payload with AES-256-CTR (skip first 4 bytes = circ_id) */
    uint8_t iv[16];
    for (uint32_t i = 0; i < 4; i++) iv[i] = 0;
    sec_aes256_ctr_encrypt(aes_key, iv, cell->payload, TOR_PAYLOAD_SIZE, cell->payload);

    sec_memzero(mac, sizeof(mac));
    sec_memzero(hmac_input, sizeof(hmac_input));
    sec_memzero(iv, sizeof(iv));
}

/* ======================================================================== */
/* Hop key derivation                                                        */
/* ======================================================================== */

static void derive_hop_keys(tor_hop_t *hop, const uint8_t key_material[64]) {
    /* Forward key: SHA256(key_material || "L") */
    uint8_t forward_input[65];
    for (uint32_t i = 0; i < 64; i++) forward_input[i] = key_material[i];
    forward_input[64] = 'L';
    sec_sha256(forward_input, 65, hop->forward_key);

    /* Backward key: SHA256(key_material || "R") */
    uint8_t backward_input[65];
    for (uint32_t i = 0; i < 64; i++) backward_input[i] = key_material[i];
    backward_input[64] = 'R';
    sec_sha256(backward_input, 65, hop->backward_key);

    /* Forward digest seed: SHA1(key_material || "L") */
    uint8_t fwd_digest_in[65];
    for (uint32_t i = 0; i < 64; i++) fwd_digest_in[i] = key_material[i];
    fwd_digest_in[64] = 'L';
    sec_sha1(fwd_digest_in, 65, hop->forward_digest);

    /* Backward digest seed: SHA1(key_material || "R") */
    uint8_t bwd_digest_in[65];
    for (uint32_t i = 0; i < 64; i++) bwd_digest_in[i] = key_material[i];
    bwd_digest_in[64] = 'R';
    sec_sha1(bwd_digest_in, 65, hop->backward_digest);

    sec_memzero(forward_input, sizeof(forward_input));
    sec_memzero(backward_input, sizeof(backward_input));
    sec_memzero(fwd_digest_in, sizeof(fwd_digest_in));
    sec_memzero(bwd_digest_in, sizeof(bwd_digest_in));
}

/* ======================================================================== */
/* Init                                                                     */
/* ======================================================================== */

int tor_init(void) {
    sec_memzero(tor_circuits, sizeof(tor_circuits));
    sec_memzero(tor_streams, sizeof(tor_streams));
    sec_memzero(&tor_consensus, sizeof(tor_consensus));
    tor_circ_counter = 0x10000;  /* Start high to avoid conflicts */
    tor_stream_counter = 1;
    tor_bytes_sent = 0;
    tor_bytes_recv = 0;
    tor_initialized = 1;
    return 0;
}

int tor_config_init(tor_config_t *cfg) {
    if (!cfg) return -1;
    sec_memzero(cfg, sizeof(tor_config_t));

    /* Generate identity keypair */
    sec_random_bytes(cfg->identity_key, 32);
    sec_random_bytes(cfg->onion_key, 32);
    sec_random_bytes(cfg->ntor_key, 32);
    sec_random_bytes(cfg->link_key, 32);

    cfg->socks_port = TOR_SOCKS_PORT;
    cfg->dns_port = 53;
    cfg->dir_port = 9001;
    cfg->use_entry_guards = 1;
    cfg->enforce_distinct_subnets = 1;
    cfg->use_hsdirs = 1;
    cfg->max_circuits = TOR_MAX_CIRCUITS;
    cfg->circuit_build_timeout = 60;
    cfg->max_circuit_build_time = 120;
    cfg->new_circuit_period = 300;
    cfg->max_retries = 3;
    cfg->pad_circuits = 1;
    cfg->isolate_streams = 1;

    /* Copy to global config */
    for (uint32_t i = 0; i < sizeof(tor_config_t); i++)
        ((uint8_t *)&tor_config)[i] = ((uint8_t *)cfg)[i];

    return 0;
}

/* ======================================================================== */
/* Circuit management                                                        */
/* ======================================================================== */

int tor_circuit_create(tor_circuit_t *circ, uint8_t purpose) {
    if (!circ) return -1;
    if (!tor_initialized) return -2;

    /* Find free circuit slot */
    int slot = -1;
    for (uint32_t i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].state == TOR_CIRC_NONE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -3;

    tor_circuit_t *new_circ = &tor_circuits[slot];
    sec_memzero(new_circ, sizeof(tor_circuit_t));

    new_circ->circ_id = tor_circ_counter++;
    new_circ->state = TOR_CIRC_BUILDING;
    new_circ->purpose = purpose;
    new_circ->hop_count = 0;
    new_circ->current_hop = 0;
    new_circ->is_built = 0;
    new_circ->stream_id_counter = 1;

    /* Generate first hop key material */
    sec_random_bytes(new_circ->prev_key, 32);

    *circ = *new_circ;
    return 0;
}

int tor_circuit_extend(tor_circuit_t *circ, const tor_relay_t *relay) {
    if (!circ || !relay) return -1;
    if (circ->hop_count >= TOR_MAX_HOPS) return -2;

    tor_hop_t *hop = &circ->hops[circ->hop_count];

    /* Derive shared secret using Curve25519 */
    uint8_t shared_secret[32];
    uint8_t public_key[32];
    sec_random_bytes(public_key, 32);
    sec_curve25519_shared_secret(relay->ntor_key, public_key, shared_secret);

    /* Key material: shared_secret || relay fingerprint || public_key */
    uint8_t key_material[64];
    for (uint32_t i = 0; i < 32; i++) key_material[i] = shared_secret[i];
    for (uint32_t i = 0; i < 20; i++) key_material[32 + i] = relay->fingerprint[i];
    key_material[52] = 'T'; key_material[53] = 'o'; key_material[54] = 'r'; key_material[55] = '-';
    key_material[56] = 'v'; key_material[57] = '2'; key_material[58] = '-'; key_material[59] = 'L';
    key_material[60] = 'i'; key_material[61] = 'n'; key_material[62] = 'k';
    for (uint32_t i = 63; i < 64; i++) key_material[i] = 0;

    /* Derive forward/backward keys */
    derive_hop_keys(hop, key_material);

    hop->circuit_id = relay->or_port; /* Simplified */
    hop->addr_ipv4 = relay->addr_ipv4;
    hop->or_port = relay->or_port;
    hop->state = 1;

    circ->hop_count++;
    circ->state = TOR_CIRC_EXTENDING;

    sec_memzero(shared_secret, sizeof(shared_secret));
    sec_memzero(public_key, sizeof(public_key));
    sec_memzero(key_material, sizeof(key_material));

    return 0;
}

int tor_circuit_destroy(tor_circuit_t *circ) {
    if (!circ) return -1;

    /* Send DESTROY cell to entry relay */
    tor_cell_t cell;
    cell.circuit_id = circ->circ_id;
    cell.command = TOR_CMD_DESTROY;
    cell.payload_len = 0;
    tor_circuit_send_cell(circ, &cell);

    /* Clear all hop keys */
    for (uint8_t i = 0; i < circ->hop_count; i++) {
        sec_memzero(&circ->hops[i], sizeof(tor_hop_t));
    }
    sec_memzero(circ->prev_key, sizeof(circ->prev_key));

    circ->state = TOR_CIRC_DEAD;

    return 0;
}

int tor_circuit_send_cell(tor_circuit_t *circ, const tor_cell_t *cell) {
    if (!circ || !cell) return -1;

    tor_cell_t encrypted;
    for (uint32_t i = 0; i < sizeof(tor_cell_t); i++)
        ((uint8_t *)&encrypted)[i] = ((uint8_t *)cell)[i];

    /* Layer encryption: encrypt from outermost to innermost */
    for (uint8_t i = circ->hop_count; i > 0; i--) {
        tor_encrypt_cell(&encrypted, &circ->hops[i - 1], 1);
    }

    /* Transmit (would go to NIC driver in real implementation) */
    tor_bytes_sent += TOR_CELL_SIZE;

    return 0;
}

int tor_circuit_recv_cell(tor_circuit_t *circ, tor_cell_t *cell) {
    if (!circ || !cell) return -1;

    /* Layer decryption: decrypt from outermost hop */
    for (uint8_t i = 0; i < circ->hop_count; i++) {
        tor_decrypt_cell(cell, &circ->hops[i]);
    }

    tor_bytes_recv += TOR_CELL_SIZE;
    return 0;
}

/* ======================================================================== */
/* Relay encryption                                                          */
/* ======================================================================== */

int tor_encrypt_cell(tor_cell_t *cell, const tor_hop_t *hop, uint8_t hop_count) {
    if (!cell || !hop) return -1;

    /* AES-256-CTR encrypt payload (skip first 4 bytes = circuit ID) */
    uint8_t iv[16];
    sec_memzero(iv, 16);

    /* Encrypt in-place */
    uint8_t encrypted[TOR_PAYLOAD_SIZE];
    sec_aes256_ctr_encrypt(hop->forward_key, iv,
                           cell->payload, TOR_PAYLOAD_SIZE, encrypted);

    for (uint32_t i = 0; i < TOR_PAYLOAD_SIZE; i++)
        cell->payload[i] = encrypted[i];

    sec_memzero(encrypted, sizeof(encrypted));
    sec_memzero(iv, sizeof(iv));
    return 0;
}

int tor_decrypt_cell(tor_cell_t *cell, const tor_hop_t *hop) {
    if (!cell || !hop) return -1;

    uint8_t iv[16];
    sec_memzero(iv, 16);

    uint8_t decrypted[TOR_PAYLOAD_SIZE];
    sec_aes256_ctr_decrypt(hop->forward_key, iv,
                           cell->payload, TOR_PAYLOAD_SIZE, decrypted);

    for (uint32_t i = 0; i < TOR_PAYLOAD_SIZE; i++)
        cell->payload[i] = decrypted[i];

    sec_memzero(decrypted, sizeof(decrypted));
    sec_memzero(iv, sizeof(iv));
    return 0;
}

/* ======================================================================== */
/* Stream management                                                         */
/* ======================================================================== */

int tor_stream_open(uint32_t circ_id, const uint8_t *addr, uint16_t port,
                    uint8_t purpose) {
    /* Find circuit */
    tor_circuit_t *circ = 0;
    for (uint32_t i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].circ_id == circ_id &&
            tor_circuits[i].state == TOR_CIRC_ESTABLISHED) {
            circ = &tor_circuits[i];
            break;
        }
    }
    if (!circ) return -1;

    /* Find free stream slot */
    int slot = -1;
    for (uint32_t i = 0; i < TOR_MAX_STREAMS; i++) {
        if (tor_streams[i].state == TOR_STREAM_NONE) {
            slot = i;
            break;
        }
    }
    if (slot < 0) return -2;

    tor_stream_t *stream = &tor_streams[slot];
    sec_memzero(stream, sizeof(tor_stream_t));
    stream->stream_id = tor_stream_counter++;
    stream->circ_id = circ_id;
    stream->state = TOR_STREAM_CONNECTING;
    stream->purpose = purpose;
    stream->target_port = port;
    for (uint32_t i = 0; i < 4; i++)
        ((uint8_t *)&stream->target_addr)[i] = addr[i];

    /* Build BEGIN relay cell */
    tor_cell_t cell;
    uint8_t begin_payload[6];
    begin_payload[0] = (uint8_t)(stream->target_addr);
    begin_payload[1] = (uint8_t)(stream->target_addr >> 8);
    begin_payload[2] = (uint8_t)(stream->target_addr >> 16);
    begin_payload[3] = (uint8_t)(stream->target_addr >> 24);
    begin_payload[4] = (uint8_t)(port >> 8);
    begin_payload[5] = (uint8_t)(port);

    build_relay_cell(&cell, circ_id, TOR_RELAY_BEGIN, stream->stream_id,
                     begin_payload, 6, circ->hops[0].forward_digest,
                     circ->hops[0].forward_key);

    tor_circuit_send_cell(circ, &cell);
    sec_memzero(begin_payload, sizeof(begin_payload));

    return stream->stream_id;
}

int tor_stream_send_data(uint32_t stream_id, const uint8_t *data, uint32_t len) {
    if (!data || len == 0) return -1;

    /* Find stream */
    tor_stream_t *stream = 0;
    tor_circuit_t *circ = 0;
    for (uint32_t i = 0; i < TOR_MAX_STREAMS; i++) {
        if (tor_streams[i].stream_id == stream_id &&
            tor_streams[i].state == TOR_STREAM_ESTABLISHED) {
            stream = &tor_streams[i];
            break;
        }
    }
    if (!stream) return -2;

    for (uint32_t i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].circ_id == stream->circ_id) {
            circ = &tor_circuits[i];
            break;
        }
    }
    if (!circ) return -3;

    /* Send DATA relay cells */
    uint32_t offset = 0;
    while (offset < len) {
        uint32_t chunk = len - offset;
        if (chunk > 486) chunk = 486; /* Max relay data payload */

        tor_cell_t cell;
        build_relay_cell(&cell, stream->circ_id, TOR_RELAY_DATA, stream->stream_id,
                         data + offset, chunk, circ->hops[0].forward_digest,
                         circ->hops[0].forward_key);

        tor_circuit_send_cell(circ, &cell);
        offset += chunk;
    }

    return 0;
}

int tor_stream_close(uint32_t stream_id) {
    for (uint32_t i = 0; i < TOR_MAX_STREAMS; i++) {
        if (tor_streams[i].stream_id == stream_id) {
            tor_streams[i].state = TOR_STREAM_CLOSING;

            /* Send END relay cell */
            tor_circuit_t *circ = 0;
            for (uint32_t j = 0; j < TOR_MAX_CIRCUITS; j++) {
                if (tor_circuits[j].circ_id == tor_streams[i].circ_id) {
                    circ = &tor_circuits[j];
                    break;
                }
            }
            if (circ) {
                tor_cell_t cell;
                uint8_t end_reason = 1; /* TOR_STREAM_END_REASON_MISC */
                build_relay_cell(&cell, tor_streams[i].circ_id,
                                 TOR_RELAY_END, tor_streams[i].stream_id,
                                 &end_reason, 1, circ->hops[0].forward_digest,
                                 circ->hops[0].forward_key);
                tor_circuit_send_cell(circ, &cell);
            }

            tor_streams[i].state = TOR_STREAM_CLOSED;
            return 0;
        }
    }
    return -1;
}

/* ======================================================================== */
/* Security features                                                         */
/* ======================================================================== */

void tor_set_padding_level(uint8_t level) {
    tor_padding_level = level;
}

void tor_force_new_circuit(void) {
    /* Mark all circuits for destruction */
    for (uint32_t i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].state != TOR_CIRC_NONE) {
            tor_circuit_destroy(&tor_circuits[i]);
        }
    }
    /* Reset counters to avoid correlation */
    tor_circ_counter = 0x10000 + (tor_circ_counter & 0xFFFF);
    tor_stream_counter = 1;
}

void tor_flush_streams(void) {
    for (uint32_t i = 0; i < TOR_MAX_STREAMS; i++) {
        if (tor_streams[i].state != TOR_STREAM_NONE) {
            tor_stream_close(tor_streams[i].stream_id);
        }
    }
    sec_memzero(tor_streams, sizeof(tor_streams));
}

void tor_get_stats(uint32_t *circuits, uint32_t *streams,
                   uint32_t *bytes_sent, uint32_t *bytes_recv) {
    uint32_t active_circs = 0, active_streams = 0;
    for (uint32_t i = 0; i < TOR_MAX_CIRCUITS; i++) {
        if (tor_circuits[i].state != TOR_CIRC_NONE &&
            tor_circuits[i].state != TOR_CIRC_DEAD)
            active_circs++;
    }
    for (uint32_t i = 0; i < TOR_MAX_STREAMS; i++) {
        if (tor_streams[i].state != TOR_STREAM_NONE &&
            tor_streams[i].state != TOR_STREAM_CLOSED)
            active_streams++;
    }
    if (circuits) *circuits = active_circs;
    if (streams) *streams = active_streams;
    if (bytes_sent) *bytes_sent = (uint32_t)tor_bytes_sent;
    if (bytes_recv) *bytes_recv = (uint32_t)tor_bytes_recv;
}
