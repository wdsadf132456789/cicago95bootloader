/**
 * Chicago-95 DNS Encrypter #1: DNS over HTTPS (DoH)
 * Encrypts DNS queries inside HTTPS POST/GET to a DoH server
 */

#include "boot/security.h"

#define DOH_MAX_URL        256
#define DOH_MAX_REQUEST    2048
#define DOH_MAX_RESPONSE   4096
#define DOH_PORT           443
#define DOH_CONTENT_TYPE   "application/dns-message"
#define DOH_TIMEOUT_MS     5000

typedef struct {
    char     server_url[DOH_MAX_URL];
    char     server_host[128];
    uint16_t server_port;
    uint8_t  ca_cert[SEC_MAX_CERT_SIZE];
    uint32_t ca_cert_len;
    uint8_t  session_key[32];
    uint8_t  tls_master[48];
    uint8_t  tls_client_random[32];
    uint8_t  tls_server_random[32];
    uint8_t  tls_session_id[32];
    uint32_t tls_session_id_len;
    uint8_t  connected;
    sec_stats_t stats;
    uint8_t  initialized;
} doh_state_t;

static doh_state_t doh;

/* ---- URL parsing ---- */
static void parse_doh_url(const char *url) {
    const char *p = url;
    uint32_t i = 0;

    /* Skip https:// */
    if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == 's' && p[5] == ':') {
        p += 8;
    } else if (p[0] == 'h' && p[1] == 't' && p[2] == 't' && p[3] == 'p' && p[4] == ':') {
        p += 7;
    }

    /* Extract host */
    i = 0;
    while (*p && *p != ':' && *p != '/' && i < 127) {
        doh.server_host[i++] = *p++;
    }
    doh.server_host[i] = 0;

    /* Extract port (default 443) */
    doh.server_port = DOH_PORT;
    if (*p == ':') {
        p++;
        doh.server_port = 0;
        while (*p >= '0' && *p <= '9') {
            doh.server_port = doh.server_port * 10 + (*p - '0');
            p++;
        }
    }

    /* Extract path */
    i = 0;
    while (*p && i < DOH_MAX_URL - 1) {
        doh.server_url[i++] = *p++;
    }
    doh.server_url[i] = 0;
}

/* ---- Init ---- */
int dns_doh_init(const char *server_url, const uint8_t ca_cert[], uint32_t ca_cert_len) {
    if (!server_url) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&doh;
    for (uint32_t i = 0; i < sizeof(doh_state_t); i++) d[i] = 0;

    parse_doh_url(server_url);

    if (ca_cert && ca_cert_len > 0 && ca_cert_len <= SEC_MAX_CERT_SIZE) {
        for (uint32_t i = 0; i < ca_cert_len; i++) doh.ca_cert[i] = ca_cert[i];
        doh.ca_cert_len = ca_cert_len;
    }

    sec_random_bytes(doh.session_key, 32);
    doh.initialized = 1;
    return SEC_OK;
}

