/**
 * Chicago-95 Bootloader Security Subsystem
 * 4 Gen-2 Firewalls + 3 DNS Encrypters + 2 WiFi Encrypters + 5 MAC Encrypters + 1 Anti-IP Reader
 *
 * All 15 modules run at boot-time before the kernel loads.
 * They configure hardware NIC filters, encrypt boot network
 * traffic, and enforce security policy before any OS code runs.
 */

#ifndef BOOT_SECURITY_H
#define BOOT_SECURITY_H

#include <stdint.h>

/* ========================================================================
 * Common constants
 * ======================================================================== */
#define SEC_OK                  0
#define SEC_ERR_NOMEM          -1
#define SEC_ERR_BAD_PARAM      -2
#define SEC_ERR_HARDWARE       -3
#define SEC_ERR_TIMEOUT        -4
#define SEC_ERR_AUTH           -5
#define SEC_ERR_CRYPTO         -6
#define SEC_ERR_STATE          -7
#define SEC_ERR_NOT_FOUND      -8
#define SEC_ERR_NOT_INIT       -9

#define SEC_MAX_RULES          512
#define SEC_MAX_KEYS           16
#define SEC_MAX_IFACE          8
#define SEC_MAC_LEN            6
#define SEC_IP_LEN             4
#define SEC_IP6_LEN            16
#define SEC_MAX_PSK_LEN        64
#define SEC_MAX_SSID_LEN       32
#define SEC_MAX_HOSTNAME       256
#define SEC_MAX_CERT_SIZE      4096

/* ========================================================================
 * Network packet representation (used by firewalls)
 * ======================================================================== */
typedef struct {
    uint8_t  src_mac[SEC_MAC_LEN];
    uint8_t  dst_mac[SEC_MAC_LEN];
    uint16_t ethertype;
    uint8_t  vlan_tag[4];
    uint8_t  ip_version;
    uint8_t  ip_proto;        /* TCP=6 UDP=17 ICMP=1 */
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t tcp_flags;       /* SYN ACK FIN RST PSH URG */
    uint32_t payload_len;
    uint8_t  *payload;
    uint8_t  iface_id;
    uint64_t timestamp;
    uint32_t packet_id;
} sec_packet_t;

/* ========================================================================
 * Firewall action codes
 * ======================================================================== */
#define FW_ACTION_ALLOW        0
#define FW_ACTION_DENY         1
#define FW_ACTION_DROP         2
#define FW_ACTION_LOG          3
#define FW_ACTION_RESET        4
#define FW_ACTION_ENCRYPT      5
#define FW_ACTION_QOS          6

/* ========================================================================
 * Firewall rule
 * ======================================================================== */
typedef struct {
    uint32_t rule_id;
    uint8_t  enabled;
    uint8_t  action;          /* FW_ACTION_* */
    uint32_t priority;

    /* Match fields (0 = wildcard) */
    uint8_t  match_src_mac[SEC_MAC_LEN];
    uint8_t  match_dst_mac[SEC_MAC_LEN];
    uint8_t  mac_mask[SEC_MAC_LEN];
    uint32_t src_ip;
    uint32_t dst_ip;
    uint32_t src_mask;
    uint32_t dst_mask;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  protocol;        /* IP protocol */
    uint32_t tcp_flags;
    uint32_t tcp_flags_mask;
    uint8_t  match_iface;

    /* Gen2-specific fields */
    uint8_t  stateful;        /* Track connection state */
    uint32_t max_payload;     /* Max payload size (0=unlimited) */
    uint32_t rate_limit;      /* Packets per second (0=unlimited) */
    uint32_t burst_limit;
    uint8_t  log_packets;     /* Enable logging */
    char     description[64];
} sec_fw_rule_t;

/* ========================================================================
 * Connection state entry (for stateful firewall)
 * ======================================================================== */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint8_t  proto;
    uint8_t  state;           /* TCP state or generic state */
    uint32_t packets_in;
    uint32_t packets_out;
    uint64_t bytes_in;
    uint64_t bytes_out;
    uint64_t created;
    uint64_t last_seen;
    uint8_t  flags;
#define CONN_FLAG_ESTABLISHED  0x01
#define CONN_FLAG_SYN_SEEN     0x02
#define CONN_FLAG_FIN_SEEN     0x04
#define CONN_FLAG_RST_SEEN     0x08
#define CONN_FLAG_ENCRYPTED    0x10
#define CONN_FLAG_LOGGED       0x20
} sec_conn_state_t;

/* ========================================================================
 * Boot-time statistics
 * ======================================================================== */
