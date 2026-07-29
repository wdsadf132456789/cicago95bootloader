/**
 * Chicago-95 Gen-2 Firewall #3: Application Layer (L7 Deep Packet Inspection)
 * HTTP, DNS, TLS SNI parsing and validation
 */

#include "boot/security.h"

#define MAX_APP_RULES      256
#define MAX_PATTERN_LEN    128
#define MAX_HOSTNAME_LEN   256
#define MAX_URI_LEN        1024
#define HTTP_MAX_HEADER    2048
#define DNS_MAX_NAME       256

typedef struct {
    uint32_t rule_id;
    uint8_t  enabled;
    uint8_t  proto;          /* 0=any, 6=TCP, 17=UDP */
    uint16_t port;
    uint8_t  action;
    char     pattern[MAX_PATTERN_LEN];
    uint32_t pattern_len;
    uint8_t  pattern_type;   /* 0=string, 1=http_method, 2=dns_type, 3=sni */
    char     description[64];
} app_rule_t;

static app_rule_t  app_rules[MAX_APP_RULES];
static uint32_t    app_rule_count = 0;
static sec_stats_t app_stats;
static uint8_t     app_initialized = 0;

/* ---- Helpers ---- */
static int str_compare(const char *a, const char *b, uint32_t len) {
    for (uint32_t i = 0; i < len; i++) {
        if (a[i] != b[i]) return (a[i] - b[i]);
        if (a[i] == 0) return 0;
    }
    return 0;
}

static int str_contains(const char *haystack, uint32_t hay_len, const char *needle, uint32_t need_len) {
    if (need_len > hay_len || need_len == 0) return 0;
    for (uint32_t i = 0; i <= hay_len - need_len; i++) {
        int match = 1;
        for (uint32_t j = 0; j < need_len; j++) {
            if (haystack[i + j] != needle[j]) { match = 0; break; }
        }
        if (match) return 1;
    }
    return 0;
}

/* ---- Init ---- */
int fw_app_layer_init(void) {
    app_rule_count = 0;
    app_stats.packets_inspected = 0;
    app_stats.packets_allowed = 0;
    app_stats.packets_dropped = 0;
    app_stats.errors = 0;
    app_initialized = 1;
    return SEC_OK;
}

int fw_app_layer_add_rule(const sec_fw_rule_t *rule) {
    if (!rule || app_rule_count >= MAX_APP_RULES) return SEC_ERR_NOMEM;
    app_rule_t *r = &app_rules[app_rule_count];
    r->rule_id = rule->rule_id;
    r->enabled = rule->enabled;
    r->proto = rule->protocol;
    r->port = rule->dst_port;
    r->action = rule->action;
    r->pattern_len = 0;
    r->pattern_type = 0;
    for (uint32_t i = 0; i < 64 && rule->description[i]; i++) {
        r->description[i] = rule->description[i];
        r->pattern_len++;
    }
    r->pattern_len = 0;
    app_rule_count++;
    return SEC_OK;
}

