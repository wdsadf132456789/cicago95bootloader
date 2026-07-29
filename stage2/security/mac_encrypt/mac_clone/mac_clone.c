/**
 * Chicago-95 MAC Encrypter #2: MAC Cloner
 * Clones MAC address from a target device on the network
 * Detects active hosts via ARP and adopts their MAC
 */

#include "boot/security.h"

#define MAC_CLONE_MAX_TARGETS  8
#define MAC_CLONE_ARP_TIMEOUT  3000
#define MAC_CLONE_SCAN_DELAY   100

typedef struct {
    uint8_t  target_mac[6];
    uint8_t  target_ip[4];
    uint32_t last_seen;
    uint32_t rtt;
    uint8_t  active;
} clone_target_t;

typedef struct {
    uint8_t  original_mac[6];
    uint8_t  cloned_mac[6];
    uint8_t  current_ip[4];
    clone_target_t targets[MAC_CLONE_MAX_TARGETS];
    uint32_t target_count;
    uint32_t selected_target;
    uint8_t  is_cloned;
    uint8_t  auto_select;   /* Auto-select lowest-RTT active target */
    uint8_t  initialized;
    sec_stats_t stats;
} mac_clone_state_t;

static mac_clone_state_t mac_clone;

/* ---- ARP request builder ---- */
static uint32_t build_arp_request(uint8_t *out, const uint8_t *src_mac,
                                   const uint8_t *src_ip, const uint8_t *target_ip) {
    uint32_t pos = 0;

    /* Ethernet header: broadcast */
    for (uint32_t i = 0; i < 6; i++) out[pos++] = 0xFF;
    for (uint32_t i = 0; i < 6; i++) out[pos++] = src_mac[i];
    out[pos++] = 0x08; out[pos++] = 0x06; /* ARP */

    /* ARP header */
    out[pos++] = 0x00; out[pos++] = 0x01; /* Ethernet */
    out[pos++] = 0x08; out[pos++] = 0x00; /* IPv4 */
    out[pos++] = 0x06; /* HW addr len */
    out[pos++] = 0x04; /* Proto addr len */
    out[pos++] = 0x00; out[pos++] = 0x01; /* Request */

    /* Sender MAC */
    for (uint32_t i = 0; i < 6; i++) out[pos++] = src_mac[i];
    /* Sender IP */
    for (uint32_t i = 0; i < 4; i++) out[pos++] = src_ip[i];
    /* Target MAC (unknown) */
    for (uint32_t i = 0; i < 6; i++) out[pos++] = 0x00;
    /* Target IP */
    for (uint32_t i = 0; i < 4; i++) out[pos++] = target_ip[i];

    return pos;
}

/* ---- Parse ARP reply ---- */
static int parse_arp_reply(const uint8_t *frame, uint32_t len,
                           uint8_t out_mac[6], uint8_t out_ip[4]) {
    if (len < 42) return 0;

    /* Check ARP opcode = reply (0x02) */
    if (frame[14] != 0x00 || frame[15] != 0x02) return 0;

    /* Sender MAC at offset 22 */
    for (uint32_t i = 0; i < 6; i++) out_mac[i] = frame[22 + i];
    /* Sender IP at offset 28 */
    for (uint32_t i = 0; i < 4; i++) out_ip[i] = frame[28 + i];

    return 1;
}