typedef struct {
    uint64_t packets_inspected;
    uint64_t packets_allowed;
    uint64_t packets_dropped;
    uint64_t packets_blocked;
    uint64_t packets_logged;
    uint64_t packets_encrypted;
    uint64_t connections_tracked;
    uint64_t connections_established;
    uint64_t connections_closed;
    uint64_t rate_limited;
    uint64_t errors;
    uint64_t dns_queries_encrypted;
    uint64_t wifi_frames_encrypted;
    uint64_t packets_handled;
    uint64_t bytes_processed;
    uint64_t connections_opened;
    uint64_t connections_active;
    uint64_t packets_decrypted;
    uint64_t drops;
    uint64_t boot_time_us;
} sec_stats_t;

/* ========================================================================
 * Common crypto primitives (used by all 9 modules)
 * ======================================================================== */

/* AES-256 */
typedef struct {
    uint8_t  round_keys[240];   /* 15 rounds * 16 bytes * 1 extra? No: 14+1=15 rounds, 16 bytes each = 240 */
    uint32_t nr;                /* Number of rounds */
} sec_aes256_ctx_t;

void sec_aes256_init(sec_aes256_ctx_t *ctx, const uint8_t key[32]);
void sec_aes256_encrypt_block(sec_aes256_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void sec_aes256_decrypt_block(sec_aes256_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void sec_aes256_cbc_encrypt(sec_aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out, uint32_t len, const uint8_t iv[16]);
void sec_aes256_cbc_decrypt(sec_aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out, uint32_t len, const uint8_t iv[16]);
void sec_aes256_gcm_encrypt(sec_aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out, uint32_t len,
                            const uint8_t iv[12], const uint8_t *aad, uint32_t aad_len, uint8_t tag[16]);
int  sec_aes256_gcm_decrypt(sec_aes256_ctx_t *ctx, const uint8_t *in, uint8_t *out, uint32_t len,
                            const uint8_t iv[12], const uint8_t *aad, uint32_t aad_len, const uint8_t tag[16]);

/* AES-128-CCMP (WPA2 CCMP mode) */
typedef struct {
    uint8_t  key[16];
    uint8_t  nonce[13];
    uint32_t pos;
} sec_aes128_ccmp_ctx_t;
void sec_aes128_ccmp_init(sec_aes128_ccmp_ctx_t *ctx, const uint8_t key[16], const uint8_t nonce[13]);
void sec_aes128_ccmp_encrypt(sec_aes128_ccmp_ctx_t *ctx, const uint8_t *in, uint8_t *out,
                             uint32_t len, uint8_t *mic);
void sec_aes128_ccmp_decrypt(sec_aes128_ccmp_ctx_t *ctx, const uint8_t *in, uint8_t *out,
                             uint32_t len, uint8_t *mic);

/* ChaCha20-Poly1305 */
typedef struct {
    uint8_t  key_bytes[32];
    uint8_t  nonce_bytes[12];
    uint64_t counter;
} sec_chacha20_ctx_t;

void sec_chacha20_init(sec_chacha20_ctx_t *ctx, const uint8_t key[32], const uint8_t nonce[12], uint64_t counter);
void sec_chacha20_crypt(sec_chacha20_ctx_t *ctx, const uint8_t *in, uint8_t *out, uint32_t len);
void sec_poly1305_key_gen(const uint8_t key[32], const uint8_t nonce[12], uint8_t out[32]);
void sec_poly1305_mac(const uint8_t key[32], const uint8_t *msg, uint32_t msg_len, uint8_t mac[16]);
void sec_chacha20_poly1305_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                   const uint8_t *aad, uint32_t aad_len,
                                   const uint8_t *in, uint32_t in_len,
                                   uint8_t *out, uint8_t tag[16]);
int  sec_chacha20_poly1305_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                   const uint8_t *aad, uint32_t aad_len,
                                   const uint8_t *in, uint32_t in_len,
                                   uint8_t *out, const uint8_t tag[16]);

/* Streaming ChaCha20-Poly1305 AEAD (used by DNSCrypt) */
typedef struct {
    uint8_t  key[32];
    uint8_t  nonce[12];
    uint32_t counter;
    uint8_t  poly_key[32];
    uint8_t  aad_buf[512];
    uint32_t aad_len;
    uint32_t aad_padded_len;
    uint32_t msg_len;
} sec_chacha20_poly1305_ctx_t;
void sec_chacha20_poly1305_init(sec_chacha20_poly1305_ctx_t *ctx,
                                const uint8_t key[32], const uint8_t nonce[12]);
void sec_chacha20_poly1305_set_aad(sec_chacha20_poly1305_ctx_t *ctx,
                                   const uint8_t *aad, uint32_t aad_len);
void sec_chacha20_poly1305_encrypt_stream(sec_chacha20_poly1305_ctx_t *ctx,
                                   const uint8_t *plaintext, uint8_t *ciphertext,
                                   uint32_t len);
void sec_chacha20_poly1305_decrypt_stream(sec_chacha20_poly1305_ctx_t *ctx,
                                   const uint8_t *ciphertext, uint8_t *plaintext,
                                   uint32_t len);
