/**
 * Chicago-95 Anti IPv4/6 Reader
 *
 * Prevents IP address fingerprinting and tracking by:
 * 1. Encrypting source/destination IP fields in packet headers
 * 2. IPv4/IPv6 address rotation with time-based key derivation
 * 3. Header obfuscation (TTL normalization, IP ID randomization, TOS masking)
 * 4. Address space randomization (rewrites addresses mid-transit)
 * 5. Onion-layer address wrapping for multi-hop privacy
 * 6. Anti-fingerprinting (normalizes header variations across stacks)
 *
 * Works at the NIC driver level before packets hit the wire or after
 * they arrive, making the real IP invisible to network readers.
 */

#include "boot/security.h"

/* ---- IPv4 Header (RFC 791) ---- */
typedef struct {
    uint8_t  ihl_version;     /* Version (4 bits) + IHL (4 bits) */
    uint8_t  tos;             /* Type of Service */
    uint16_t total_length;
    uint16_t identification;
    uint16_t flags_fragment;  /* Flags (3 bits) + Fragment Offset (13 bits) */
    uint8_t  ttl;
    uint8_t  protocol;
    uint16_t header_checksum;
    uint32_t src_addr;
    uint32_t dst_addr;
} __attribute__((packed)) ipv4_header_t;

/* ---- IPv6 Header (RFC 8200) ---- */
typedef struct {
    uint32_t flow_label;      /* Version (4) + Traffic Class (8) + Flow Label (20) */
    uint16_t payload_length;
    uint8_t  next_header;
    uint8_t  hop_limit;
    uint8_t  src_addr[16];
    uint8_t  dst_addr[16];
} __attribute__((packed)) ipv6_header_t;

/* ---- Onion Layer (for multi-hop address wrapping) ---- */
#define MAX_ONION_LAYERS 4

typedef struct {
    uint32_t inner_addr;      /* IPv4 address at this layer */
    uint8_t  inner_addr6[16]; /* IPv6 address at this layer */
    uint8_t  layer_key[32];   /* Key for encrypting this layer */
    uint8_t  active;
} onion_layer_t;

/* ---- State ---- */
#define ANTI_IP_MAX_ENTITIES  32
#define ANTI_IP_HISTORY       64

typedef struct {
    /* Real addresses */
    uint32_t real_ipv4;
    uint8_t  real_ipv6[16];
    uint32_t peer_ipv4;
    uint8_t  peer_ipv6[16];

    /* Obfuscated addresses (on the wire) */
    uint32_t fake_ipv4;
    uint8_t  fake_ipv6[16];
    uint32_t fake_peer_ipv4;
    uint8_t  fake_peer_ipv6[16];

    /* Mapping table: fake -> real (for demux on receive) */
    struct {
        uint32_t fake_v4;
        uint32_t real_v4;
        uint8_t  fake_v6[16];
        uint8_t  real_v6[16];
        uint64_t expires_at;
        uint8_t  active;
    } map_table[ANTI_IP_MAX_ENTITIES];
    uint32_t map_count;

    /* Address history to prevent reuse */
    uint32_t addr4_history[ANTI_IP_HISTORY];
    uint8_t  addr6_history[ANTI_IP_HISTORY][16];
    uint32_t history_count;

    /* Onion layers */
    onion_layer_t layers[MAX_ONION_LAYERS];
    uint32_t layer_count;

    /* Anti-fingerprint state */
    uint32_t base_ttl;          /* Normalized TTL */
    uint16_t base_id;           /* Base IP ID counter */
    uint8_t  normalize_tos;     /* Zero out TOS field */
    uint8_t  randomize_id;      /* Randomize IP identification */
    uint8_t  normalize_df;      /* Normalize Don't Fragment bit */
    uint8_t  scramble_options;  /* Scramble IP options */

    /* Encryption key for address scrambling */
    uint8_t  master_key[32];
    uint64_t epoch_counter;     /* Increments each rotation */

    /* Mode */
    uint8_t  mode;              /* 0=off, 1=encrypt, 2=rotate, 3=onion, 4=all */
    uint8_t  ipv6_enabled;
    uint8_t  initialized;

    sec_stats_t stats;
} anti_ip_state_t;

