/**
 * Chicago-95 Bootloader — Minimal Tor Browser
 * Maximum-security Tor client running at boot time.
 *
 * Components: circuit builder, relay encryption, directory consensus,
 * SOCKS5 proxy, hidden service support, stream isolation, padding.
 *
 * All crypto uses AES-256-CTR + SHA-384 + Curve25519.
 * No plaintext leaks — every byte is encrypted before leaving NIC.
 */

#ifndef BOOT_TOR_H
#define BOOT_TOR_H

#include <stdint.h>

/* ======================================================================== */
/* Tor cell format (512 bytes, fixed)                                        */
/* ======================================================================== */

#define TOR_CELL_SIZE       512
#define TOR_CELL_HEADER     7      /* circuit_id(4) + cmd(1) + payload_len(2) */
#define TOR_PAYLOAD_SIZE    (TOR_CELL_SIZE - TOR_CELL_HEADER)

/* Cell commands */
#define TOR_CMD_PADDING         0
#define TOR_CMD_CREATE          1
#define TOR_CMD_CREATED         2
#define TOR_CMD_RELAY           3
#define TOR_CMD_DESTROY         4
#define TOR_CMD_CREATE_FAST     5
#define TOR_CMD_CREATED_FAST    6
#define TOR_CMD_NETINFO         8
#define TOR_CMD_PADDING_NEGOTIATE 9
#define TOR_CMD_PADDING_NEGOTIATED 10
#define TOR_CMD_VERSIONS        7
#define TOR_CMDVPADDING         128
#define TOR_CMD_AUTH_CHALLENGE  130
#define TOR_CMD_AUTHENTICATE    131
#define TOR_CMD_AUTHORIZE       132

/* Relay cell commands */
#define TOR_RELAY_BEGIN         1
#define TOR_RELAY_DATA         2
#define TOR_RELAY_END           3
#define TOR_RELAY_CONNECTED     4
#define TOR_RELAY_SENDME        5
#define TOR_RELAY_EXTEND        6
#define TOR_RELAY_EXTENDED      7
#define TOR_RELAY_TRUNCATE      8
#define TOR_RELAY_TRUNCATED     9
#define TOR_RELAY_ROLLBACK     10
#define TOR_RELAY_ESTABLISH_CIRC 11
#define TOR_RELAY_ESTABLISHED   12
#define TOR_RELAY_CREATE_FAST   13
#define TOR_RELAY_CREATED_FAST  14
#define TOR_RELAY_VERSIONS      15
#define TOR_RELAY_EARLY         16
#define TOR_RELAY_DROP          17
#define TOR_RELAY_SESSION_INFO  18
#define TOR_RELAY_CONNECT2      19
#define TOR_RELAY_EXTEND2       20
#define TOR_RELAY_EXTENDED2     21
#define TOR_RELAY_PERSISTENT    22
#define TOR_RELAY_PERSISTENT_FAILED 23
#define TOR_RELAY_ONION_SPEED_TEST 24
#define TOR_RELAY_ONION2        25
#define TOR_RELAY_ONION2_KEY    26
#define TOR_RELAY_ONION2_REND   27
#define TOR_RELAY_ONION2_INTRO  28

#define TOR_CIRCUIT_ID_NONE     0

/* ======================================================================== */
/* Cell structure                                                            */
/* ======================================================================== */

typedef struct {
    uint32_t circuit_id;
    uint8_t  command;
    uint16_t payload_len;
    uint8_t  payload[TOR_PAYLOAD_SIZE];
} __attribute__((packed)) tor_cell_t;

/* ======================================================================== */
/* Circuit states                                                            */
/* ======================================================================== */

#define TOR_CIRC_NONE          0
#define TOR_CIRC_BUILDING      1
#define TOR_CIRC_EXTENDING     2
#define TOR_CIRC_ESTABLISHED   3
#define TOR_CIRC_WAIT_DESPAWN  4
#define TOR_CIRC_DEAD          5

/* Circuit flags */
#define TOR_CIRC_FLAG_IS_HIGH_DEMAND   (1 << 0)
#define TOR_CIRC_FLAG_IS_LOW_LATENCY   (1 << 1)
#define TOR_CIRC_FLAG_IS_INTERNAL      (1 << 2)
#define TOR_CIRC_FLAG_IS_HSDIR         (1 << 3)
#define TOR_CIRC_FLAG_IS_EXIT_OK       (1 << 4)
#define TOR_CIRC_FLAG_IS_ONION_BALANCED (1 << 5)

#define TOR_MAX_HOPS           8
#define TOR_MAX_CIRCUITS       32
#define TOR_MAX_STREAMS        64

/* ======================================================================== */
/* Relay identity (onion key, signing key, ntor key)                         */
/* ======================================================================== */

