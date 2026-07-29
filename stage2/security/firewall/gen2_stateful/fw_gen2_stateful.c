/**
 * Chicago-95 Gen-2 Firewall #2: Stateful Inspection
 * Full connection tracking with TCP state machine and SYN flood protection
 */

#include "boot/security.h"

#define MAX_CONNECTIONS     1024
#define SYN_COOKIES_MAX    256
#define SYN_FLOOD_THRESHOLD 64
#define SYN_FLOOD_WINDOW_MS 10000
#define CONN_TIMEOUT_MS     300000
#define CONN_HALFOPEN_MAX   128

/* TCP states */
#define TCP_ST_NONE         0
#define TCP_ST_SYN_SENT     1
#define TCP_ST_SYN_RCVD     2
#define TCP_ST_ESTABLISHED  3
#define TCP_ST_FIN_WAIT_1   4
#define TCP_ST_FIN_WAIT_2   5
#define TCP_ST_CLOSING      6
#define TCP_ST_TIME_WAIT    7
#define TCP_ST_CLOSE_WAIT   8
#define TCP_ST_LAST_ACK     9
#define TCP_ST_CLOSED       10

/* TCP flags */
#define TCP_FIN  0x01
#define TCP_SYN  0x02
#define TCP_RST  0x04
#define TCP_PSH  0x08
#define TCP_ACK  0x10
#define TCP_URG  0x20

/* SYN cookie entry */
typedef struct {
    uint32_t src_ip;
    uint32_t dst_ip;
    uint16_t src_port;
    uint16_t dst_port;
    uint32_t seq;
    uint64_t timestamp;
    uint8_t  used;
} syn_cookie_t;

static sec_conn_state_t connections[MAX_CONNECTIONS];
static uint32_t         conn_count = 0;
static syn_cookie_t     syn_cookies[SYN_COOKIES_MAX];
static uint32_t         syn_cookie_count = 0;
static uint32_t         halfopen_count = 0;
static sec_fw_rule_t    stateful_rules[SEC_MAX_RULES];
static uint32_t         stateful_rule_count = 0;
static sec_stats_t      stateful_stats;
static uint8_t          stateful_initialized = 0;
static uint64_t         stateful_now = 0;

static uint64_t get_timestamp(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void conn_zero_conn(sec_conn_state_t *c) {
    uint8_t *p = (uint8_t*)c;
    for (uint32_t i = 0; i < sizeof(sec_conn_state_t); i++) p[i] = 0;
}

/* ---- Init ---- */
int fw_stateful_init(void) {
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) conn_zero_conn(&connections[i]);
    for (uint32_t i = 0; i < SYN_COOKIES_MAX; i++) syn_cookies[i].used = 0;
    conn_count = 0;
    syn_cookie_count = 0;
    halfopen_count = 0;
    stateful_rule_count = 0;
    stateful_stats.packets_inspected = 0;
    stateful_stats.packets_allowed = 0;
    stateful_stats.packets_dropped = 0;
    stateful_stats.connections_tracked = 0;
    stateful_stats.connections_established = 0;
    stateful_stats.connections_closed = 0;
    stateful_stats.errors = 0;
    stateful_initialized = 1;
    return SEC_OK;
}

int fw_stateful_add_rule(const sec_fw_rule_t *rule) {
    if (!rule || stateful_rule_count >= SEC_MAX_RULES) return SEC_ERR_NOMEM;
    sec_fw_rule_t *dst = &stateful_rules[stateful_rule_count];
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)rule;
    for (uint32_t i = 0; i < sizeof(sec_fw_rule_t); i++) d[i] = s[i];
    stateful_rule_count++;
    return SEC_OK;
}

/* ---- Find connection by 5-tuple ---- */
static int find_connection(uint32_t sip, uint32_t dip, uint16_t sp, uint16_t dp, uint8_t proto, sec_conn_state_t **out) {
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        sec_conn_state_t *c = &connections[i];
        if (c->src_ip == sip && c->dst_ip == dip &&
            c->src_port == sp && c->dst_port == dp && c->proto == proto) {
            *out = c;
            return 1;
        }
        /* Also check reverse direction */
        if (c->src_ip == dip && c->dst_ip == sip &&
            c->src_port == dp && c->dst_port == sp && c->proto == proto) {
            *out = c;
            return 1;
        }
    }
    return 0;
}

/* ---- Create new connection ---- */
static sec_conn_state_t *create_connection(uint32_t sip, uint32_t dip, uint16_t sp, uint16_t dp, uint8_t proto) {
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        if (connections[i].proto == 0) {
            sec_conn_state_t *c = &connections[i];
            c->src_ip = sip;
            c->dst_ip = dip;
            c->src_port = sp;
            c->dst_port = dp;
            c->proto = proto;
            c->state = TCP_ST_NONE;
            c->created = stateful_now;
            c->last_seen = stateful_now;
            c->packets_in = 0;
            c->packets_out = 0;
            c->bytes_in = 0;
            c->bytes_out = 0;
            c->flags = 0;
            conn_count++;
            stateful_stats.connections_tracked++;
            return c;
        }
    }
    return (sec_conn_state_t*)0;
}

/* ---- SYN cookie ---- */
static int syn_cookie_create(uint32_t sip, uint32_t dip, uint16_t sp, uint16_t dp, uint32_t seq) {
    for (uint32_t i = 0; i < SYN_COOKIES_MAX; i++) {
        if (!syn_cookies[i].used) {
            syn_cookies[i].src_ip = sip;
            syn_cookies[i].dst_ip = dip;
            syn_cookies[i].src_port = sp;
            syn_cookies[i].dst_port = dp;
            syn_cookies[i].seq = seq;
            syn_cookies[i].timestamp = stateful_now;
            syn_cookies[i].used = 1;
            syn_cookie_count++;
            return 1;
        }
    }
    return 0;
}

