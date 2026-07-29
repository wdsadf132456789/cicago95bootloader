/**
 * Chicago-95 Gen-2 Firewall #1: Packet Filter
 * Static L2/L3/L4 rule matching with priority ordering and rate limiting
 */

#include "boot/security.h"

#define MAX_FILTER_RULES    512
#define RATE_BUCKETS        256
#define RATE_WINDOW_MS      1000

typedef struct {
    uint64_t tokens;
    uint64_t last_refill;
    uint32_t rate;
    uint32_t burst;
} rate_bucket_t;

static sec_fw_rule_t filter_rules[MAX_FILTER_RULES];
static uint32_t      filter_rule_count = 0;
static rate_bucket_t rate_buckets[RATE_BUCKETS];
static sec_stats_t   filter_stats;
static uint8_t       filter_initialized = 0;

/* ---- Token bucket rate limiter ---- */
static void rate_refill(rate_bucket_t *b, uint64_t now) {
    uint64_t elapsed = now - b->last_refill;
    uint64_t refill  = (elapsed * b->rate) / RATE_WINDOW_MS;
    b->tokens += refill;
    if (b->tokens > b->burst) b->tokens = b->burst;
    b->last_refill = now;
}

static int rate_allow(rate_bucket_t *b) {
    uint64_t now = 0;
    { uint32_t lo, hi;
      asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
      now = ((uint64_t)hi << 32) | lo; }
    if (!b->last_refill) { b->last_refill = now; b->tokens = b->burst; }
    rate_refill(b, now);
    if (b->tokens > 0) { b->tokens--; return 1; }
    return 0;
}

/* ---- Init ---- */
int fw_packet_filter_init(void) {
    filter_rule_count = 0;
    filter_stats.packets_inspected = 0;
    filter_stats.packets_allowed  = 0;
    filter_stats.packets_dropped  = 0;
    filter_stats.packets_blocked  = 0;
    filter_stats.packets_logged   = 0;
    filter_stats.rate_limited     = 0;
    filter_stats.errors           = 0;
    filter_initialized = 1;
    return SEC_OK;
}

/* ---- Add rule ---- */
int fw_packet_filter_add_rule(const sec_fw_rule_t *rule) {
    if (!rule || filter_rule_count >= MAX_FILTER_RULES) return SEC_ERR_NOMEM;

    sec_fw_rule_t *dst = &filter_rules[filter_rule_count];
    /* Manual struct copy (no memcpy in bootloader without libc) */
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)rule;
    for (uint32_t i = 0; i < sizeof(sec_fw_rule_t); i++) d[i] = s[i];

    if (dst->rate_limit > 0) {
        uint32_t idx = dst->rule_id % RATE_BUCKETS;
        rate_buckets[idx].rate  = dst->rate_limit;
        rate_buckets[idx].burst = dst->burst_limit ? dst->burst_limit : dst->rate_limit * 2;
        rate_buckets[idx].tokens = rate_buckets[idx].burst;
        rate_buckets[idx].last_refill = 0;
    }

    filter_rule_count++;
    return SEC_OK;
}

/* ---- Remove rule ---- */
int fw_packet_filter_remove_rule(uint32_t rule_id) {
    for (uint32_t i = 0; i < filter_rule_count; i++) {
        if (filter_rules[i].rule_id == rule_id) {
            for (uint32_t j = i; j < filter_rule_count - 1; j++) {
                uint8_t *d = (uint8_t*)&filter_rules[j];
                const uint8_t *s = (const uint8_t*)&filter_rules[j + 1];
                for (uint32_t k = 0; k < sizeof(sec_fw_rule_t); k++) d[k] = s[k];
            }
            filter_rule_count--;
            return SEC_OK;
        }
    }
    return SEC_ERR_BAD_PARAM;
}

/* ---- Match a single rule against a packet ---- */
static int rule_matches(const sec_fw_rule_t *r, const sec_packet_t *pkt) {
    /* MAC match */
    for (int i = 0; i < 6; i++) {
        if (r->match_src_mac[i] && (pkt->src_mac[i] & r->mac_mask[i]) != (r->match_src_mac[i] & r->mac_mask[i]))
            return 0;
        if (r->match_dst_mac[i] && (pkt->dst_mac[i] & r->mac_mask[i]) != (r->match_dst_mac[i] & r->mac_mask[i]))
            return 0;
    }
    /* IP match */
    if (r->src_ip && (pkt->src_ip & r->src_mask) != (r->src_ip & r->src_mask)) return 0;
    if (r->dst_ip && (pkt->dst_ip & r->dst_mask) != (r->dst_ip & r->dst_mask)) return 0;
    /* Port match */
    if (r->src_port && pkt->src_port != r->src_port) return 0;
    if (r->dst_port && pkt->dst_port != r->dst_port) return 0;
    /* Protocol match */
    if (r->protocol && pkt->ip_proto != r->protocol) return 0;
    /* TCP flags match */
    if (r->tcp_flags_mask && (pkt->tcp_flags & r->tcp_flags_mask) != (r->tcp_flags & r->tcp_flags_mask))
        return 0;
    /* Interface match */
    if (r->match_iface && pkt->iface_id != r->match_iface) return 0;
    return 1;
}

/* ---- Evaluate packet against all rules (sorted by priority) ---- */
int fw_packet_filter_eval(const sec_packet_t *pkt) {
    if (!pkt) { filter_stats.errors++; return FW_ACTION_DENY; }
    filter_stats.packets_inspected++;

    int best_action = FW_ACTION_DENY;
    uint32_t best_priority = 0;
    int found = 0;

    for (uint32_t i = 0; i < filter_rule_count; i++) {
        const sec_fw_rule_t *r = &filter_rules[i];
        if (!r->enabled) continue;
        if (r->priority < best_priority && found) continue;
        if (rule_matches(r, pkt)) {
            /* Rate limit check */
            if (r->rate_limit > 0) {
                uint32_t idx = r->rule_id % RATE_BUCKETS;
                if (!rate_allow(&rate_buckets[idx])) {
                    filter_stats.rate_limited++;
                    return FW_ACTION_DROP;
                }
            }
            best_action = r->action;
            best_priority = r->priority;
            found = 1;
        }
    }

    if (found) {
        switch (best_action) {
            case FW_ACTION_ALLOW: filter_stats.packets_allowed++; break;
            case FW_ACTION_DENY:
            case FW_ACTION_DROP:  filter_stats.packets_dropped++; break;
            case FW_ACTION_LOG:   filter_stats.packets_logged++;  break;
            case FW_ACTION_RESET: filter_stats.packets_blocked++; break;
            default: break;
        }
    } else {
        filter_stats.packets_dropped++;
    }

    return best_action;
}

/* ---- Flush all rules ---- */
void fw_packet_filter_flush(void) {
    filter_rule_count = 0;
}

/* ---- Stats ---- */
void fw_packet_filter_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&filter_stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