void sec_chacha20_poly1305_final(sec_chacha20_poly1305_ctx_t *ctx, uint8_t tag[16]);

/* Flat one-shot ChaCha20-Poly1305 (uses ctx internally) */
void sec_chacha20_poly1305_flat_encrypt(const uint8_t key[32], const uint8_t nonce[12],
                                        const uint8_t *aad, uint32_t aad_len,
                                        const uint8_t *in, uint32_t in_len,
                                        uint8_t *out, uint8_t tag[16]);
void sec_chacha20_poly1305_flat_decrypt(const uint8_t key[32], const uint8_t nonce[12],
                                        const uint8_t *aad, uint32_t aad_len,
                                        const uint8_t *in, uint32_t in_len,
                                        uint8_t *out, const uint8_t tag[16]);

/* SHA-256 */
typedef struct {
    uint32_t state[8];
    uint64_t total_len;
    uint8_t  buffer[64];
    uint32_t buf_len;
} sec_sha256_ctx_t;

void sec_sha256_init(sec_sha256_ctx_t *ctx);
void sec_sha256_update(sec_sha256_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sec_sha256_final(sec_sha256_ctx_t *ctx, uint8_t out[32]);
void sec_sha256_hash(const uint8_t *data, uint32_t len, uint8_t out[32]);

/* SHA-512 */
typedef struct {
    uint64_t state[8];
    uint64_t total_len;
    uint8_t  buffer[128];
    uint32_t buf_len;
} sec_sha512_ctx_t;

void sec_sha512_init(sec_sha512_ctx_t *ctx);
void sec_sha512_update(sec_sha512_ctx_t *ctx, const uint8_t *data, uint32_t len);
void sec_sha512_final(sec_sha512_ctx_t *ctx, uint8_t out[64]);

/* HMAC */
void sec_hmac_sha256(const uint8_t *key, uint32_t key_len,
                     const uint8_t *msg, uint32_t msg_len, uint8_t out[32]);
void sec_hmac_sha512(const uint8_t key[64], const uint8_t *msg, uint32_t msg_len, uint8_t out[64]);

/* HKDF */
int  sec_hkdf_sha256_extract(const uint8_t *salt, uint32_t salt_len,
                             const uint8_t *ikm, uint32_t ikm_len, uint8_t prk[32]);
void sec_hkdf_sha256_expand(const uint8_t prk[32], uint32_t prk_len,
                            const uint8_t *info, uint32_t info_len,
                            uint8_t *okm, uint32_t okm_len);

/* PBKDF2 */
void sec_pbkdf2_hmac_sha256(const uint8_t *password, uint32_t pass_len,
                            const uint8_t *salt, uint32_t salt_len,
                            uint32_t iterations, uint8_t *out, uint32_t out_len);

/* Random */
void sec_random_bytes(uint8_t *buf, uint32_t len);
uint32_t sec_random_u32(void);

/* Missing crypto primitives */
void sec_memzero(void *ptr, uint32_t len);
void sec_sha1(const uint8_t *data, uint32_t len, uint8_t out[20]);
void sec_sha3_256(const uint8_t *data, uint32_t len, uint8_t out[32]);
void sec_hmac_sha1(const uint8_t *key, uint32_t key_len,
                   const uint8_t *data, uint32_t data_len, uint8_t out[20]);
void sec_aes256_ctr_encrypt(const uint8_t key[32], const uint8_t iv[16],
                            const uint8_t *in, uint32_t len, uint8_t *out);
void sec_aes256_ctr_decrypt(const uint8_t key[32], const uint8_t iv[16],
                            const uint8_t *in, uint32_t len, uint8_t *out);
void sec_curve25519_shared_secret(const uint8_t scalar[32],
                                   const uint8_t point[32], uint8_t out[32]);
void sec_chacha20_encrypt(const uint8_t key[32], uint32_t counter,
                          const uint8_t nonce[12], uint32_t nonce_len,
                          uint8_t *out, uint32_t len);

/* ========================================================================
 * Boot-time PCI NIC register access
 * ======================================================================== */
uint32_t boot_pci_config_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg);
void     boot_pci_config_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t reg, uint32_t val);
uint16_t boot_pci_find_device(uint16_t vendor, uint16_t device);
uint16_t boot_pci_find_class(uint8_t class_code, uint8_t subclass);

/* ========================================================================
 * Boot-time NIC operations
 * ======================================================================== */
#define NIC_MAX_ENTRIES  256

typedef struct {
    uint32_t mmio_base;
    uint16_t io_port;
    uint8_t  mac[SEC_MAC_LEN];
    uint8_t  irq;
    uint8_t  nic_type;      /* 0=Intel e1000, 1=Realtek 8139, 2=AMD PCnet */
    void     *driver_data;
} boot_nic_t;