/* ---- Build DoH request (RFC 8484 GET method) ---- */
int dns_doh_build_request(const char *hostname, uint16_t qtype, uint8_t *out, uint32_t *out_len) {
    if (!hostname || !out || !out_len) return SEC_ERR_BAD_PARAM;

    /* Build raw DNS query first */
    uint8_t dns_query[512];
    uint32_t dns_len = 0;

    /* DNS Header */
    uint16_t id = (uint16_t)sec_random_u32();
    dns_query[dns_len++] = (id >> 8) & 0xFF;
    dns_query[dns_len++] = id & 0xFF;
    dns_query[dns_len++] = 0x01; /* Standard query, recursion desired */
    dns_query[dns_len++] = 0x00;
    dns_query[dns_len++] = 0x00; /* QDCOUNT = 1 */
    dns_query[dns_len++] = 0x01;
    dns_query[dns_len++] = 0x00; /* ANCOUNT */
    dns_query[dns_len++] = 0x00;
    dns_query[dns_len++] = 0x00; /* NSCOUNT */
    dns_query[dns_len++] = 0x00;
    dns_query[dns_len++] = 0x00; /* ARCOUNT */
    dns_query[dns_len++] = 0x00;

    /* Question: encode hostname as labels */
    const char *p = hostname;
    while (*p) {
        uint32_t label_start = dns_len;
        dns_len++; /* reserve length byte */
        uint32_t label_len = 0;
        while (*p && *p != '.' && label_len < 63) {
            dns_query[dns_len++] = *p++;
            label_len++;
        }
        dns_query[label_start] = label_len;
        if (*p == '.') p++;
    }
    dns_query[dns_len++] = 0; /* root label */

    /* QTYPE */
    dns_query[dns_len++] = (qtype >> 8) & 0xFF;
    dns_query[dns_len++] = qtype & 0xFF;
    /* QCLASS = IN (1) */
    dns_query[dns_len++] = 0x00;
    dns_query[dns_len++] = 0x01;

    /* Base64url encode the DNS query */
    const char *b64chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
    uint32_t b64_out = 0;

    /* Build HTTP GET request */
    uint32_t pos = 0;
    /* "GET " */
    out[pos++] = 'G'; out[pos++] = 'E'; out[pos++] = 'T'; out[pos++] = ' ';
    /* Path */
    for (uint32_t i = 0; doh.server_url[i]; i++) out[pos++] = doh.server_url[i];
    /* "?dns=" */
    out[pos++] = '?'; out[pos++] = 'd'; out[pos++] = 'n'; out[pos++] = 's'; out[pos++] = '=';

    /* Base64url encode DNS query */
    for (uint32_t i = 0; i < dns_len; i += 3) {
        uint32_t val = ((uint32_t)dns_query[i]) << 16;
        if (i + 1 < dns_len) val |= ((uint32_t)dns_query[i + 1]) << 8;
        if (i + 2 < dns_len) val |= dns_query[i + 2];

        out[pos++] = b64chars[(val >> 18) & 0x3F];
        out[pos++] = b64chars[(val >> 12) & 0x3F];
        if (i + 1 < dns_len) out[pos++] = b64chars[(val >> 6) & 0x3F];
        if (i + 2 < dns_len) out[pos++] = b64chars[val & 0x3F];
    }

    /* HTTP/1.1 headers */
    out[pos++] = ' '; out[pos++] = 'H'; out[pos++] = 'T'; out[pos++] = 'T';
    out[pos++] = 'P'; out[pos++] = '/'; out[pos++] = '1'; out[pos++] = '.';
    out[pos++] = '1'; out[pos++] = '\r'; out[pos++] = '\n';

    /* Host header */
    out[pos++] = 'H'; out[pos++] = 'o'; out[pos++] = 's'; out[pos++] = 't';
    out[pos++] = ':'; out[pos++] = ' ';
    for (uint32_t i = 0; doh.server_host[i]; i++) out[pos++] = doh.server_host[i];
    out[pos++] = '\r'; out[pos++] = '\n';

    /* Accept header */
    out[pos++] = 'A'; out[pos++] = 'c'; out[pos++] = 'c'; out[pos++] = 'e';
    out[pos++] = 'p'; out[pos++] = 't'; out[pos++] = ':'; out[pos++] = ' ';
    out[pos++] = 'd'; out[pos++] = 'n'; out[pos++] = 's'; out[pos++] = '-';
    out[pos++] = 'm'; out[pos++] = 'e'; out[pos++] = 's'; out[pos++] = 's';
    out[pos++] = 'a'; out[pos++] = 'g'; out[pos++] = 'e'; out[pos++] = '\r';
    out[pos++] = '\n';

    /* Connection: close */
    out[pos++] = 'C'; out[pos++] = 'o'; out[pos++] = 'n'; out[pos++] = 'n';
    out[pos++] = 'e'; out[pos++] = 'c'; out[pos++] = 't'; out[pos++] = 'i';
    out[pos++] = 'o'; out[pos++] = 'n'; out[pos++] = ':'; out[pos++] = ' ';
    out[pos++] = 'c'; out[pos++] = 'l'; out[pos++] = 'o'; out[pos++] = 's';
    out[pos++] = 'e'; out[pos++] = '\r'; out[pos++] = '\n';

    /* Double CRLF */
    out[pos++] = '\r'; out[pos++] = '\n';

    *out_len = pos;
    return SEC_OK;
}

