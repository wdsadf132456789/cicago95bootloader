/**
 * Chicago-95 Gen-2 Firewall #4: Adaptive (Behavioral Anomaly Detection)
 * Baseline tracking, anomaly scoring, auto-blocking
 */

#include "boot/security.h"

#define MAX_TRACK_IPS      512
#define MAX_BLOCKLIST      128
#define BASELINE_WINDOW    100
#define EMA_ALPHA_FIXED    8      /* fixed-point: alpha = 8/256 = 0.03125 */
#define EMA_SHIFT          8

typedef struct {
    uint32_t ip;
    uint64_t pkts_per_window;
    uint64_t bytes_per_window;
    uint64_t connections_per_window;
    /* Exponential moving average */
    uint64_t ema_pkts;
    uint64_t ema_bytes;
    uint64_t ema_conns;
    /* Variance (fixed-point, squared deviations) */
    uint64_t var_pkts;
    uint64_t var_bytes;
    uint64_t var_conns;
    /* Counters for current window */
    uint64_t window_pkts;
    uint64_t window_bytes;
    uint64_t window_conns;
    uint32_t window_count;
    /* Scoring */
    uint32_t anomaly_score;
    uint32_t blocked_until;
    uint8_t  active;
} ip_profile_t;

typedef struct {
    uint32_t ip;
    uint64_t expires_at;
    uint8_t  active;
} blocklist_entry_t;

static ip_profile_t     profiles[MAX_TRACK_IPS];
static uint32_t         profile_count = 0;
static blocklist_entry_t blocklist[MAX_BLOCKLIST];
static uint32_t         blocklist_count = 0;
static sec_fw_rule_t    adaptive_rules[SEC_MAX_RULES];
static uint32_t         adaptive_rule_count = 0;
static sec_stats_t      adaptive_stats;
static uint8_t          adaptive_initialized = 0;
static uint64_t         adaptive_now = 0;

/* Configurable thresholds */
static uint32_t threshold_anomaly_score = 300;
static uint32_t threshold_block_duration_ms = 60000;
static uint32_t threshold_pkts_stddev_multiplier = 3;
static uint32_t threshold_bytes_stddev_multiplier = 3;

static uint64_t get_timestamp(void) {
    uint32_t lo, hi;
    asm volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

/* ---- Helpers ---- */
static int ip_in_blocklist(uint32_t ip) {
    for (uint32_t i = 0; i < MAX_BLOCKLIST; i++) {
        if (blocklist[i].active && blocklist[i].ip == ip) {
            if (adaptive_now < blocklist[i].expires_at) return 1;
            blocklist[i].active = 0;
        }
    }
    return 0;
}

static void ip_block(uint32_t ip) {
    for (uint32_t i = 0; i < MAX_BLOCKLIST; i++) {
        if (!blocklist[i].active) {
            blocklist[i].ip = ip;
            blocklist[i].expires_at = adaptive_now + (uint64_t)threshold_block_duration_ms * 100000;
            blocklist[i].active = 1;
            blocklist_count++;
            return;
        }
    }
}

static ip_profile_t *find_or_create_profile(uint32_t ip) {
    for (uint32_t i = 0; i < MAX_TRACK_IPS; i++) {
        if (profiles[i].active && profiles[i].ip == ip) return &profiles[i];
    }
    for (uint32_t i = 0; i < MAX_TRACK_IPS; i++) {
        if (!profiles[i].active) {
            profiles[i].ip = ip;
            profiles[i].active = 1;
            profiles[i].ema_pkts = 0;
            profiles[i].ema_bytes = 0;
            profiles[i].ema_conns = 0;
            profiles[i].var_pkts = 0;
            profiles[i].var_bytes = 0;
            profiles[i].var_conns = 0;
            profiles[i].window_pkts = 0;
            profiles[i].window_bytes = 0;
            profiles[i].window_conns = 0;
            profiles[i].window_count = 0;
            profiles[i].anomaly_score = 0;
            profiles[i].blocked_until = 0;
            profile_count++;
            return &profiles[i];
        }
    }
    return (ip_profile_t*)0;
}

/* ---- Init ---- */
int fw_adaptive_init(void) {
    for (uint32_t i = 0; i < MAX_TRACK_IPS; i++) profiles[i].active = 0;
    for (uint32_t i = 0; i < MAX_BLOCKLIST; i++) blocklist[i].active = 0;
    profile_count = 0;
    blocklist_count = 0;
    adaptive_rule_count = 0;
    adaptive_stats.packets_inspected = 0;
    adaptive_stats.packets_allowed = 0;
    adaptive_stats.packets_dropped = 0;
    adaptive_stats.packets_blocked = 0;
    adaptive_stats.errors = 0;
    adaptive_initialized = 1;
    return SEC_OK;
}

int fw_adaptive_add_rule(const sec_fw_rule_t *rule) {
    if (!rule || adaptive_rule_count >= SEC_MAX_RULES) return SEC_ERR_NOMEM;
    sec_fw_rule_t *dst = &adaptive_rules[adaptive_rule_count];
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)rule;
    for (uint32_t i = 0; i < sizeof(sec_fw_rule_t); i++) d[i] = s[i];
    adaptive_rule_count++;
    return SEC_OK;
}

