/**
 * Chicago-95 Network Stack
 * Ethernet, ARP, IP, UDP, basic sockets API
 */

#include "net.h"
#include "../kernel.h"
#include "../kmalloc.h"
#include "e1000.h"

/* Forward declarations */
static void udp_handle_packet(uint32_t src_ip, const void *data, uint32_t len);
static void icmp_handle_packet(uint32_t src_ip, const void *data, uint32_t len);

static net_state_t net;

/* ---- Ethernet ---- */

int net_eth_send(const uint8_t dst[6], uint16_t ether_type, const void *data, uint32_t len) {
    uint8_t frame[1600];
    if (len + 14 > sizeof(frame)) return -1;

    __builtin_memcpy(frame + 0, dst, 6);
    __builtin_memcpy(frame + 6, net.mac, 6);
    frame[12] = (ether_type >> 8) & 0xFF;
    frame[13] = ether_type & 0xFF;
    __builtin_memcpy(frame + 14, data, len);

    return e1000_send(frame, len + 14);
}

/* ---- ARP ---- */

static arp_entry_t arp_cache[ARP_CACHE_SIZE];
static uint32_t arp_clock = 0;

static void arp_cache_add(uint32_t ip, const uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].ip == ip) {
            __builtin_memcpy(arp_cache[i].mac, mac, 6);
            arp_cache[i].age = arp_clock;
            return;
        }
    }
    /* Find oldest entry */
    int oldest = 0;
    uint32_t oldest_age = ~0U;
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (!arp_cache[i].valid) { oldest = i; break; }
        if (arp_cache[i].age < oldest_age) {
            oldest_age = arp_cache[i].age;
            oldest = i;
        }
    }
    arp_cache[oldest].ip = ip;
    __builtin_memcpy(arp_cache[oldest].mac, mac, 6);
    arp_cache[oldest].valid = 1;
    arp_cache[oldest].age = arp_clock;
}

int arp_lookup(uint32_t ip, uint8_t mac[6]) {
    for (int i = 0; i < ARP_CACHE_SIZE; i++) {
        if (arp_cache[i].valid && arp_cache[i].ip == ip) {
            __builtin_memcpy(mac, arp_cache[i].mac, 6);
            return 0;
        }
    }
    return -1;
}

static void arp_handle_request(const void *data, uint32_t len) {
    if (len < 28) return;
    const uint8_t *pkt = data;

    uint16_t opcode = (pkt[6] << 8) | pkt[7];
    uint32_t sender_ip = *(uint32_t *)(pkt + 14);
    uint8_t *sender_mac = (uint8_t *)(pkt + 8);

    /* Add to cache */
    arp_cache_add(sender_ip, sender_mac);

    if (opcode == 1) {  /* ARP Request */
        uint32_t target_ip = *(uint32_t *)(pkt + 24);
        if (target_ip == net.ip) {
            /* Build ARP reply */
            uint8_t reply[42];
            __builtin_memset(reply, 0, 42);

            /* Ethernet header */
            __builtin_memcpy(reply + 0, sender_mac, 6);
            __builtin_memcpy(reply + 6, net.mac, 6);
            reply[12] = 0x08; reply[13] = 0x06;

            /* ARP header */
            reply[14] = 0; reply[15] = 1;   /* Hardware type: Ethernet */
            reply[16] = 0x08; reply[17] = 0; /* Protocol: IPv4 */
            reply[18] = 6;  reply[19] = 4;  /* HW addr len, Proto addr len */
            reply[20] = 0; reply[21] = 2;   /* Opcode: ARP Reply */

            __builtin_memcpy(reply + 22, net.mac, 6);  /* Sender MAC */
            *(uint32_t *)(reply + 28) = net.ip;        /* Sender IP */
            __builtin_memcpy(reply + 32, sender_mac, 6); /* Target MAC */
            *(uint32_t *)(reply + 38) = sender_ip;     /* Target IP */

            net_eth_send(sender_mac, 0x0806, reply + 14, 28);
        }
    }
}