int  boot_nic_init(boot_nic_t *nic);
int  boot_nic_send(boot_nic_t *nic, const uint8_t *frame, uint32_t len);
int  boot_nic_recv(boot_nic_t *nic, uint8_t *frame, uint32_t *len, uint32_t timeout_ms);
void boot_nic_set_promisc(boot_nic_t *nic, int enable);
void boot_nic_get_mac(boot_nic_t *nic, uint8_t mac[6]);

/* ========================================================================
 * Forward declarations for all 9 security modules
 * ======================================================================== */

/* --- 4 Firewalls Gen-2 --- */
/* FW-1: Packet Filter (L2/L3/L4 static rules) */
int  fw_packet_filter_init(void);
int  fw_packet_filter_add_rule(const sec_fw_rule_t *rule);
int  fw_packet_filter_remove_rule(uint32_t rule_id);
int  fw_packet_filter_eval(const sec_packet_t *pkt);
void fw_packet_filter_flush(void);
void fw_packet_filter_get_stats(sec_stats_t *stats);

/* FW-2: Stateful Inspection (connection tracking) */
int  fw_stateful_init(void);
int  fw_stateful_add_rule(const sec_fw_rule_t *rule);
int  fw_stateful_eval(const sec_packet_t *pkt);
int  fw_stateful_track(const sec_packet_t *pkt);
int  fw_stateful_lookup(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port, uint16_t dst_port, uint8_t proto, sec_conn_state_t **out);
void fw_stateful_timeout_stale(uint64_t max_age_ms);
void fw_stateful_get_stats(sec_stats_t *stats);

/* FW-3: Application Layer (L7 deep packet inspection) */
int  fw_app_layer_init(void);
int  fw_app_layer_add_rule(const sec_fw_rule_t *rule);
int  fw_app_layer_eval(const sec_packet_t *pkt);
int  fw_app_layer_inspect_http(const uint8_t *payload, uint32_t len);
int  fw_app_layer_inspect_dns(const uint8_t *payload, uint32_t len);
int  fw_app_layer_inspect_tls_sni(const uint8_t *payload, uint32_t len, char *hostname, uint32_t hostname_max);
void fw_app_layer_get_stats(sec_stats_t *stats);

/* FW-4: Adaptive (behavioral / anomaly detection) */
int  fw_adaptive_init(void);
int  fw_adaptive_add_rule(const sec_fw_rule_t *rule);
int  fw_adaptive_eval(const sec_packet_t *pkt);
int  fw_adaptive_update_baseline(void);
int  fw_adaptive_detect_anomaly(const sec_packet_t *pkt);
void fw_adaptive_set_threshold(uint32_t param, uint32_t value);
void fw_adaptive_get_stats(sec_stats_t *stats);

/* --- 3 DNS Encrypters --- */
/* DNS-1: DNS over HTTPS (DoH) */
int  dns_doh_init(const char *server_url, const uint8_t ca_cert[], uint32_t ca_cert_len);
int  dns_doh_encrypt_query(const uint8_t *raw_query, uint32_t query_len,
                           uint8_t *out, uint32_t *out_len);
int  dns_doh_decrypt_response(const uint8_t *encrypted, uint32_t encrypted_len,
                              uint8_t *out, uint32_t *out_len);
int  dns_doh_build_request(const char *hostname, uint16_t qtype, uint8_t *out, uint32_t *out_len);
int  dns_doh_send_receive(boot_nic_t *nic, const uint8_t *request, uint32_t req_len,
                          uint8_t *response, uint32_t *resp_len);
void dns_doh_set_server(const char *server_url);
void dns_doh_get_stats(sec_stats_t *stats);

/* DNS-2: DNS over TLS (DoT) */
int  dns_dot_init(const char *server, const uint8_t ca_cert[], uint32_t ca_cert_len);
int  dns_dot_connect(boot_nic_t *nic);
int  dns_dot_encrypt_query(const uint8_t *raw_query, uint32_t query_len,
                           uint8_t *out, uint32_t *out_len);
int  dns_dot_decrypt_response(const uint8_t *encrypted, uint32_t encrypted_len,
                              uint8_t *out, uint32_t *out_len);
int  dns_dot_send_receive(boot_nic_t *nic, const uint8_t *query, uint32_t query_len,
                          uint8_t *response, uint32_t *resp_len);
void dns_dot_disconnect(void);
void dns_dot_get_stats(sec_stats_t *stats);

/* DNS-3: DNSCrypt */
int  dns_crypt_init(const char *provider_name, const uint8_t public_key[32],
                    const uint8_t secret_key[32]);
int  dns_crypt_encrypt_query(const uint8_t *raw_query, uint32_t query_len,
                             uint8_t *out, uint32_t *out_len);
int  dns_crypt_decrypt_response(const uint8_t *encrypted, uint32_t encrypted_len,
                                uint8_t *out, uint32_t *out_len);
