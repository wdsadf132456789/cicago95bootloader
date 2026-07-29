#ifndef NET_H
#define NET_H

#include <stdint.h>

#define ARP_CACHE_SIZE  32
#define UDP_MAX_SOCKETS 16

/* ARP cache entry */
typedef struct {
    uint32_t ip;
    uint8_t  mac[6];
    uint8_t  valid;
    uint32_t age;
} arp_entry_t;

/* UDP socket */
typedef struct {
    int      bound;
    uint16_t port;
    uint32_t src_ip;
    uint16_t src_port;
    uint8_t  data[1500];
    uint32_t data_len;
} udp_socket_t;

/* Network state */
typedef struct {
    uint8_t  mac[6];
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
} net_state_t;

/* Ethernet */
int  net_eth_send(const uint8_t dst[6], uint16_t ether_type, const void *data, uint32_t len);
void net_recv_frame(const void *frame, uint32_t len);

/* ARP */
int  arp_lookup(uint32_t ip, uint8_t mac[6]);
void arp_send_request(uint32_t target_ip);

/* UDP */
int  udp_socket(uint16_t port);
void udp_close(int sock);
int  udp_send(int sock, uint32_t dst_ip, uint16_t dst_port, const void *data, uint32_t len);
int  udp_recv(int sock, void *buf, uint32_t max_len);

/* ICMP */
int  net_send_ping(uint32_t dst_ip, uint16_t id, uint16_t seq);
int  net_ping_pending(void);
int  net_ping_reply_rx(void);

/* Polling */
void net_poll(void);

/* Init */
void net_init(void);
void net_set_ip(uint32_t ip);
uint32_t net_get_ip(void);
uint32_t net_get_mask(void);
uint32_t net_get_gateway(void);
void net_get_mac(uint8_t mac[6]);

#endif