/* ---- Update baseline ---- */
int fw_adaptive_update_baseline(void) {
    for (uint32_t i = 0; i < MAX_TRACK_IPS; i++) {
        ip_profile_t *p = &profiles[i];
        if (!p->active || p->window_count == 0) continue;

        uint64_t avg_pkts  = p->window_pkts  / p->window_count;
        uint64_t avg_bytes = p->window_bytes / p->window_count;
        uint64_t avg_conns = p->window_conns / p->window_count;

        /* EMA update: new_ema = alpha * sample + (1 - alpha) * old_ema */
        if (p->ema_pkts == 0) {
            p->ema_pkts  = avg_pkts;
            p->ema_bytes = avg_bytes;
            p->ema_conns = avg_conns;
        } else {
            p->ema_pkts  = (EMA_ALPHA_FIXED * avg_pkts  + (256 - EMA_ALPHA_FIXED) * p->ema_pkts)  >> EMA_SHIFT;
            p->ema_bytes = (EMA_ALPHA_FIXED * avg_bytes + (256 - EMA_ALPHA_FIXED) * p->ema_bytes) >> EMA_SHIFT;
            p->ema_conns = (EMA_ALPHA_FIXED * avg_conns + (256 - EMA_ALPHA_FIXED) * p->ema_conns) >> EMA_SHIFT;

            /* Variance update (exponential moving variance) */
            int64_t diff_pkts  = (int64_t)avg_pkts  - (int64_t)p->ema_pkts;
            int64_t diff_bytes = (int64_t)avg_bytes - (int64_t)p->ema_bytes;
            int64_t diff_conns = (int64_t)avg_conns - (int64_t)p->ema_conns;
            uint64_t abs_diff_pkts  = diff_pkts  >= 0 ? diff_pkts  : -diff_pkts;
            uint64_t abs_diff_bytes = diff_bytes >= 0 ? diff_bytes : -diff_bytes;
            uint64_t abs_diff_conns = diff_conns >= 0 ? diff_conns : -diff_conns;
            p->var_pkts  = (EMA_ALPHA_FIXED * (abs_diff_pkts  * abs_diff_pkts)  + (256 - EMA_ALPHA_FIXED) * p->var_pkts)  >> EMA_SHIFT;
            p->var_bytes = (EMA_ALPHA_FIXED * (abs_diff_bytes * abs_diff_bytes) + (256 - EMA_ALPHA_FIXED) * p->var_bytes) >> EMA_SHIFT;
            p->var_conns = (EMA_ALPHA_FIXED * (abs_diff_conns * abs_diff_conns) + (256 - EMA_ALPHA_FIXED) * p->var_conns) >> EMA_SHIFT;
        }

        /* Reset window */
        p->window_pkts = 0;
        p->window_bytes = 0;
        p->window_conns = 0;
        p->window_count = 0;
    }
    return SEC_OK;
}