int  dns_crypt_send_receive(boot_nic_t *nic, const uint8_t *query, uint32_t query_len,
                            uint8_t *response, uint32_t *resp_len);
int  dns_crypt_refresh_keys(void);
void dns_crypt_get_stats(sec_stats_t *stats);

/* --- 2 WiFi Encrypters --- */
/* WiFi-1: WPA2-AES-CCMP */
int  wpa2_aes_init(const uint8_t *ssid, const uint8_t *psk);
int  wpa2_aes_handshake_4way(boot_nic_t *nic);
int  wpa2_aes_encrypt_frame(const uint8_t *frame, uint32_t frame_len,
                            uint8_t *out, uint32_t *out_len, uint16_t *frame_counter);
int  wpa2_aes_decrypt_frame(const uint8_t *frame, uint32_t frame_len,
                            uint8_t *out, uint32_t *out_len, uint16_t *frame_counter);
int  wpa2_aes_derive_keys(const uint8_t pmk[32], const uint8_t anonce[32],
                          const uint8_t snonce[32], const uint8_t addr1[6],
                          const uint8_t addr2[6], uint8_t ptk[64]);
void wpa2_aes_set_psk(const uint8_t pmk[32]);
void wpa2_aes_get_stats(sec_stats_t *stats);

/* WiFi-2: WPA3-SAE (Simultaneous Authentication of Equals) */
int  wpa3_sae_init(const uint8_t *ssid, const uint8_t *password);
int  wpa3_sae_commit(boot_nic_t *nic, const uint8_t peer_addr[6]);
int  wpa3_sae_confirm(boot_nic_t *nic, const uint8_t peer_addr[6]);
int  wpa3_sae_encrypt_frame(const uint8_t *frame, uint32_t frame_len,
                            uint8_t *out, uint32_t *out_len);
int  wpa3_sae_decrypt_frame(const uint8_t *frame, uint32_t frame_len,
                            uint8_t *out, uint32_t *out_len);
int  wpa3_sae_derive_keys(const uint8_t kck[32], const uint8_t kek[32],
                          const uint8_t tk[32]);
int  wpa3_sae_set_password(const char *password, uint32_t password_len);
void wpa3_sae_get_stats(sec_stats_t *stats);

/* --- 5 MAC Encrypters --- */
/* MAC-1: MAC Randomizer (boot-time random MAC) */
int  mac_random_init(const uint8_t original_mac[6]);
int  mac_random_get(uint8_t out[6]);
int  mac_random_rotate(void);
int  mac_random_apply(boot_nic_t *nic);
int  mac_random_restore(boot_nic_t *nic);
void mac_random_set_preserve(int preserve_unicast, int preserve_locally);
void mac_random_get_stats(sec_stats_t *stats);

/* MAC-2: MAC Cloner (clone from target device) */
int  mac_clone_init(const uint8_t original_mac[6], const uint8_t local_ip[4]);
int  mac_clone_add_target(const uint8_t mac[6], const uint8_t ip[4]);
int  mac_clone_scan(boot_nic_t *nic);
int  mac_clone_select_target(boot_nic_t *nic);
int  mac_clone_apply(boot_nic_t *nic);
int  mac_clone_set_target(boot_nic_t *nic, const uint8_t target_mac[6]);
int  mac_clone_restore(boot_nic_t *nic);
void mac_clone_set_auto_select(int enable);
void mac_clone_get_stats(sec_stats_t *stats);

/* MAC-3: MAC Masker (XOR-based deterministic masking) */
int  mac_mask_init(const uint8_t original_mac[6], const uint8_t key[32]);
int  mac_mask_get(uint8_t out[6]);
int  mac_mask_rotate(const uint8_t new_key[32]);
int  mac_mask_unmask(const uint8_t masked[6], uint8_t original[6]);
int  mac_mask_apply(boot_nic_t *nic);
int  mac_mask_restore(boot_nic_t *nic);
void mac_mask_set_preserve(int preserve_unicast, int preserve_locally);
void mac_mask_get_stats(sec_stats_t *stats);

/* MAC-4: MAC Rotator (time-based rotation) */
int  mac_rot_init(const uint8_t original_mac[6], uint32_t interval_ms);
int  mac_rot_get(uint8_t out[6]);
int  mac_rot_rotate(void);
int  mac_rot_tick(boot_nic_t *nic);
int  mac_rot_apply(boot_nic_t *nic);
int  mac_rot_restore(boot_nic_t *nic);
int  mac_rot_jump_to(uint32_t index);
void mac_rot_set_interval(uint32_t interval_ms);
void mac_rot_set_auto(int enable);
void mac_rot_set_preserve(int preserve_unicast, int preserve_locally);
void mac_rot_get_stats(sec_stats_t *stats);

