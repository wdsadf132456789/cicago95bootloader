#ifndef E1000_H
#define E1000_H

#include <stdint.h>

/* e1000 MMIO Register Offsets */
#define E1000_CTRL     0x0000   /* Device Control */
#define E1000_STATUS   0x0008   /* Device Status */
#define E1000_EERD     0x0014   /* EEPROM Read */
#define E1000_ICR      0x00C0   /* Interrupt Cause Read */
#define E1000_IMS      0x00D0   /* Interrupt Mask Set */

/* TX Registers */
#define E1000_TXCTL    0x0040   /* TX Control */
#define E1000_TDBAL    0x3800   /* TX Descriptor Base Low */
#define E1000_TDBAH    0x3804   /* TX Descriptor Base High */
#define E1000_TDLEN    0x3808   /* TX Descriptor Length */
#define E1000_TDH      0x3810   /* TX Descriptor Head */
#define E1000_TDT      0x3818   /* TX Descriptor Tail */

/* RX Registers */
#define E1000_RXCTL    0x0100   /* RX Control */
#define E1000_RDBAL    0x2800   /* RX Descriptor Base Low */
#define E1000_RDBAH    0x2804   /* RX Descriptor Base High */
#define E1000_RDLEN    0x2808   /* RX Descriptor Length */
#define E1000_RDH      0x2810   /* RX Descriptor Head */
#define E1000_RDT      0x2818   /* RX Descriptor Tail */

/* Receive Address */
#define E1000_RAL      0x5400
#define E1000_RAH      0x5404

/* CTRL bits */
#define E1000_CTRL_RST  (1 << 26)

/* ICR bits */
#define E1000_ICR_TXDW  (1 << 0)   /* TX Descriptor Written Back */
#define E1000_ICR_RXT0  (1 << 7)   /* RX Timer Interrupt */

#define E1000_TX_RING_SIZE 16
#define E1000_RX_RING_SIZE 16
#define E1000_BUF_SIZE     2048

/* Descriptor structure (16 bytes, same for TX and RX) */
typedef struct {
    uint32_t addr;        /* Buffer address (low 32 bits) */
    uint32_t addr_hi;     /* Buffer address (high 32 bits) */
    uint16_t length;      /* Packet length */
    uint16_t cso;         /* Checksum Offset */
    uint8_t  status;      /* Status (DD bit = bit 0) */
    uint8_t  errors;
    uint16_t special;     /* Special/VLAN */
    uint8_t  cmd;         /* TX: command flags */
    uint8_t  _reserved[3];
} __attribute__((aligned(16))) e1000_desc_t;

typedef struct {
    volatile uint32_t *mmio;
    uint8_t  mac[6];
    int      initialized;

    /* TX ring */
    e1000_desc_t *tx_ring;
    uint8_t *tx_buffers[E1000_TX_RING_SIZE];
    uint16_t tx_head;
    uint16_t tx_tail;

    /* RX ring */
    e1000_desc_t *rx_ring;
    uint8_t *rx_buffers[E1000_RX_RING_SIZE];
    uint16_t rx_head;
    uint16_t rx_tail;
} e1000_state_t;

int  e1000_init(uint16_t bus, uint8_t dev, uint8_t func);
int  e1000_send(const void *data, uint32_t len);
int  e1000_recv(void *buf, uint32_t max_len);
void e1000_get_mac(uint8_t mac[6]);
int  e1000_is_ready(void);

#endif