typedef struct {
    uint8_t  identity_key[32];   /* Ed25519 */
    uint8_t  onion_key[32];      /* Curve25519 */
    uint8_t  ntor_key[32];       /* Curve25519 */
    uint8_t  signing_key[64];    /* Ed25519 extended */
    uint8_t  fingerprint[20];    /* SHA-1 of identity key */
    uint32_t addr_ipv4;
    uint16_t dir_port;
    uint16_t or_port;
    uint16_t exit_port;
    uint8_t  flags;
    uint8_t  is_exit;
    uint8_t  is_guard;
    uint8_t  is_hsdir;
} __attribute__((packed)) tor_relay_t;

/* ======================================================================== */
/* Hop state (single layer of onion)                                         */
/* ======================================================================== */

typedef struct {
    uint8_t  forward_key[32];    /* AES-256-CTR encrypt */
    uint8_t  backward_key[32];   /* AES-256-CTR decrypt */
    uint8_t  forward_digest[20]; /* SHA-1 HMAC */
    uint8_t  backward_digest[20];
    uint8_t  session_key[32];
    uint8_t  session_mac[20];
    uint32_t circuit_id;
    uint32_t addr_ipv4;
    uint16_t or_port;
    uint8_t  relay_early_count;
    uint8_t  state;
} __attribute__((packed)) tor_hop_t;

/* ======================================================================== */
/* Circuit structure                                                         */
/* ======================================================================== */

typedef struct {
    uint32_t circ_id;
    uint8_t  state;
    uint8_t  flags;
    uint8_t  hop_count;
    uint8_t  current_hop;
    tor_hop_t hops[TOR_MAX_HOPS];
    uint8_t  prev_key[32];       /* For extend operations */
    uint32_t stream_id_counter;
    uint8_t  is_built;
    uint8_t  purpose;            /* 0=general, 1=hs_client, 2=hs_service, 3=cached_desc */
} __attribute__((packed)) tor_circuit_t;

/* ======================================================================== */
/* Stream state                                                              */
/* ======================================================================== */

#define TOR_STREAM_NONE          0
#define TOR_STREAM_CONNECTING    1
#define TOR_STREAM_OPEN          2
#define TOR_STREAM_PROXY_WAIT    3
#define TOR_STREAM_ESTABLISHED   4
#define TOR_STREAM_CLOSING       5
#define TOR_STREAM_CLOSED        6

typedef struct {
    uint32_t stream_id;
    uint32_t circ_id;
    uint8_t  state;
    uint8_t  purpose;            /* 0=socks, 1=dir, 2=resolve */
    uint16_t target_port;
    uint32_t target_addr;
    uint8_t  socks_state;
    uint8_t  sendme_pending;
} __attribute__((packed)) tor_stream_t;

/* ======================================================================== */
/* Hidden service structures                                                 */
/* ======================================================================== */

typedef struct {
    uint8_t  version;            /* 3 only */
    uint8_t  identity_key[32];   /* Ed25519 */
    uint8_t  blinding_key[32];   /* For v3 */
    uint8_t  intro_points[16][6];
    uint8_t  hsdirs[16][6];
    uint32_t intro_count;
    uint32_t hsdir_count;
    uint16_t port;
    uint8_t  service_id[32];     /* .onion address raw */
    uint8_t  is_published;
} __attribute__((packed)) tor_hidden_service_t;

/* ======================================================================== */
/* Directory consensus                                                      */
/* ======================================================================== */

typedef struct {
    uint32_t valid_after;
    uint32_t fresh_until;
    uint32_t valid_until;
    uint32_t vote_seconds;
    uint32_t dist_seconds;
    uint32_t client_version;
    uint32_t server_version;
    uint8_t  consensus_digest[32];
    uint32_t relay_count;
    tor_relay_t relays[256];
    uint32_t guard_count;
    uint32_t exit_count;
    uint32_t hsdir_count;
} __attribute__((packed)) tor_consensus_t;

/* ======================================================================== */
/* SOCKS5 proxy state                                                       */
/* ======================================================================== */

#define TOR_SOCKS_PORT 9050
#define TOR_SOCKS_MAX  16

#define SOCKS5_NONE        0
#define SOCKS5_WAIT_AUTH    1
#define SOCKS5_WAIT_CMD     2
#define SOCKS5_CONNECTED    3
#define SOCKS5_ERROR        4

typedef struct {
    uint8_t  state;
    uint8_t  auth_method;
    uint32_t stream_id;
    uint32_t circ_id;
    uint32_t target_addr;
    uint16_t target_port;
    uint8_t  target_name[256];
    uint8_t  name_len;
} __attribute__((packed)) tor_socks5_t;