/* ---- Init ---- */
int mac_clone_init(const uint8_t original_mac[6], const uint8_t local_ip[4]) {
    if (!original_mac) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&mac_clone;
    for (uint32_t i = 0; i < sizeof(mac_clone_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) mac_clone.original_mac[i] = original_mac[i];
    if (local_ip) {
        for (uint32_t i = 0; i < 4; i++) mac_clone.current_ip[i] = local_ip[i];
    }

    mac_clone.auto_select = 1;
    mac_clone.initialized = 1;
    return SEC_OK;
}

/* ---- Add target ---- */
int mac_clone_add_target(const uint8_t mac[6], const uint8_t ip[4]) {
    if (!mac || !ip) return SEC_ERR_BAD_PARAM;
    if (mac_clone.target_count >= MAC_CLONE_MAX_TARGETS) return SEC_ERR_NOMEM;

    uint32_t idx = mac_clone.target_count;
    for (uint32_t i = 0; i < 6; i++) mac_clone.targets[idx].target_mac[i] = mac[i];
    for (uint32_t i = 0; i < 4; i++) mac_clone.targets[idx].target_ip[i] = ip[i];
    mac_clone.targets[idx].active = 1;
    mac_clone.targets[idx].rtt = 0xFFFFFFFF;
    mac_clone.targets[idx].last_seen = 0;
    mac_clone.target_count++;

    return SEC_OK;
}

/* ---- Scan network for targets ---- */
int mac_clone_scan(boot_nic_t *nic) {
    if (!nic || !mac_clone.initialized) return SEC_ERR_BAD_PARAM;

    uint8_t src_mac[6];
    boot_nic_get_mac(nic, src_mac);

    /* ARP probe common IPs on subnet */
    uint8_t subnet[4];
    for (uint32_t i = 0; i < 3; i++) subnet[i] = mac_clone.current_ip[i];

    for (uint32_t host = 1; host < 255; host++) {
        subnet[3] = host;
        if (subnet[3] == mac_clone.current_ip[3]) continue; /* Skip self */

        uint8_t arp[64];
        uint32_t arp_len = build_arp_request(arp, src_mac, mac_clone.current_ip, subnet);

        boot_nic_send(nic, arp, arp_len);

        /* Wait briefly for reply */
        uint8_t reply[1514];
        uint32_t reply_len = sizeof(reply);
        int result = boot_nic_recv(nic, reply, &reply_len, MAC_CLONE_SCAN_DELAY);

        if (result == SEC_OK && reply_len >= 42) {
            uint8_t resp_mac[6], resp_ip[4];
            if (parse_arp_reply(reply, reply_len, resp_mac, resp_ip)) {
                mac_clone_add_target(resp_mac, resp_ip);
            }
        }
    }

    return SEC_OK;
}

/* ---- Select best target (lowest RTT, active) ---- */
int mac_clone_select_target(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    uint32_t best_idx = 0;
    uint32_t best_rtt = 0xFFFFFFFF;
    int found = 0;

    for (uint32_t i = 0; i < mac_clone.target_count; i++) {
        if (mac_clone.targets[i].active && mac_clone.targets[i].rtt < best_rtt) {
            best_rtt = mac_clone.targets[i].rtt;
            best_idx = i;
            found = 1;
        }
    }

    if (!found) return SEC_ERR_NOT_FOUND;

    mac_clone.selected_target = best_idx;
    for (uint32_t i = 0; i < 6; i++)
        mac_clone.cloned_mac[i] = mac_clone.targets[best_idx].target_mac[i];

    return SEC_OK;
}

/* ---- Apply cloned MAC ---- */
int mac_clone_apply(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;
    if (!mac_clone.is_cloned) {
        /* Auto-select if configured */
        if (mac_clone.auto_select && mac_clone.target_count > 0) {
            mac_clone_select_target(nic);
        }
    }

    if (mac_clone.is_cloned) {
        boot_nic_set_mac(nic, mac_clone.cloned_mac);
        mac_clone.stats.connections_opened++;
        return SEC_OK;
    }

    return SEC_ERR_STATE;
}

/* ---- Set specific clone target ---- */
int mac_clone_set_target(boot_nic_t *nic, const uint8_t target_mac[6]) {
    if (!nic || !target_mac) return SEC_ERR_BAD_PARAM;

    for (uint32_t i = 0; i < 6; i++)
        mac_clone.cloned_mac[i] = target_mac[i];
    mac_clone.is_cloned = 1;

    boot_nic_set_mac(nic, mac_clone.cloned_mac);
    return SEC_OK;
}

/* ---- Restore original ---- */
int mac_clone_restore(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, mac_clone.original_mac);
    mac_clone.is_cloned = 0;
    return SEC_OK;
}

void mac_clone_set_auto_select(int enable) {
    mac_clone.auto_select = enable ? 1 : 0;
}

void mac_clone_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&mac_clone.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
