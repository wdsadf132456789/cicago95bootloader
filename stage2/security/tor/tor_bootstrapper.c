/**
 * Chicago-95 Tor v3 Hidden Service Circuit Bootstrapper
 * Fetches consensus, builds 3-hop circuit, starts SOCKS5, publishes .onion
 */

#include <stdint.h>
#include "boot/security.h"
#include "security/tor.h"
#include "boot/ring0_init.h"

#define TOR_BOOTSTRAP_TIMEOUT_MS   30000
#define TOR_CIRCUIT_BUILD_MS       10000
#define TOR_CONSENSUS_RETRY_MS     5000
#define TOR_HS_PUBLISH_INTERVAL    60

static tor_bootstrap_state_t tor_boot;

/* Find a guard relay from consensus */
static int tor_select_guard(tor_consensus_t *cons, tor_relay_t *guard) {
    /* Scan for a guard flag relay */
    for (uint32_t i = 0; i < cons->relay_count && i < 256; i++) {
        tor_relay_t *r = &cons->relays[i];
        if (r->flags & 0x02) { /* is_guard */
            uint8_t *d = (uint8_t *)guard;
            const uint8_t *s = (const uint8_t *)r;
            for (uint32_t j = 0; j < sizeof(tor_relay_t); j++) d[j] = s[j];
            return 0;
        }
    }
    /* Fallback: use first relay if no guard flag */
    if (cons->relay_count > 0) {
        uint8_t *d = (uint8_t *)guard;
        const uint8_t *s = (const uint8_t *)&cons->relays[0];
        for (uint32_t j = 0; j < sizeof(tor_relay_t); j++) d[j] = s[j];
        return 0;
    }
    return -1;
}

/* Find an exit relay from consensus */
static int tor_select_exit(tor_consensus_t *cons, tor_relay_t *exit) {
    for (uint32_t i = 0; i < cons->relay_count && i < 256; i++) {
        tor_relay_t *r = &cons->relays[i];
        if (r->flags & 0x01) { /* is_exit */
            uint8_t *d = (uint8_t *)exit;
            const uint8_t *s = (const uint8_t *)r;
            for (uint32_t j = 0; j < sizeof(tor_relay_t); j++) d[j] = s[j];
            return 0;
        }
    }
    return -1;
}

/* Find a middle relay (not guard, not exit) */
static int tor_select_middle(tor_consensus_t *cons, tor_relay_t *mid) {
    for (uint32_t i = 0; i < cons->relay_count && i < 256; i++) {
        tor_relay_t *r = &cons->relays[i];
        uint8_t flags = r->flags;
        if (!(flags & 0x01) && !(flags & 0x02)) { /* not exit, not guard */
            uint8_t *d = (uint8_t *)mid;
            const uint8_t *s = (const uint8_t *)r;
            for (uint32_t j = 0; j < sizeof(tor_relay_t); j++) d[j] = s[j];
            return 0;
        }
    }
    /* Fallback: use any non-guard, non-exit, or just the second relay */
    if (cons->relay_count > 1) {
        uint8_t *d = (uint8_t *)mid;
        const uint8_t *s = (const uint8_t *)&cons->relays[1];
        for (uint32_t j = 0; j < sizeof(tor_relay_t); j++) d[j] = s[j];
        return 0;
    }
    return -1;
}

int tor_bootstrap_init(void) {
    uint8_t *d = (uint8_t *)&tor_boot;
    for (uint32_t i = 0; i < sizeof(tor_bootstrap_state_t); i++) d[i] = 0;

    /* Initialize Tor core */
    int rc = tor_init();
    if (rc != 0) return -1;

    tor_boot.state = TOR_BOOT_CONSENSUS;
    tor_boot.initialized = 1;
    return 0;
}