/* ======================================================================== */
/* Tor client configuration                                                  */
/* ======================================================================== */

typedef struct {
    uint8_t  nickname[16];
    uint8_t  identity_key[32];
    uint8_t  signing_key[64];
    uint8_t  onion_key[32];
    uint8_t  ntor_key[32];
    uint8_t  link_key[32];
    uint32_t socks_port;
    uint32_t dns_port;
    uint32_t dir_port;
    uint8_t  use_entry_guards;
    uint8_t  enforce_distinct_subnets;
    uint8_t  use_hsdirs;
    uint8_t  max_circuits;
    uint16_t circuit_build_timeout;
    uint16_t max_circuit_build_time;
    uint32_t new_circuit_period;
    uint32_t max_retries;
    uint8_t  pad_circuits;
    uint8_t  isolate_streams;
} __attribute__((packed)) tor_config_t;

/* ======================================================================== */
/* API                                                                      */
/* ======================================================================== */

/* Init */
int  tor_init(void);
int  tor_config_init(tor_config_t *cfg);

/* Circuit management */
int  tor_circuit_create(tor_circuit_t *circ, uint8_t purpose);
int  tor_circuit_extend(tor_circuit_t *circ, const tor_relay_t *relay);
int  tor_circuit_destroy(tor_circuit_t *circ);
int  tor_circuit_send_cell(tor_circuit_t *circ, const tor_cell_t *cell);
int  tor_circuit_recv_cell(tor_circuit_t *circ, tor_cell_t *cell);

/* Relay encryption */
int  tor_encrypt_cell(tor_cell_t *cell, const tor_hop_t *hops, uint8_t hop_count);
int  tor_decrypt_cell(tor_cell_t *cell, const tor_hop_t *hop);

/* Directory */
int  tor_directory_fetch_consensus(tor_consensus_t *cons);
int  tor_directory_request_desc(const uint8_t *fingerprint, uint8_t *desc, uint32_t *desc_len);

/* SOCKS5 proxy */
int  tor_socks5_init(void);
int  tor_socks5_accept(tor_socks5_t *conn);
int  tor_socks5_handle(tor_socks5_t *conn, const uint8_t *data, uint32_t len,
                       uint8_t *resp, uint32_t *resp_len);
int  tor_socks5_connect(tor_socks5_t *conn, uint32_t circ_id);

/* Hidden services */
int  tor_hs_init(tor_hidden_service_t *hs, uint16_t port);
int  tor_hs_publish(tor_hidden_service_t *hs);
int  tor_hs_handle_intro(tor_hidden_service_t *hs, const tor_cell_t *cell);

/* Stream management */
int  tor_stream_open(uint32_t circ_id, const uint8_t *addr, uint16_t port, uint8_t purpose);
int  tor_stream_send_data(uint32_t stream_id, const uint8_t *data, uint32_t len);
int  tor_stream_close(uint32_t stream_id);

/* Packet processing (called from NIC RX) */
int  tor_process_incoming(const uint8_t *frame, uint32_t len);

/* Security features */
void tor_set_padding_level(uint8_t level);
void tor_force_new_circuit(void);
void tor_flush_streams(void);

/* Stats */
void tor_get_stats(uint32_t *circuits, uint32_t *streams, uint32_t *bytes_sent, uint32_t *bytes_recv);

/* ---- Bootstrapper ---- */

enum {
    TOR_BOOT_IDLE = 0,
    TOR_BOOT_CONSENSUS,
    TOR_BOOT_GUARD_SELECT,
    TOR_BOOT_CIRCUIT_BUILD,
    TOR_BOOT_CIRCUIT_EXTEND_1,
    TOR_BOOT_CIRCUIT_EXTEND_2,
    TOR_BOOT_CIRCUIT_READY,
    TOR_BOOT_SOCKS5_START,
    TOR_BOOT_HS_PUBLISH,
    TOR_BOOT_READY,
    TOR_BOOT_FAILED
};

typedef struct {
    uint8_t  state;
    uint8_t  initialized;
    uint32_t circuit_id;
    uint8_t  onion_addr[64];
    uint8_t  hs_published;
    uint32_t last_publish;
    uint32_t bytes_sent;
    uint32_t bytes_recv;
    uint8_t  guard_fingerprint[20];
    uint8_t  guard_ip[4];
    uint16_t guard_or_port;
} tor_bootstrap_state_t;

int  tor_bootstrap_init(void);
int  tor_bootstrap_poll(void);
int  tor_bootstrap_is_ready(void);
const uint8_t *tor_bootstrap_get_onion_addr(void);
const tor_bootstrap_state_t *tor_bootstrap_get_state(void);
uint32_t tor_bootstrap_get_circuit_id(void);

#endif /* BOOT_TOR_H */