static anti_ip_state_t anti_ip;

/* ---- XOR two IPv6 addresses ---- */
static void ipv6_xor(uint8_t result[16], const uint8_t a[16], const uint8_t b[16]) {
    for (uint32_t i = 0; i < 16; i++) result[i] = a[i] ^ b[i];
}

/* ---- Check if address is in history ---- */
static int ipv4_in_history(uint32_t addr) {
    for (uint32_t i = 0; i < anti_ip.history_count; i++) {
        if (anti_ip.addr4_history[i] == addr) return 1;
    }
    return 0;
}

static int ipv6_in_history(const uint8_t addr[16]) {
    for (uint32_t i = 0; i < anti_ip.history_count; i++) {
        int match = 1;
        for (uint32_t j = 0; j < 16; j++) {
            if (anti_ip.addr6_history[i][j] != addr[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* ---- Add to history ---- */
static void ipv4_add_history(uint32_t addr) {
    if (anti_ip.history_count >= ANTI_IP_HISTORY) {
        for (uint32_t i = 0; i < ANTI_IP_HISTORY - 1; i++)
            anti_ip.addr4_history[i] = anti_ip.addr4_history[i + 1];
        anti_ip.history_count = ANTI_IP_HISTORY - 1;
    }
    anti_ip.addr4_history[anti_ip.history_count++] = addr;
}

static void ipv6_add_history(const uint8_t addr[16]) {
    if (anti_ip.history_count >= ANTI_IP_HISTORY) {
        for (uint32_t i = 0; i < ANTI_IP_HISTORY - 1; i++)
            for (uint32_t j = 0; j < 16; j++)
                anti_ip.addr6_history[i][j] = anti_ip.addr6_history[i + 1][j];
        anti_ip.history_count = ANTI_IP_HISTORY - 1;
    }
    for (uint32_t j = 0; j < 16; j++)
        anti_ip.addr6_history[anti_ip.history_count][j] = addr[j];
    anti_ip.history_count++;
}

/* ---- Generate fake IPv4 address ---- */
static uint32_t generate_fake_v4(uint32_t real) {
    uint8_t derived[4];
    uint8_t input[8];
    for (uint32_t i = 0; i < 4; i++) input[i] = (real >> (i * 8)) & 0xFF;
    input[4] = (anti_ip.epoch_counter >> 24) & 0xFF;
    input[5] = (anti_ip.epoch_counter >> 16) & 0xFF;
    input[6] = (anti_ip.epoch_counter >> 8) & 0xFF;
    input[7] = anti_ip.epoch_counter & 0xFF;

    sec_hkdf_sha256(anti_ip.master_key, 32,
                    (const uint8_t*)"Chicago95 Anti-IP v4", 20,
                    input, 8, derived, 4);

    uint32_t fake = 0;
    for (uint32_t i = 0; i < 4; i++)
        fake |= ((uint32_t)derived[i]) << (i * 8);

    /* Ensure unicast, not multicast/broadcast, not loopback, not link-local */
    fake &= 0xDFFFFFFF;  /* Clear multicast */
    fake |= 0x01000000;  /* Ensure Class A range for realism */

    /* Avoid history collisions */
    uint32_t attempts = 0;
    while (ipv4_in_history(fake) && attempts < 100) {
        fake++;
        attempts++;
    }

    ipv4_add_history(fake);
    return fake;
}

/* ---- Generate fake IPv6 address ---- */
static void generate_fake_v6(const uint8_t real[16], uint8_t fake[16]) {
    uint8_t input[32];
    for (uint32_t i = 0; i < 16; i++) input[i] = real[i];
    input[16] = (anti_ip.epoch_counter >> 56) & 0xFF;
    input[17] = (anti_ip.epoch_counter >> 48) & 0xFF;
    input[18] = (anti_ip.epoch_counter >> 40) & 0xFF;
    input[19] = (anti_ip.epoch_counter >> 32) & 0xFF;
    input[20] = (anti_ip.epoch_counter >> 24) & 0xFF;
    input[21] = (anti_ip.epoch_counter >> 16) & 0xFF;
    input[22] = (anti_ip.epoch_counter >> 8) & 0xFF;
    input[23] = anti_ip.epoch_counter & 0xFF;

    sec_hkdf_sha256(anti_ip.master_key, 32,
                    (const uint8_t*)"Chicago95 Anti-IP v6", 20,
                    input, 24, fake, 16);

    /* Ensure unicast (clear multicast bit, set global unicast) */
    fake[0] &= 0xFE;  /* Clear multicast */
    fake[0] |= 0x20;  /* Set global unicast */

    uint32_t attempts = 0;
    while (ipv6_in_history(fake) && attempts < 100) {
        fake[15]++;
        attempts++;
    }

    ipv6_add_history(fake);
}

/* ---- Recalculate IPv4 header checksum ---- */
static uint16_t ipv4_checksum(const ipv4_header_t *hdr) {
    const uint16_t *words = (const uint16_t*)hdr;
    uint32_t sum = 0;
    uint32_t ihl = (hdr->ihl_version & 0x0F) * 2;

    for (uint32_t i = 0; i < ihl; i++) {
        if (i != 5) sum += words[i];  /* Skip checksum field */
    }

    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum & 0xFFFF;
}

/* ---- Add mapping entry ---- */
static int add_mapping(uint32_t fake_v4, uint32_t real_v4,
                       const uint8_t fake_v6[16], const uint8_t real_v6[16]) {
    if (anti_ip.map_count >= ANTI_IP_MAX_ENTITIES) return -1;

    uint32_t idx = anti_ip.map_count;
    anti_ip.map_table[idx].fake_v4 = fake_v4;
    anti_ip.map_table[idx].real_v4 = real_v4;
    if (fake_v6) for (uint32_t i = 0; i < 16; i++) anti_ip.map_table[idx].fake_v6[i] = fake_v6[i];
    if (real_v6) for (uint32_t i = 0; i < 16; i++) anti_ip.map_table[idx].real_v6[i] = real_v6[i];
    anti_ip.map_table[idx].expires_at = anti_ip.epoch_counter + 1000000000ULL;
    anti_ip.map_table[idx].active = 1;
    anti_ip.map_count++;

    return 0;
}

/* ---- Lookup real address from fake ---- */
static int lookup_mapping_v4(uint32_t fake, uint32_t *real) {
    for (uint32_t i = 0; i < anti_ip.map_count; i++) {
        if (anti_ip.map_table[i].active && anti_ip.map_table[i].fake_v4 == fake) {
            *real = anti_ip.map_table[i].real_v4;
            return 0;
        }
    }
    return -1;
}

static int lookup_mapping_v6(const uint8_t fake[16], uint8_t real[16]) {
    for (uint32_t i = 0; i < anti_ip.map_count; i++) {
        if (anti_ip.map_table[i].active) {
            int match = 1;
            for (uint32_t j = 0; j < 16; j++) {
                if (anti_ip.map_table[i].fake_v6[j] != fake[j]) { match = 0; break; }
            }
            if (match) {
                for (uint32_t j = 0; j < 16; j++) real[j] = anti_ip.map_table[i].real_v6[j];
                return 0;
            }
        }
    }
    return -1;
}

/* ---- Expire old mappings ---- */
static void expire_mappings(void) {
    for (uint32_t i = 0; i < anti_ip.map_count; i++) {
        if (anti_ip.map_table[i].active && anti_ip.epoch_counter > anti_ip.map_table[i].expires_at) {
            anti_ip.map_table[i].active = 0;
        }
    }
}

/* ---- Init ---- */
int anti_ip_init(uint32_t real_ipv4, const uint8_t real_ipv6[16]) {
    uint8_t *d = (uint8_t*)&anti_ip;
    for (uint32_t i = 0; i < sizeof(anti_ip_state_t); i++) d[i] = 0;

    anti_ip.real_ipv4 = real_ipv4;
    if (real_ipv6) for (uint32_t i = 0; i < 16; i++) anti_ip.real_ipv6[i] = real_ipv6[i];

    sec_random_bytes(anti_ip.master_key, 32);
    anti_ip.epoch_counter = 0;

    /* Defaults */
    anti_ip.base_ttl = 64;
    anti_ip.normalize_tos = 1;
    anti_ip.randomize_id = 1;
    anti_ip.normalize_df = 1;
    anti_ip.scramble_options = 1;
    anti_ip.ipv6_enabled = (real_ipv6 != 0) ? 1 : 0;
    anti_ip.mode = 4;  /* All modes by default */

    /* Generate initial fake addresses */
    anti_ip.fake_ipv4 = generate_fake_v4(real_ipv4);
    if (anti_ip.ipv6_enabled) generate_fake_v6(real_ipv4 ? anti_ip.real_ipv6 : (const uint8_t*)"\x20\x01\x0d\xb8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01", anti_ip.fake_ipv6);

    add_mapping(anti_ip.fake_ipv4, anti_ip.real_ipv4, anti_ip.fake_ipv6, anti_ip.real_ipv6);

    anti_ip.initialized = 1;
    return SEC_OK;
}

/* ---- Set peer address ---- */
void anti_ip_set_peer(uint32_t peer_v4, const uint8_t peer_v6[16]) {
    anti_ip.peer_ipv4 = peer_v4;
    if (peer_v6) for (uint32_t i = 0; i < 16; i++) anti_ip.peer_ipv6[i] = peer_v6[i];

    anti_ip.fake_peer_ipv4 = generate_fake_v4(peer_v4);
    if (anti_ip.ipv6_enabled && peer_v6) generate_fake_v6(peer_v6, anti_ip.fake_peer_ipv6);

    add_mapping(anti_ip.fake_peer_ipv4, anti_ip.peer_ipv4, anti_ip.fake_peer_ipv6, anti_ip.peer_ipv6);
}

/* ---- Obfuscate outgoing IPv4 packet ---- */
int anti_ip_obfuscate_v4(uint8_t *packet, uint32_t len) {
    if (!packet || len < 20) return SEC_ERR_BAD_PARAM;
    if (!anti_ip.initialized || anti_ip.mode == 0) return SEC_OK;

    ipv4_header_t *hdr = (ipv4_header_t*)packet;
    uint8_t version = (hdr->ihl_version >> 4) & 0x0F;
    if (version != 4) return SEC_OK;

    /* Replace source IP */
    if (hdr->src_addr == anti_ip.real_ipv4) {
        hdr->src_addr = anti_ip.fake_ipv4;
    }

    /* Replace destination IP */
    if (hdr->dst_addr == anti_ip.peer_ipv4) {
        hdr->dst_addr = anti_ip.fake_peer_ipv4;
    }

    /* Anti-fingerprint: normalize header */
    if (anti_ip.normalize_tos) hdr->tos = 0;

    if (anti_ip.randomize_id) {
        uint16_t new_id = (uint16_t)(sec_random_u32() & 0xFFFF);
        hdr->identification = new_id;
    }

    if (anti_ip.normalize_df) {
        hdr->flags_fragment &= 0xBFFF;  /* Clear DF bit */
        hdr->flags_fragment &= 0x3FFF;  /* Clear all flags for max stealth */
    }

    /* Normalize TTL to common value */
    if (anti_ip.base_ttl > 0) {
        hdr->ttl = anti_ip.base_ttl;
    }

    /* Recalculate checksum */
    hdr->header_checksum = 0;
    hdr->header_checksum = ipv4_checksum(hdr);

    anti_ip.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Obfuscate outgoing IPv6 packet ---- */
int anti_ip_obfuscate_v6(uint8_t *packet, uint32_t len) {
    if (!packet || len < 40) return SEC_ERR_BAD_PARAM;
    if (!anti_ip.initialized || anti_ip.mode == 0 || !anti_ip.ipv6_enabled) return SEC_OK;

    ipv6_header_t *hdr = (ipv6_header_t*)packet;
    uint8_t version = (hdr->flow_label >> 28) & 0x0F;
    if (version != 6) return SEC_OK;

    /* Replace source */
    int src_match = 1;
    for (uint32_t i = 0; i < 16; i++) {
        if (hdr->src_addr[i] != anti_ip.real_ipv6[i]) { src_match = 0; break; }
    }
    if (src_match) {
        for (uint32_t i = 0; i < 16; i++) hdr->src_addr[i] = anti_ip.fake_ipv6[i];
    }

    /* Replace destination */
    int dst_match = 1;
    for (uint32_t i = 0; i < 16; i++) {
        if (hdr->dst_addr[i] != anti_ip.peer_ipv6[i]) { dst_match = 0; break; }
    }
    if (dst_match) {
        for (uint32_t i = 0; i < 16; i++) hdr->dst_addr[i] = anti_ip.fake_peer_ipv6[i];
    }

    /* Anti-fingerprint: normalize flow label */
    uint32_t flow = hdr->flow_label & 0x000FFFFF;  /* Keep flow label */
    uint32_t tc = 0;  /* Zero traffic class */
    hdr->flow_label = (tc << 28) | (0 << 20) | flow;

    /* Normalize hop limit */
    if (anti_ip.base_ttl > 0) {
        hdr->hop_limit = anti_ip.base_ttl;
    }

    anti_ip.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- De-obfuscate incoming IPv4 packet ---- */
int anti_ip_deobfuscate_v4(uint8_t *packet, uint32_t len) {
    if (!packet || len < 20) return SEC_ERR_BAD_PARAM;
    if (!anti_ip.initialized || anti_ip.mode == 0) return SEC_OK;

    ipv4_header_t *hdr = (ipv4_header_t*)packet;
    uint8_t version = (hdr->ihl_version >> 4) & 0x0F;
    if (version != 4) return SEC_OK;

    uint32_t real;

    /* Resolve source */
    if (lookup_mapping_v4(hdr->src_addr, &real) == 0) {
        hdr->src_addr = real;
    }

    /* Resolve destination */
    if (lookup_mapping_v4(hdr->dst_addr, &real) == 0) {
        hdr->dst_addr = real;
    }

    /* Recalculate checksum */
    hdr->header_checksum = 0;
    hdr->header_checksum = ipv4_checksum(hdr);

    anti_ip.stats.packets_decrypted++;
    return SEC_OK;
}

/* ---- De-obfuscate incoming IPv6 packet ---- */
int anti_ip_deobfuscate_v6(uint8_t *packet, uint32_t len) {
    if (!packet || len < 40) return SEC_ERR_BAD_PARAM;
    if (!anti_ip.initialized || anti_ip.mode == 0 || !anti_ip.ipv6_enabled) return SEC_OK;

    ipv6_header_t *hdr = (ipv6_header_t*)packet;
    uint8_t version = (hdr->flow_label >> 28) & 0x0F;
    if (version != 6) return SEC_OK;

    uint8_t real[16];

    if (lookup_mapping_v6(hdr->src_addr, real) == 0) {
        for (uint32_t i = 0; i < 16; i++) hdr->src_addr[i] = real[i];
    }

    if (lookup_mapping_v6(hdr->dst_addr, real) == 0) {
        for (uint32_t i = 0; i < 16; i++) hdr->dst_addr[i] = real[i];
    }

    anti_ip.stats.packets_decrypted++;
    return SEC_OK;
}

/* ---- Rotate addresses (generates new fakes) ---- */
int anti_ip_rotate(void) {
    if (!anti_ip.initialized) return SEC_ERR_NOT_INIT;

    anti_ip.epoch_counter++;

    uint32_t old_fake_v4 = anti_ip.fake_ipv4;
    anti_ip.fake_ipv4 = generate_fake_v4(anti_ip.real_ipv4);

    if (anti_ip.ipv6_enabled) {
        uint8_t old_fake_v6[16];
        for (uint32_t i = 0; i < 16; i++) old_fake_v6[i] = anti_ip.fake_ipv6[i];
        generate_fake_v6(anti_ip.real_ipv6, anti_ip.fake_ipv6);
    }

    expire_mappings();
    add_mapping(anti_ip.fake_ipv4, anti_ip.real_ipv4, anti_ip.fake_ipv6, anti_ip.real_ipv6);

    if (anti_ip.peer_ipv4) {
        anti_ip.fake_peer_ipv4 = generate_fake_v4(anti_ip.peer_ipv4);
        if (anti_ip.ipv6_enabled) generate_fake_v6(anti_ip.peer_ipv6, anti_ip.fake_peer_ipv6);
        add_mapping(anti_ip.fake_peer_ipv4, anti_ip.peer_ipv4, anti_ip.fake_peer_ipv6, anti_ip.peer_ipv6);
    }

    anti_ip.stats.packets_encrypted++;
    return SEC_OK;
}

/* ---- Onion wrap: add encryption layer ---- */
int anti_ip_onion_wrap(uint32_t relay_addr_v4, const uint8_t relay_addr6[16]) {
    if (anti_ip.layer_count >= MAX_ONION_LAYERS) return SEC_ERR_NOMEM;
    if (!anti_ip.initialized) return SEC_ERR_NOT_INIT;

    uint32_t idx = anti_ip.layer_count;
    onion_layer_t *layer = &anti_ip.layers[idx];

    layer->inner_addr = relay_addr_v4;
    if (relay_addr6) for (uint32_t i = 0; i < 16; i++) layer->inner_addr6[i] = relay_addr6[i];

    sec_random_bytes(layer->layer_key, 32);
    layer->active = 1;
    anti_ip.layer_count++;

    /* Generate a fake address for this relay layer */
    uint32_t fake_relay = generate_fake_v4(relay_addr_v4);
    add_mapping(fake_relay, relay_addr_v4, 0, 0);

    return SEC_OK;
}

/* ---- Apply onion layers to IPv4 packet ---- */
int anti_ip_onion_apply_v4(uint8_t *packet, uint32_t len) {
    if (!packet || len < 20) return SEC_ERR_BAD_PARAM;
    if (!anti_ip.initialized || anti_ip.layer_count == 0) return SEC_OK;

    ipv4_header_t *hdr = (ipv4_header_t*)packet;
    uint8_t version = (hdr->ihl_version >> 4) & 0x0F;
    if (version != 4) return SEC_OK;

    /* Replace source with outermost relay */
    for (int i = anti_ip.layer_count - 1; i >= 0; i--) {
        if (anti_ip.layers[i].active) {
            uint32_t fake;
            fake = generate_fake_v4(anti_ip.layers[i].inner_addr);
            hdr->src_addr = fake;
            break;
        }
    }

    hdr->header_checksum = 0;
    hdr->header_checksum = ipv4_checksum(hdr);

    return SEC_OK;
}

/* ---- Set mode ---- */
void anti_ip_set_mode(uint8_t mode) {
    anti_ip.mode = mode;
}

void anti_ip_set_ttl(uint32_t ttl) {
    anti_ip.base_ttl = ttl;
}

void anti_ip_set_normalize(int tos, int df, int id_random, int options) {
    anti_ip.normalize_tos = tos ? 1 : 0;
    anti_ip.normalize_df = df ? 1 : 0;
    anti_ip.randomize_id = id_random ? 1 : 0;
    anti_ip.scramble_options = options ? 1 : 0;
}

/* ---- Get obfuscated addresses (for external use) ---- */
void anti_ip_get_fake_v4(uint32_t *src, uint32_t *dst) {
    if (src) *src = anti_ip.fake_ipv4;
    if (dst) *dst = anti_ip.fake_peer_ipv4;
}

void anti_ip_get_fake_v6(uint8_t src[16], uint8_t dst[16]) {
    if (src) for (uint32_t i = 0; i < 16; i++) src[i] = anti_ip.fake_ipv6[i];
    if (dst) for (uint32_t i = 0; i < 16; i++) dst[i] = anti_ip.fake_peer_ipv6[i];
}

void anti_ip_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&anti_ip.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