void arp_send_request(uint32_t target_ip) {
    uint8_t broadcast[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t pkt[28];
    __builtin_memset(pkt, 0, 28);

    pkt[0] = 0; pkt[1] = 1;  /* Hardware type: Ethernet */
    pkt[2] = 0x08; pkt[3] = 0;  /* Protocol: IPv4 */
    pkt[4] = 6; pkt[5] = 4;  /* HW addr len, Proto addr len */
    pkt[6] = 0; pkt[7] = 1;  /* Opcode: ARP Request */

    __builtin_memcpy(pkt + 8, net.mac, 6);   /* Sender MAC */
    *(uint32_t *)(pkt + 14) = net.ip;         /* Sender IP */
    __builtin_memset(pkt + 20, 0, 6);         /* Target MAC (unknown) */
    *(uint32_t *)(pkt + 24) = target_ip;      /* Target IP */

    net_eth_send(broadcast, 0x0806, pkt, 28);
}

/* ---- IP ---- */

static void ip_handle_packet(const void *data, uint32_t len) {
    if (len < 20) return;
    const uint8_t *pkt = data;

    uint8_t version = (pkt[0] >> 4) & 0xF;
    if (version != 4) return;

    uint16_t total_len = (pkt[2] << 8) | pkt[3];
    uint8_t protocol = pkt[9];
    uint32_t src_ip = *(uint32_t *)(pkt + 12);
    uint32_t dst_ip = *(uint32_t *)(pkt + 16);

    if (dst_ip != net.ip && dst_ip != 0xFFFFFFFF) return;

    uint8_t ihl = (pkt[0] & 0xF) * 4;
    const void *payload = pkt + ihl;
    uint32_t payload_len = total_len - ihl;

    if (protocol == 1) {       /* ICMP */
        icmp_handle_packet(src_ip, payload, payload_len);
    } else if (protocol == 17) {  /* UDP */
        udp_handle_packet(src_ip, payload, payload_len);
    }
}

/* ---- UDP ---- */

static udp_socket_t udp_sockets[UDP_MAX_SOCKETS];

int udp_socket(uint16_t port) {
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (!udp_sockets[i].bound) {
            udp_sockets[i].bound = 1;
            udp_sockets[i].port = port;
            udp_sockets[i].data_len = 0;
            return i;
        }
    }
    return -1;
}

void udp_close(int sock) {
    if (sock >= 0 && sock < UDP_MAX_SOCKETS) {
        udp_sockets[sock].bound = 0;
    }
}

int udp_send(int sock, uint32_t dst_ip, uint16_t dst_port, const void *data, uint32_t len) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS) return -1;
    if (!udp_sockets[sock].bound) return -1;

    /* Resolve destination MAC */
    uint8_t dst_mac[6];
    if (arp_lookup(dst_ip, dst_mac) < 0) {
        arp_send_request(dst_ip);
        return -1;  /* ARP not resolved yet */
    }

    uint16_t src_port = udp_sockets[sock].port;

    /* Build UDP packet */
    uint8_t pkt[1500];
    uint16_t udp_len = 8 + len;

    /* UDP header */
    pkt[0] = (src_port >> 8) & 0xFF;
    pkt[1] = src_port & 0xFF;
    pkt[2] = (dst_port >> 8) & 0xFF;
    pkt[3] = dst_port & 0xFF;
    pkt[4] = (udp_len >> 8) & 0xFF;
    pkt[5] = udp_len & 0xFF;
    pkt[6] = 0; pkt[7] = 0;  /* Checksum (optional for UDP over IPv4) */

    __builtin_memcpy(pkt + 8, data, len);

    /* Build IP header */
    uint8_t ip_hdr[20];
    __builtin_memset(ip_hdr, 0, 20);
    ip_hdr[0] = 0x45;  /* Version=4, IHL=5 */
    uint16_t total = 20 + udp_len;
    ip_hdr[2] = (total >> 8) & 0xFF;
    ip_hdr[3] = total & 0xFF;
    ip_hdr[6] = 0x40;  /* Don't Fragment */
    ip_hdr[8] = 64;    /* TTL */
    ip_hdr[9] = 17;    /* Protocol: UDP */
    *(uint32_t *)(ip_hdr + 12) = net.ip;
    *(uint32_t *)(ip_hdr + 16) = dst_ip;

    /* IP checksum */
    uint32_t sum = 0;
    for (int i = 0; i < 20; i += 2) {
        sum += (ip_hdr[i] << 8) | ip_hdr[i + 1];
    }
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    uint16_t csum = ~sum;
    ip_hdr[10] = (csum >> 8) & 0xFF;
    ip_hdr[11] = csum & 0xFF;

    /* Combine IP + UDP */
    uint8_t full_pkt[1500];
    __builtin_memcpy(full_pkt, ip_hdr, 20);
    __builtin_memcpy(full_pkt + 20, pkt, udp_len);

    return net_eth_send(dst_mac, 0x0800, full_pkt, 20 + udp_len);
}