/* ---- HTTP Header Parsing ---- */
int fw_app_layer_inspect_http(const uint8_t *payload, uint32_t len) {
    if (!payload || len < 4) return FW_ACTION_ALLOW;

    /* Check for HTTP method at start */
    const char *methods[] = { "GET ", "POST ", "PUT ", "DELETE ", "HEAD ", "OPTIONS ", "PATCH ", "CONNECT " };
    uint32_t method_lens[] = { 4, 5, 4, 7, 5, 8, 6, 8 };

    int is_http = 0;
    for (int m = 0; m < 8; m++) {
        if (len >= method_lens[m] && str_compare((const char*)payload, methods[m], method_lens[m] - 1) == 0) {
            is_http = 1;
            break;
        }
    }
    if (!is_http) return FW_ACTION_ALLOW;

    /* Check for response */
    if (len >= 4 && str_compare((const char*)payload, "HTTP", 4) == 0) is_http = 1;
    if (!is_http) return FW_ACTION_ALLOW;

    app_stats.packets_inspected++;

    /* Parse headers line by line */
    uint32_t pos = 0;
    while (pos < len - 1) {
        /* Find end of current header line */
        uint32_t line_start = pos;
        while (pos < len - 1 && !(payload[pos] == '\r' && payload[pos + 1] == '\n')) pos++;
        uint32_t line_len = pos - line_start;
        pos += 2; /* skip \r\n */

        if (line_len < 2) continue; /* empty line = end of headers */

        /* Extract header name (before ':') */
        char header_name[64];
        uint32_t hname_len = 0;
        uint32_t h = line_start;
        while (h < line_start + line_len && payload[h] != ':' && hname_len < 63) {
            header_name[hname_len++] = payload[h++];
        }
        header_name[hname_len] = 0;

        /* Skip ': ' */
        h++;
        while (h < line_start + line_len && payload[h] == ' ') h++;
        uint32_t value_len = line_start + line_len - h;
        const char *value = (const char*)&payload[h];

        /* Check Host header for forbidden hostnames */
        if (hname_len == 4 && str_compare(header_name, "Host", 4) == 0) {
            for (uint32_t r = 0; r < app_rule_count; r++) {
                if (app_rules[r].enabled && app_rules[r].pattern_type == 2) {
                    if (str_contains(value, value_len, app_rules[r].pattern, app_rules[r].pattern_len)) {
                        app_stats.packets_dropped++;
                        return app_rules[r].action;
                    }
                }
            }
        }

        /* Check for oversized headers */
        if (line_len > 8192) {
            app_stats.packets_dropped++;
            return FW_ACTION_DROP;
        }

        /* Check Content-Length for oversized bodies */
        if (hname_len == 14 && str_compare(header_name, "Content-Length", 14) == 0) {
            uint32_t content_len = 0;
            for (uint32_t v = 0; v < value_len && value[v] >= '0' && value[v] <= '9'; v++) {
                content_len = content_len * 10 + (value[v] - '0');
            }
            if (content_len > 100 * 1024 * 1024) { /* 100MB limit */
                app_stats.packets_dropped++;
                return FW_ACTION_DROP;
            }
        }
    }

    app_stats.packets_allowed++;
    return FW_ACTION_ALLOW;
}

/* ---- DNS Packet Parsing ---- */
int fw_app_layer_inspect_dns(const uint8_t *payload, uint32_t len) {
    if (!payload || len < 12) return FW_ACTION_ALLOW;
    app_stats.packets_inspected++;

    /* DNS header: ID(2) flags(2) QDCOUNT(2) ANCOUNT(2) NSCOUNT(2) ARCOUNT(2) */
    uint16_t qdcount = (payload[4] << 8) | payload[5];
    uint16_t ancount = (payload[6] << 8) | payload[7];

    /* Parse question section */
    uint32_t pos = 12;
    for (uint16_t q = 0; q < qdcount && pos < len; q++) {
        /* Parse QNAME */
        char qname[DNS_MAX_NAME];
        uint32_t qname_len = 0;
        uint32_t label_len;

        while (pos < len) {
            label_len = payload[pos++];
            if (label_len == 0) break;
            if (pos + label_len > len) return FW_ACTION_ALLOW;
            if (qname_len > 0) { qname[qname_len++] = '.'; }
            for (uint32_t l = 0; l < label_len && qname_len < DNS_MAX_NAME - 1; l++) {
                qname[qname_len++] = payload[pos++];
            }
        }
        qname[qname_len] = 0;

        /* Skip QTYPE(2) and QCLASS(2) */
        pos += 4;

        /* Check against rules */
        for (uint32_t r = 0; r < app_rule_count; r++) {
            if (!app_rules[r].enabled) continue;
            if (app_rules[r].pattern_type == 0 && app_rules[r].pattern_len > 0) {
                if (str_contains(qname, qname_len, app_rules[r].pattern, app_rules[r].pattern_len)) {
                    app_stats.packets_dropped++;
                    return app_rules[r].action;
                }
            }
            /* Block DNS response with too many answers (amplification) */
            if (app_rules[r].pattern_type == 2 && ancount > 64) {
                app_stats.packets_dropped++;
                return FW_ACTION_DROP;
            }
        }
    }

    app_stats.packets_allowed++;
    return FW_ACTION_ALLOW;
}