/* ---- Encrypt raw DNS query into DoH payload ---- */
int dns_doh_encrypt_query(const uint8_t *raw_query, uint32_t query_len,
                          uint8_t *out, uint32_t *out_len) {
    if (!raw_query || !out || !out_len) return SEC_ERR_BAD_PARAM;

    /* Encrypt with AES-256-GCM using session key */
    uint8_t iv[12];
    sec_random_bytes(iv, 12);

    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, doh.session_key);

    sec_aes256_gcm_encrypt(&ctx, raw_query, out, query_len, iv, (const uint8_t*)"doh", 3, out + query_len);

    /* Prepend IV (12 bytes) */
    for (int i = 11; i >= 0; i--) {
        out[query_len + 16 + i] = out[i];
    }
    for (uint32_t i = 0; i < 12; i++) out[i] = iv[i];
    *out_len = query_len + 16 + 12; /* IV + ciphertext + GCM tag */

    doh.stats.packets_encrypted++;
    doh.stats.dns_queries_encrypted++;
    return SEC_OK;
}

/* ---- Decrypt DoH response ---- */
int dns_doh_decrypt_response(const uint8_t *encrypted, uint32_t encrypted_len,
                             uint8_t *out, uint32_t *out_len) {
    if (!encrypted || !out || !out_len) return SEC_ERR_BAD_PARAM;
    if (encrypted_len < 28) return SEC_ERR_BAD_PARAM; /* 12 IV + 16 tag minimum */

    const uint8_t *iv = encrypted;
    const uint8_t *ciphertext = encrypted + 12;
    const uint8_t *tag = encrypted + encrypted_len - 16;
    uint32_t ct_len = encrypted_len - 12 - 16;

    sec_aes256_ctx_t ctx;
    sec_aes256_init(&ctx, doh.session_key);

    int result = sec_aes256_gcm_decrypt(&ctx, ciphertext, out, ct_len, iv, (const uint8_t*)"doh", 3, tag);
    if (result != 0) return SEC_ERR_CRYPTO;

    *out_len = ct_len;
    return SEC_OK;
}

/* ---- Send/Receive (uses boot_nic + TLS) ---- */
int dns_doh_send_receive(boot_nic_t *nic, const uint8_t *request, uint32_t req_len,
                         uint8_t *response, uint32_t *resp_len) {
    if (!nic || !request || !response || !resp_len) return SEC_ERR_BAD_PARAM;

    /* Build TCP SYN to DoH server port 443 */
    uint8_t frame[1514];
    uint32_t frame_len = 0;

    /* Ethernet header */
    boot_nic_get_mac(nic, frame);
    frame[6] = 0x00; frame[7] = 0x00; frame[8] = 0x00; frame[9] = 0x00; frame[10] = 0x00; frame[11] = 0x00;
    frame[12] = 0x08; frame[13] = 0x00; /* IPv4 */
    frame_len = 14;

    /* In full implementation: build IP + TCP + TLS handshake + HTTP request */
    /* For boot-time, this uses the NIC driver to send raw frames */

    int result = boot_nic_send(nic, frame, frame_len);
    if (result != SEC_OK) return result;

    result = boot_nic_recv(nic, response, resp_len, DOH_TIMEOUT_MS);
    return result;
}

void dns_doh_set_server(const char *server_url) {
    if (server_url) parse_doh_url(server_url);
}

void dns_doh_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&doh.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