int udp_recv(int sock, void *buf, uint32_t max_len) {
    if (sock < 0 || sock >= UDP_MAX_SOCKETS) return -1;
    udp_socket_t *s = &udp_sockets[sock];
    if (!s->bound || s->data_len == 0) return -1;

    uint32_t copy = s->data_len;
    if (copy > max_len) copy = max_len;
    __builtin_memcpy(buf, s->data, copy);
    s->data_len = 0;
    return (int)copy;
}

static void udp_handle_packet(uint32_t src_ip, const void *data, uint32_t len) {
    if (len < 8) return;
    const uint8_t *udp = data;
    uint16_t dst_port = (udp[2] << 8) | udp[3];
    uint16_t seg_len = (udp[4] << 8) | udp[5];

    /* Find matching socket */
    for (int i = 0; i < UDP_MAX_SOCKETS; i++) {
        if (udp_sockets[i].bound && udp_sockets[i].port == dst_port) {
            uint32_t copy = seg_len - 8;
            if (copy > sizeof(udp_sockets[i].data)) copy = sizeof(udp_sockets[i].data);
            __builtin_memcpy(udp_sockets[i].data, udp + 8, copy);
            udp_sockets[i].data_len = copy;
            udp_sockets[i].src_ip = src_ip;
            udp_sockets[i].src_port = (udp[0] << 8) | udp[1];
            return;
        }
    }
}

/* ---- ICMP (ping) ---- */

static int     ping_outstanding = 0;
static int     ping_reply_received = 0;

static uint16_t icmp_checksum(const uint8_t *data, uint32_t len) {
    uint32_t sum = 0;
    for (uint32_t i = 0; i + 1 < len; i += 2) {
        sum += (data[i] << 8) | data[i + 1];
    }
    if (len & 1) sum += data[len - 1] << 8;
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return ~sum;
}

/* Build an IPv4 packet with ICMP payload and send it */
static int ip_send(uint32_t dst_ip, uint8_t protocol, const void *icmp_data, uint32_t icmp_len) {
    uint8_t dst_mac[6];
    if (arp_lookup(dst_ip, dst_mac) < 0) {
        arp_send_request(dst_ip);
        return -1;
    }

    uint8_t ip_hdr[20];
    __builtin_memset(ip_hdr, 0, 20);
    ip_hdr[0] = 0x45;
    uint16_t total = 20 + icmp_len;
    ip_hdr[2] = (total >> 8) & 0xFF;
    ip_hdr[3] = total & 0xFF;
    ip_hdr[6] = 0x40;
    ip_hdr[8] = 64;
    ip_hdr[9] = protocol;
    *(uint32_t *)(ip_hdr + 12) = net.ip;
    *(uint32_t *)(ip_hdr + 16) = dst_ip;

    uint16_t csum = icmp_checksum(ip_hdr, 20);
    ip_hdr[10] = (csum >> 8) & 0xFF;
    ip_hdr[11] = csum & 0xFF;

    uint8_t full_pkt[1500];
    __builtin_memcpy(full_pkt, ip_hdr, 20);
    __builtin_memcpy(full_pkt + 20, icmp_data, icmp_len);

    return net_eth_send(dst_mac, 0x0800, full_pkt, 20 + icmp_len);
}