/* MAC-5: MAC OUI Spoofer (prefix spoofing with vendor DB) */
int  oui_spoof_init(const uint8_t original_mac[6]);
int  oui_spoof_select(uint32_t oui_idx);
int  oui_spoof_select_random(void);
int  oui_spoof_select_vendor(const char *vendor);
int  oui_spoof_get(uint8_t out[6]);
int  oui_spoof_apply(boot_nic_t *nic);
int  oui_spoof_restore(boot_nic_t *nic);
void oui_spoof_get_oui(uint8_t out_oui[3]);
void oui_spoof_set_randomize_nic(int enable);
void oui_spoof_set_preserve_universal(int enable);
void oui_spoof_get_stats(sec_stats_t *stats);

/* --- Anti IPv4/6 Reader (IP address obfuscation) --- */
int  anti_ip_init(uint32_t real_ipv4, const uint8_t real_ipv6[16]);
void anti_ip_set_peer(uint32_t peer_v4, const uint8_t peer_ipv6[16]);
int  anti_ip_obfuscate_v4(uint8_t *packet, uint32_t len);
int  anti_ip_obfuscate_v6(uint8_t *packet, uint32_t len);
int  anti_ip_deobfuscate_v4(uint8_t *packet, uint32_t len);
int  anti_ip_deobfuscate_v6(uint8_t *packet, uint32_t len);
int  anti_ip_rotate(void);
int  anti_ip_onion_wrap(uint32_t relay_addr_v4, const uint8_t relay_addr6[16]);
int  anti_ip_onion_apply_v4(uint8_t *packet, uint32_t len);
void anti_ip_set_mode(uint8_t mode);
void anti_ip_set_ttl(uint32_t ttl);
void anti_ip_set_normalize(int tos, int df, int id_random, int options);
void anti_ip_get_fake_v4(uint32_t *src, uint32_t *dst);
void anti_ip_get_fake_v6(uint8_t src[16], uint8_t dst[16]);
void anti_ip_get_stats(sec_stats_t *stats);

/* ========================================================================
 * 10 Disk Data / UUID Encrypters
 * Obfuscate disk identifiers, UUIDs, serials, labels so that lsblk,
 * blkid, smartctl, and similar tools cannot identify real hardware.
 * All operate at boot-time on in-memory disk structures.
 * ======================================================================== */

/* UUID length for all disk modules */
#define DISK_UUID_LEN       16
#define DISK_SERIAL_LEN     32
#define DISK_LABEL_LEN      64

/* --- DISK-1: UUID Randomizer (FS UUID rotation per boot) --- */
int  disk_uuid_random_init(void);
int  disk_uuid_randomize(const uint8_t uuid_in[DISK_UUID_LEN],
                         uint8_t uuid_out[DISK_UUID_LEN]);
int  disk_uuid_restore(const uint8_t uuid_in[DISK_UUID_LEN],
                       uint8_t uuid_out[DISK_UUID_LEN]);
int  disk_uuid_random_rotate(void);
void disk_uuid_random_get_key(uint8_t key[32]);
void disk_uuid_random_get_stats(sec_stats_t *stats);

/* --- DISK-2: Volume Serial Masker (XOR-based serial obfuscation) --- */
int  disk_serial_mask_init(const uint8_t key[32]);
int  disk_serial_mask_apply(const uint8_t serial_in[DISK_SERIAL_LEN],
                            uint8_t serial_out[DISK_SERIAL_LEN]);
int  disk_serial_mask_remove(const uint8_t serial_in[DISK_SERIAL_LEN],
                             uint8_t serial_out[DISK_SERIAL_LEN]);
int  disk_serial_mask_rotate(const uint8_t new_key[32]);
void disk_serial_mask_get_stats(sec_stats_t *stats);

/* --- DISK-3: GPT Header Encrypter (disk GUID + partition GUID obfuscation) --- */
typedef struct {
    uint8_t disk_guid[DISK_UUID_LEN];
    uint8_t part_guid[DISK_UUID_LEN];
    uint8_t first_lba[8];
    uint8_t last_lba[8];
    uint32_t part_num;
} disk_gpt_entry_t;

int  disk_gpt_encrypt_init(void);
int  disk_gpt_encrypt_disk_guid(const uint8_t guid_in[DISK_UUID_LEN],
                                uint8_t guid_out[DISK_UUID_LEN]);
int  disk_gpt_encrypt_part_guid(const uint8_t guid_in[DISK_UUID_LEN],
                                uint8_t guid_out[DISK_UUID_LEN]);
int  disk_gpt_encrypt_entry(const disk_gpt_entry_t *in,
                            disk_gpt_entry_t *out);
int  disk_gpt_decrypt_entry(const disk_gpt_entry_t *in,
                            disk_gpt_entry_t *out);
void disk_gpt_get_stats(sec_stats_t *stats);