/* ---- Anomaly detection ---- */
int fw_adaptive_detect_anomaly(const sec_packet_t *pkt) {
    if (!pkt) return 0;

    ip_profile_t *p = find_or_create_profile(pkt->src_ip);
    if (!p) return 0;

    /* Increment window counters */
    p->window_pkts++;
    p->window_bytes += pkt->payload_len + 40; /* approx header size */
    p->window_count = 1;

    /* If baseline not yet established (< 10 windows), allow */
    if (p->ema_pkts == 0) return 0;

    /* Calculate z-scores */
    uint64_t stddev_pkts  = p->var_pkts  ? 1 : 0;
    uint64_t stddev_bytes = p->var_bytes ? 1 : 0;
    uint64_t stddev_conns = p->var_conns ? 1 : 0;

    /* Integer square root approximation */
    {
        uint64_t v = p->var_pkts;
        uint64_t result = 0;
        uint64_t bit = 1ULL << 30;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= result + bit) { v -= result + bit; result = (result >> 1) + bit; }
            else result >>= 1;
            bit >>= 2;
        }
        stddev_pkts = result;
    }
    {
        uint64_t v = p->var_bytes;
        uint64_t result = 0;
        uint64_t bit = 1ULL << 30;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= result + bit) { v -= result + bit; result = (result >> 1) + bit; }
            else result >>= 1;
            bit >>= 2;
        }
        stddev_bytes = result;
    }
    {
        uint64_t v = p->var_conns;
        uint64_t result = 0;
        uint64_t bit = 1ULL << 30;
        while (bit > v) bit >>= 2;
        while (bit) {
            if (v >= result + bit) { v -= result + bit; result = (result >> 1) + bit; }
            else result >>= 1;
            bit >>= 2;
        }
        stddev_conns = result;
    }

    uint64_t diff_pkts  = pkt->payload_len > p->ema_pkts  ? pkt->payload_len - p->ema_pkts  : p->ema_pkts  - pkt->payload_len;
    uint64_t diff_bytes = (pkt->payload_len + 40) > p->ema_bytes ? (pkt->payload_len + 40) - p->ema_bytes : p->ema_bytes - (pkt->payload_len + 40);

    uint32_t score = 0;
    if (stddev_pkts > 0 && diff_pkts > stddev_pkts * threshold_pkts_stddev_multiplier) {
        score += (uint32_t)(diff_pkts / (stddev_pkts + 1)) * 100;
    }
    if (stddev_bytes > 0 && diff_bytes > stddev_bytes * threshold_bytes_stddev_multiplier) {
        score += (uint32_t)(diff_bytes / (stddev_bytes + 1)) * 100;
    }

    p->anomaly_score = score;

    if (score > threshold_anomaly_score) {
        ip_block(pkt->src_ip);
        adaptive_stats.packets_blocked++;
        return 1;
    }

    return 0;
}

/* ---- Main eval ---- */
int fw_adaptive_eval(const sec_packet_t *pkt) {
    if (!pkt) return FW_ACTION_DENY;
    adaptive_stats.packets_inspected++;

    adaptive_now = get_timestamp();

    /* Check blocklist */
    if (ip_in_blocklist(pkt->src_ip)) {
        adaptive_stats.packets_blocked++;
        return FW_ACTION_DROP;
    }

    /* Anomaly detection */
    if (fw_adaptive_detect_anomaly(pkt)) {
        return FW_ACTION_DROP;
    }

    adaptive_stats.packets_allowed++;
    return FW_ACTION_ALLOW;
}

/* ---- Set threshold ---- */
void fw_adaptive_set_threshold(uint32_t param, uint32_t value) {
    switch (param) {
        case 0: threshold_anomaly_score = value; break;
        case 1: threshold_block_duration_ms = value; break;
        case 2: threshold_pkts_stddev_multiplier = value; break;
        case 3: threshold_bytes_stddev_multiplier = value; break;
    }
}

void fw_adaptive_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&adaptive_stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