static void icmp_handle_packet(uint32_t src_ip, const void *data, uint32_t len) {
    if (len < 8) return;
    const uint8_t *icmp = data;

    if (icmp[0] == 8 && icmp[1] == 0) {  /* Echo Request */
        uint16_t id  = (icmp[4] << 8) | icmp[5];
        uint16_t seq = (icmp[6] << 8) | icmp[7];
        uint32_t payload_len = len - 8;

        /* Build echo reply */
        uint8_t reply[1500];
        reply[0] = 0;  /* Type: Echo Reply */
        reply[1] = 0;  /* Code */
        reply[2] = 0; reply[3] = 0;
        reply[4] = (id >> 8) & 0xFF;  reply[5] = id & 0xFF;
        reply[6] = (seq >> 8) & 0xFF; reply[7] = seq & 0xFF;
        if (payload_len > 0)
            __builtin_memcpy(reply + 8, icmp + 8, payload_len);

        uint16_t csum = icmp_checksum(reply, 8 + payload_len);
        reply[2] = (csum >> 8) & 0xFF;
        reply[3] = csum & 0xFF;

        ip_send(src_ip, 1, reply, 8 + payload_len);

    } else if (icmp[0] == 0 && icmp[1] == 0) {  /* Echo Reply */
        ping_reply_received = 1;
        ping_outstanding = 0;
    }
}

/* Send an ICMP Echo Request (outgoing ping) */
int net_send_ping(uint32_t dst_ip, uint16_t id, uint16_t seq) {
    uint8_t pkt[64];
    __builtin_memset(pkt, 0, sizeof(pkt));
    pkt[0] = 8;  /* Type: Echo Request */
    pkt[1] = 0;  /* Code */
    pkt[4] = (id >> 8) & 0xFF;  pkt[5] = id & 0xFF;
    pkt[6] = (seq >> 8) & 0xFF; pkt[7] = seq & 0xFF;

    uint16_t csum = icmp_checksum(pkt, 64);
    pkt[2] = (csum >> 8) & 0xFF;
    pkt[3] = csum & 0xFF;

    ping_outstanding = 1;
    ping_reply_received = 0;

    return ip_send(dst_ip, 1, pkt, 64);
}

int net_ping_pending(void)  { return ping_outstanding; }
int net_ping_reply_rx(void) { int r = ping_reply_received; ping_reply_received = 0; return r; }

/* ---- Main receive handler ---- */

void net_recv_frame(const void *frame, uint32_t len) {
    if (len < 14) return;
    const uint8_t *pkt = frame;

    uint16_t ether_type = (pkt[12] << 8) | pkt[13];

    if (ether_type == 0x0806) {  /* ARP */
        arp_handle_request(pkt + 14, len - 14);
    } else if (ether_type == 0x0800) {  /* IPv4 */
        ip_handle_packet(pkt + 14, len - 14);
    }
}

/* ---- Init ---- */

void net_init(void) {
    __builtin_memset(&net, 0, sizeof(net));
    __builtin_memset(arp_cache, 0, sizeof(arp_cache));
    __builtin_memset(udp_sockets, 0, sizeof(udp_sockets));

    /* Set default IP (192.168.1.100) */
    net.ip = (192) | (168 << 8) | (1 << 16) | (100 << 24);
    net.mask = 0x00FFFFFF;  /* 255.255.255.0 */
    net.gateway = (192) | (168 << 8) | (1 << 16) | (1 << 24);  /* 192.168.1.1 */

    /* Get MAC from e1000 */
    if (e1000_is_ready()) {
        e1000_get_mac(net.mac);
    }

    /* Send gratuitous ARP to announce our MAC */
    arp_send_request(net.ip);
}

/* ---- Socket API ---- */

int net_socket_create(void) {
    return 0;  /* UDP socket type */
}

void net_set_ip(uint32_t ip) {
    net.ip = ip;
}

uint32_t net_get_ip(void) {
    return net.ip;
}

void net_get_mac(uint8_t mac[6]) {
    __builtin_memcpy(mac, net.mac, 6);
}

uint32_t net_get_mask(void)    { return net.mask; }
uint32_t net_get_gateway(void) { return net.gateway; }

/* Poll the NIC for incoming frames */
void net_poll(void) {
    uint8_t buf[1600];
    int len;
    while ((len = e1000_recv(buf, sizeof(buf))) > 0) {
        net_recv_frame(buf, len);
    }
}