/* --- DISK-4: MBR Disk ID Scrambler (signature byte scramble) --- */
int  disk_mbr_scramble_init(void);
int  disk_mbr_scramble(uint32_t disk_id_in, uint32_t *disk_id_out);
int  disk_mbr_unscramble(uint32_t disk_id_in, uint32_t *disk_id_out);
int  disk_mbr_scramble_part_entry(uint8_t entry_in[16], uint8_t entry_out[16]);
int  disk_mbr_unscramble_part_entry(uint8_t entry_in[16], uint8_t entry_out[16]);
void disk_mbr_get_stats(sec_stats_t *stats);

/* --- DISK-5: LUKS Header Camouflage (volume type hiding) --- */
int  disk_luks_camouflage_init(void);
int  disk_luks_hide_header(uint8_t *sector0, uint32_t sector_size);
int  disk_luks_reveal_header(uint8_t *sector0, uint32_t sector_size);
int  disk_luks_mask_magic(uint8_t *sector0, uint32_t sector_size);
int  disk_luks_unmask_magic(uint8_t *sector0, uint32_t sector_size);
void disk_luks_get_stats(sec_stats_t *stats);

/* --- DISK-6: Partition Name Encrypter (GPT name string mask) --- */
int  disk_partname_init(void);
int  disk_partname_encrypt(const uint8_t *name_in, uint32_t name_len,
                           uint8_t *name_out);
int  disk_partname_decrypt(const uint8_t *name_in, uint32_t name_len,
                           uint8_t *name_out);
int  disk_partname_rotate(void);
void disk_partname_get_stats(sec_stats_t *stats);

/* --- DISK-7: Filesystem Label Encrypter (volume label rotation) --- */
int  disk_label_init(void);
int  disk_label_encrypt(const uint8_t *label_in, uint32_t label_len,
                        uint8_t *label_out);
int  disk_label_decrypt(const uint8_t *label_in, uint32_t label_len,
                        uint8_t *label_out);
int  disk_label_rotate(void);
void disk_label_get_stats(sec_stats_t *stats);

/* --- DISK-8: SMART Serial Obfuscator (drive serial + model hide) --- */
int  disk_smart_obfuscate_init(void);
int  disk_smart_hide_serial(const uint8_t serial_in[DISK_SERIAL_LEN],
                            uint8_t serial_out[DISK_SERIAL_LEN]);
int  disk_smart_reveal_serial(const uint8_t serial_in[DISK_SERIAL_LEN],
                              uint8_t serial_out[DISK_SERIAL_LEN]);
int  disk_smart_hide_model(const uint8_t *model_in, uint32_t model_len,
                           uint8_t *model_out);
int  disk_smart_reveal_model(const uint8_t *model_in, uint32_t model_len,
                             uint8_t *model_out);
void disk_smart_get_stats(sec_stats_t *stats);

/* --- DISK-9: ATA INQUIRY Scrambler (identify data scramble) --- */
int  disk_inquiry_init(void);
int  disk_inquiry_scramble(uint16_t ident_data[256], uint16_t out[256]);
int  disk_inquiry_unscramble(uint16_t ident_data[256], uint16_t out[256]);
int  disk_inquiry_scramble_model(uint16_t ident_data[256]);
int  disk_inquiry_scramble_serial(uint16_t ident_data[256]);
void disk_inquiry_get_stats(sec_stats_t *stats);

/* --- DISK-10: Disk Fingerprint Rotator (cross-boot persistent ID rotation) --- */
int  disk_fingerprint_init(uint64_t boot_counter);
int  disk_fingerprint_rotate(void);
int  disk_fingerprint_get(uint8_t fingerprint[DISK_UUID_LEN]);
int  disk_fingerprint_apply_uuid(const uint8_t real_uuid[DISK_UUID_LEN],
                                 uint8_t fake_uuid[DISK_UUID_LEN]);
int  disk_fingerprint_revert_uuid(const uint8_t fake_uuid[DISK_UUID_LEN],
                                  uint8_t real_uuid[DISK_UUID_LEN]);
uint64_t disk_fingerprint_get_boot_count(void);
void disk_fingerprint_get_stats(sec_stats_t *stats);

/* ========================================================================
 * Tor Browser (minimal, maximum-security Tor client at boot time)
 * ======================================================================== */

/* Tor core */
#include "security/tor.h"

int  tor_init(void);
int  tor_config_init(tor_config_t *cfg);
int  tor_circuit_create(tor_circuit_t *circ, uint8_t purpose);
int  tor_circuit_extend(tor_circuit_t *circ, const tor_relay_t *relay);
int  tor_circuit_destroy(tor_circuit_t *circ);
int  tor_circuit_send_cell(tor_circuit_t *circ, const tor_cell_t *cell);
int  tor_circuit_recv_cell(tor_circuit_t *circ, tor_cell_t *cell);
int  tor_encrypt_cell(tor_cell_t *cell, const tor_hop_t *hops, uint8_t hop_count);
int  tor_decrypt_cell(tor_cell_t *cell, const tor_hop_t *hop);