/* Main state machine - call repeatedly */
int tor_bootstrap_poll(void) {
    if (!tor_boot.initialized) return -1;

    switch (tor_boot.state) {
    case TOR_BOOT_CONSENSUS: {
        /* Fetch network consensus from directory authorities */
        static tor_consensus_t consensus;
        int rc = tor_directory_fetch_consensus(&consensus);
        if (rc != 0) {
            /* Retry later */
            return -2;
        }

        /* Store relay count for circuit building */
        tor_boot.state = TOR_BOOT_GUARD_SELECT;

        /* Fall through to guard selection */
        static tor_relay_t guard, mid, exit;

        if (tor_select_guard(&consensus, &guard) != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -3;
        }

        /* Copy guard fingerprint for display */
        for (uint32_t i = 0; i < 20; i++)
            tor_boot.guard_fingerprint[i] = guard.fingerprint[i];
        tor_boot.guard_ip[0] = (guard.addr_ipv4 >> 24) & 0xFF;
        tor_boot.guard_ip[1] = (guard.addr_ipv4 >> 16) & 0xFF;
        tor_boot.guard_ip[2] = (guard.addr_ipv4 >> 8) & 0xFF;
        tor_boot.guard_ip[3] = guard.addr_ipv4 & 0xFF;
        tor_boot.guard_or_port = guard.or_port;

        /* Select middle and exit relays */
        if (tor_select_middle(&consensus, &mid) != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -4;
        }
        if (tor_select_exit(&consensus, &exit) != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -5;
        }

        /* Create 3-hop circuit: guard -> middle -> exit */
        tor_circuit_t circ;
        uint8_t *c = (uint8_t *)&circ;
        for (uint32_t i = 0; i < sizeof(tor_circuit_t); i++) c[i] = 0;

        rc = tor_circuit_create(&circ, 0); /* purpose=general */
        if (rc != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -6;
        }

        /* Extend through guard (first hop) */
        rc = tor_circuit_extend(&circ, &guard);
        if (rc != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -7;
        }

        /* Extend through middle */
        rc = tor_circuit_extend(&circ, &mid);
        if (rc != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -8;
        }

        /* Extend through exit */
        rc = tor_circuit_extend(&circ, &exit);
        if (rc != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -9;
        }

        tor_boot.circuit_id = circ.circ_id;
        tor_boot.state = TOR_BOOT_CIRCUIT_READY;
        break;
    }

    case TOR_BOOT_CIRCUIT_READY: {
        /* Circuit is established - start SOCKS5 proxy */
        int rc = tor_socks5_init();
        if (rc != 0) {
            tor_boot.state = TOR_BOOT_FAILED;
            return -10;
        }
        tor_boot.state = TOR_BOOT_SOCKS5_START;
        break;
    }

    case TOR_BOOT_SOCKS5_START: {
        /* Initialize hidden service */
        tor_hidden_service_t hs;
        uint8_t *h = (uint8_t *)&hs;
        for (uint32_t i = 0; i < sizeof(tor_hidden_service_t); i++) h[i] = 0;

        int rc = tor_hs_init(&hs, 22); /* SSH port on .onion */
        if (rc == 0) {
            /* Copy .onion address */
            for (uint32_t i = 0; i < 64; i++)
                tor_boot.onion_addr[i] = hs.service_id[i];

            /* Publish hidden service to HSDirs */
            rc = tor_hs_publish(&hs);
            tor_boot.hs_published = (rc == 0) ? 1 : 0;
        }

        tor_boot.state = TOR_BOOT_READY;
        break;
    }

    case TOR_BOOT_READY:
        /* Normal operation */
        break;

    case TOR_BOOT_FAILED:
        return -1;

    default:
        break;
    }

    return 0;
}

int tor_bootstrap_is_ready(void) {
    return tor_boot.state == TOR_BOOT_READY;
}

const uint8_t *tor_bootstrap_get_onion_addr(void) {
    return tor_boot.onion_addr;
}

const tor_bootstrap_state_t *tor_bootstrap_get_state(void) {
    return &tor_boot;
}

uint32_t tor_bootstrap_get_circuit_id(void) {
    return tor_boot.circuit_id;
}