/* ---- TLS ClientHello SNI Extraction ---- */
int fw_app_layer_inspect_tls_sni(const uint8_t *payload, uint32_t len, char *hostname, uint32_t hostname_max) {
    if (!payload || len < 5 || !hostname) return SEC_ERR_BAD_PARAM;
    app_stats.packets_inspected++;

    /* TLS record: ContentType(1) Version(2) Length(2) */
    if (payload[0] != 0x16) return SEC_OK; /* Not a handshake */
    if (len < 5) return SEC_OK;

    uint16_t record_len = (payload[3] << 8) | payload[4];
    if ((uint32_t)record_len + 5 > len) return SEC_OK;

    /* Handshake: Type(1) Length(3) */
    uint32_t pos = 5;
    if (payload[pos] != 0x01) return SEC_OK; /* Not ClientHello */
    pos += 4; /* skip type + 3-byte length */

    /* ClientHello: Version(2) Random(32) */
    if (pos + 34 > len) return SEC_OK;
    pos += 34;

    /* SessionID */
    if (pos >= len) return SEC_OK;
    uint8_t session_len = payload[pos++];
    pos += session_len;

    /* Cipher Suites */
    if (pos + 2 > len) return SEC_OK;
    uint16_t cipher_len = (payload[pos] << 8) | payload[pos + 1];
    pos += 2 + cipher_len;

    /* Compression Methods */
    if (pos >= len) return SEC_OK;
    uint8_t comp_len = payload[pos++];
    pos += 1 + comp_len;

    /* Extensions */
    if (pos + 2 > len) return SEC_OK;
    uint16_t ext_total = (payload[pos] << 8) | payload[pos + 1];
    pos += 2;
    uint32_t ext_end = pos + ext_total;

    while (pos + 4 <= ext_end && pos + 4 <= len) {
        uint16_t ext_type = (payload[pos] << 8) | payload[pos + 1];
        uint16_t ext_len  = (payload[pos + 2] << 8) | payload[pos + 3];
        pos += 4;

        /* SNI extension = 0x0000 */
        if (ext_type == 0x0000 && ext_len > 5) {
            pos += 5; /* skip list length(2) + type(1) + name length(2) */
            uint32_t name_len = ext_len - 5;
            if (name_len >= hostname_max) name_len = hostname_max - 1;
            if (pos + name_len <= len) {
                for (uint32_t i = 0; i < name_len; i++) {
                    hostname[i] = payload[pos + i];
                }
                hostname[name_len] = 0;
                return SEC_OK;
            }
        }
        pos += ext_len;
    }

    return SEC_OK;
}

/* ---- Main eval (dispatches to protocol-specific inspectors) ---- */
int fw_app_layer_eval(const sec_packet_t *pkt) {
    if (!pkt || !pkt->payload) return FW_ACTION_ALLOW;
    app_stats.packets_inspected++;

    /* HTTP on TCP port 80, 8080, 443 */
    if (pkt->ip_proto == 6 && (pkt->dst_port == 80 || pkt->dst_port == 8080)) {
        return fw_app_layer_inspect_http(pkt->payload, pkt->payload_len);
    }

    /* DNS on UDP port 53 */
    if (pkt->ip_proto == 17 && pkt->dst_port == 53) {
        return fw_app_layer_inspect_dns(pkt->payload, pkt->payload_len);
    }

    /* TLS on TCP port 443 */
    if (pkt->ip_proto == 6 && pkt->dst_port == 443) {
        char sni[MAX_HOSTNAME_LEN];
        sni[0] = 0;
        fw_app_layer_inspect_tls_sni(pkt->payload, pkt->payload_len, sni, MAX_HOSTNAME_LEN);
        if (sni[0]) {
            for (uint32_t r = 0; r < app_rule_count; r++) {
                if (app_rules[r].enabled && app_rules[r].pattern_type == 3) {
                    if (str_contains(sni, 256, app_rules[r].pattern, app_rules[r].pattern_len)) {
                        app_stats.packets_dropped++;
                        return app_rules[r].action;
                    }
                }
            }
        }
    }

    app_stats.packets_allowed++;
    return FW_ACTION_ALLOW;
}

void fw_app_layer_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&app_stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