/* Tor directory */
int  tor_directory_fetch_consensus(tor_consensus_t *cons);
int  tor_directory_request_desc(const uint8_t *fingerprint, uint8_t *desc,
                                uint32_t *desc_len);

/* Tor SOCKS5 proxy */
int  tor_socks5_init(void);
int  tor_socks5_accept(tor_socks5_t *conn);
int  tor_socks5_handle(tor_socks5_t *conn, const uint8_t *data, uint32_t len,
                       uint8_t *resp, uint32_t *resp_len);
int  tor_socks5_connect(tor_socks5_t *conn, uint32_t circ_id);

/* Tor hidden services */
int  tor_hs_init(tor_hidden_service_t *hs, uint16_t port);
int  tor_hs_publish(tor_hidden_service_t *hs);
int  tor_hs_handle_intro(tor_hidden_service_t *hs, const tor_cell_t *cell);

/* Tor streams */
int  tor_stream_open(uint32_t circ_id, const uint8_t *addr, uint16_t port,
                     uint8_t purpose);
int  tor_stream_send_data(uint32_t stream_id, const uint8_t *data, uint32_t len);
int  tor_stream_close(uint32_t stream_id);

/* Tor security */
void tor_set_padding_level(uint8_t level);
void tor_force_new_circuit(void);
void tor_flush_streams(void);
void tor_get_stats(uint32_t *circuits, uint32_t *streams,
                   uint32_t *bytes_sent, uint32_t *bytes_recv);

/* ========================================================================
 * WiFi Drivers (all chipsets)
 * ======================================================================== */

/* WiFi unified API */
#include "drivers/wifi.h"

int  wifi_init(void);
int  wifi_register_driver(const wifi_driver_ops_t *ops);
int  wifi_select_driver(uint32_t pci_vendor, uint32_t pci_device);
int  wifi_scan_start(uint8_t channel, uint8_t band);
int  wifi_scan_get(wifi_scan_result_t *result);
int  wifi_auth(const uint8_t *bssid, const uint8_t *ssid,
               uint8_t security, const uint8_t *key, uint32_t key_len);
int  wifi_assoc(const uint8_t *bssid, uint8_t channel);
int  wifi_deauth(const uint8_t *bssid);
int  wifi_send(const uint8_t *frame, uint32_t len);
int  wifi_recv(uint8_t *frame, uint32_t *len);
void wifi_set_channel(uint8_t channel, uint8_t band);
int  wifi_set_power(uint8_t power);
int  wifi_get_rssi(void);
void wifi_get_mac(uint8_t mac[6]);
uint8_t wifi_get_state(void);
int  wifi_pci_scan(uint32_t *vendor, uint32_t *device, uint32_t *bar0);

/* Individual driver registration */
int  wifi_intel_register(void);
int  wifi_atheros_register(void);
int  wifi_broadcom_register(void);
int  wifi_realtek_register(void);
int  wifi_mediatek_register(void);
int  wifi_prism5_register(void);
int  wifi_marvell_register(void);

/* ========================================================================
 * Master security init (called from Stage 2 main)
 * ======================================================================== */
typedef struct {
    uint8_t  fw_enabled;
    uint8_t  dns_enc_enabled;
    uint8_t  wifi_enc_enabled;
    uint8_t  fw_count;
    uint8_t  dns_count;
    uint8_t  wifi_count;
    sec_stats_t stats;
    uint64_t init_time_us;
} sec_master_state_t;

int  sec_master_init(void);
int  sec_master_init_firewalls(void);
int  sec_master_init_dns_encrypters(void);
int  sec_master_init_wifi_encrypters(void);
int  sec_master_eval_packet(const sec_packet_t *pkt);
void sec_master_get_stats(sec_master_state_t *out);

/* E1000 NIC functions */
int  e1000_probe(uint16_t *io_base);
void e1000_reset(uint16_t io_base);
int  e1000_read_mac(uint16_t io_base, uint8_t mac[6]);
int  e1000_init_ring(uint16_t io_base);

/* Boot NIC operations */
void boot_nic_set_mac(boot_nic_t *nic, const uint8_t mac[6]);

/* Crypto helper functions */
void sec_sha256(const uint8_t *data, uint32_t len, uint8_t out[32]);
void sec_sha512(const uint8_t *data, uint32_t len, uint8_t out[64]);
void sec_pbkdf2_sha1(const uint8_t *password, uint32_t pass_len,
                     const uint8_t *salt, uint32_t salt_len,
                     uint32_t iterations, uint8_t *out, uint32_t out_len);
void sec_hkdf_sha256(const uint8_t *ikm, uint32_t ikm_len,
                     const uint8_t *salt, uint32_t salt_len,
                     const uint8_t *info, uint32_t info_len,
                     uint8_t *okm, uint32_t okm_len);

#endif /* BOOT_SECURITY_H */