static int syn_cookie_verify(uint32_t sip, uint32_t dip, uint16_t sp, uint16_t dp, uint32_t ack_seq) {
    for (uint32_t i = 0; i < SYN_COOKIES_MAX; i++) {
        syn_cookie_t *sc = &syn_cookies[i];
        if (sc->used && sc->src_ip == dip && sc->dst_ip == sip &&
            sc->src_port == dp && sc->dst_port == sp) {
            if (ack_seq == sc->seq + 1) {
                sc->used = 0;
                syn_cookie_count--;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- TCP state transition ---- */
static void tcp_state_advance(sec_conn_state_t *c, uint32_t flags) {
    switch (c->state) {
        case TCP_ST_NONE:
            if (flags & TCP_SYN) { c->state = TCP_ST_SYN_RCVD; c->flags |= CONN_FLAG_SYN_SEEN; }
            break;
        case TCP_ST_SYN_RCVD:
            if (flags & TCP_ACK) { c->state = TCP_ST_ESTABLISHED; c->flags |= CONN_FLAG_ESTABLISHED; stateful_stats.connections_established++; }
            break;
        case TCP_ST_ESTABLISHED:
            if (flags & TCP_FIN) { c->state = TCP_ST_FIN_WAIT_1; c->flags |= CONN_FLAG_FIN_SEEN; }
            if (flags & TCP_RST) { c->state = TCP_ST_CLOSED; c->flags |= CONN_FLAG_RST_SEEN; }
            break;
        case TCP_ST_FIN_WAIT_1:
            if (flags & TCP_ACK) c->state = TCP_ST_FIN_WAIT_2;
            if (flags & TCP_FIN) c->state = TCP_ST_CLOSING;
            break;
        case TCP_ST_FIN_WAIT_2:
            if (flags & TCP_FIN) c->state = TCP_ST_TIME_WAIT;
            break;
        case TCP_ST_CLOSING:
            if (flags & TCP_ACK) c->state = TCP_ST_TIME_WAIT;
            break;
        case TCP_ST_TIME_WAIT:
            c->state = TCP_ST_CLOSED;
            stateful_stats.connections_closed++;
            break;
        case TCP_ST_CLOSE_WAIT:
            if (flags & TCP_FIN) { c->state = TCP_ST_LAST_ACK; }
            break;
        case TCP_ST_LAST_ACK:
            if (flags & TCP_ACK) { c->state = TCP_ST_CLOSED; stateful_stats.connections_closed++; }
            break;
        default:
            break;
    }
    c->last_seen = stateful_now;
}

/* ---- Evaluate packet ---- */
int fw_stateful_eval(const sec_packet_t *pkt) {
    if (!pkt) return FW_ACTION_DENY;
    stateful_stats.packets_inspected++;
    stateful_now = get_timestamp();

    /* SYN flood protection */
    if (pkt->ip_proto == 6 && (pkt->tcp_flags & TCP_SYN) && !(pkt->tcp_flags & TCP_ACK)) {
        if (halfopen_count >= CONN_HALFOPEN_MAX) {
            stateful_stats.packets_dropped++;
            return FW_ACTION_DROP;
        }
        if (conn_count >= MAX_CONNECTIONS) {
            stateful_stats.packets_dropped++;
            return FW_ACTION_DROP;
        }
        /* Create SYN cookie */
        syn_cookie_create(pkt->src_ip, pkt->dst_ip, pkt->src_port, pkt->dst_port, 0);
        sec_conn_state_t *c = create_connection(pkt->src_ip, pkt->dst_ip, pkt->src_port, pkt->dst_port, pkt->ip_proto);
        if (c) {
            halfopen_count++;
            tcp_state_advance(c, pkt->tcp_flags);
        }
        stateful_stats.packets_allowed++;
        return FW_ACTION_ALLOW;
    }

    /* For non-SYN, find existing connection */
    sec_conn_state_t *conn = (sec_conn_state_t*)0;
    if (pkt->ip_proto == 6) {
        if (find_connection(pkt->src_ip, pkt->dst_ip, pkt->src_port, pkt->dst_port, pkt->ip_proto, &conn)) {
            tcp_state_advance(conn, pkt->tcp_flags);
            if (conn->state == TCP_ST_SYN_RCVD && (pkt->tcp_flags & TCP_ACK)) {
                if (syn_cookie_verify(pkt->src_ip, pkt->dst_ip, pkt->src_port, pkt->dst_port, 0)) {
                    halfopen_count--;
                }
            }
            if (conn->state == TCP_ST_CLOSED) {
                conn_zero_conn(conn);
                conn_count--;
            }
            stateful_stats.packets_allowed++;
            return FW_ACTION_ALLOW;
        }
        /* No connection found and not SYN -> unsolicited, drop */
        stateful_stats.packets_dropped++;
        return FW_ACTION_DROP;
    }

    /* UDP / ICMP: allow */
    stateful_stats.packets_allowed++;
    return FW_ACTION_ALLOW;
}

/* ---- Timeout stale connections ---- */
void fw_stateful_timeout_stale(uint64_t max_age_ms) {
    for (uint32_t i = 0; i < MAX_CONNECTIONS; i++) {
        sec_conn_state_t *c = &connections[i];
        if (c->proto == 0) continue;
        if ((stateful_now - c->last_seen) > max_age_ms) {
            if (c->state == TCP_ST_ESTABLISHED) stateful_stats.connections_closed++;
            conn_zero_conn(c);
            conn_count--;
        }
    }
}

void fw_stateful_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&stateful_stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
