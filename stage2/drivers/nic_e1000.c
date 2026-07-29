/**
 * Chicago-95 Bootloader - NIC Driver
 * Intel E1000 (e1000) PCI Ethernet driver for boot-time network
 */

#include "boot/security.h"

/* E1000 Register offsets */
#define E1000_CTRL       0x00000
#define E1000_STATUS     0x00008
#define E1000_EERD       0x00014
#define E1000_ICR        0x000C0
#define E1000_IMS        0x000D0
#define E1000_RCTL       0x00100
#define E1000_TCTL       0x00400
#define E1000_RDBAL      0x02800
#define E1000_RDBAH      0x02804
#define E1000_RDLEN      0x02808
#define E1000_RDH        0x02810
#define E1000_RDT        0x02818
#define E1000_TDBAL      0x03800
#define E1000_TDBAH      0x03804
#define E1000_TDLEN      0x03808
#define E1000_TDH        0x03810
#define E1000_TDT        0x03818
#define E1000_MTA        0x05200
#define E1000_RAL        0x05400
#define E1000_RAH        0x05404
#define E1000_STATUS     0x00008

/* E1000 constants */
#define E1000_CTRL_RST   0x04000000
#define E1000_CTRL_SLU   0x00000040
#define E1000_RCTL_EN    0x00000002
#define E1000_RCTL_BAM   0x00008000
#define E1000_RCTL_BSIZE 0x00000000
#define E1000_TCTL_EN    0x00000002
#define E1000_TCTL_PSP   0x00000008
#define E1000_STATUS_LU  0x00000002

#define RX_DESC_COUNT    128
#define TX_DESC_COUNT    128
#define RX_BUFFER_SIZE   2048
#define TX_BUFFER_SIZE   2048

/* Descriptor structures */
typedef struct {
    uint64_t addr;
    uint16_t length;
    uint16_t csum;
    uint8_t  status;
    uint8_t  errors;
    uint16_t special;
} __attribute__((packed)) e1000_rx_desc_t;

typedef struct {
    uint64_t addr;
    uint16_t length;
    uint8_t  cso;
    uint8_t  cmd;
    uint8_t  status;
    uint8_t  css;
    uint16_t special;
} __attribute__((packed)) e1000_tx_desc_t;

/* Driver state */
typedef struct {
    uint32_t  io_base;
    uint8_t   mac[6];
    uint8_t   irq;

    /* Receive */
    e1000_rx_desc_t *rx_descs;
    uint8_t         *rx_buffers[RX_DESC_COUNT];
    uint32_t        rx_tail;

    /* Transmit */
    e1000_tx_desc_t *tx_descs;
    uint8_t         *tx_buffers[TX_DESC_COUNT];
    uint32_t        tx_tail;

    /* Link */
    uint8_t         link_up;
    uint32_t        speed;

    /* Stats */
    uint64_t        packets_sent;
    uint64_t        packets_recv;
    uint64_t        errors;
} e1000_state_t;

static e1000_state_t e1000;

/* ---- PCI config space ---- */
static inline void pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset, uint32_t value) {
    uint32_t addr = (1 << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    asm volatile("outl %0, %%dx" : : "a"(addr), "d"(0xCF8));
    asm volatile("outl %0, %%dx" : : "a"(value), "d"(0xCFC));
}

static inline uint32_t pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t offset) {
    uint32_t addr = (1 << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) | ((uint32_t)func << 8) | (offset & 0xFC);
    asm volatile("outl %0, %%dx" : : "a"(addr), "d"(0xCF8));
    uint32_t result;
    asm volatile("inl %%dx, %0" : "=a"(result) : "d"(0xCFC));
    return result;
}

/* ---- E1000 register I/O ---- */
static inline void e1000_write(uint32_t reg, uint32_t value) {
    uint16_t port = (uint16_t)(e1000.io_base + reg);
    asm volatile("outl %0, %%dx" : : "a"(value), "d"(port));
}

static inline uint32_t e1000_read(uint32_t reg) {
    uint32_t val;
    uint16_t port = (uint16_t)(e1000.io_base + reg);
    asm volatile("inl %%dx, %0" : "=a"(val) : "d"(port));
    return val;
}

/* ---- EEPROM read ---- */
static uint16_t e1000_eeprom_read(uint8_t addr) {
    uint32_t data = 0;
    uint16_t result = 0;

    e1000_write(E1000_EERD, (addr << 2) | 0x01);

    for (uint32_t i = 0; i < 1000; i++) {
        data = e1000_read(E1000_EERD);
        if (data & 0x10) {
            result = (data >> 16) & 0xFFFF;
            break;
        }
    }

    return result;
}

/* ---- Find E1000 NIC via PCI ---- */
static int e1000_find_pci(uint8_t *bus_out, uint8_t *dev_out) {
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t val = pci_read(bus, dev, 0, 0);
            uint16_t vendor = val & 0xFFFF;
            uint16_t device = (val >> 16) & 0xFFFF;

            /* Intel E1000 */
            if (vendor == 0x8086 && (device == 0x100E || device == 0x1502 ||
                device == 0x1503 || device == 0x153A || device == 0x1533)) {
                /* Enable bus mastering */
                uint32_t cmd = pci_read(bus, dev, 0, 0x04);
                cmd |= (1 << 2);  /* Bus Master Enable */
                cmd |= (1 << 1);  /* Memory Space Enable */
                pci_write(bus, dev, 0, 0x04, cmd);

                /* Get BAR0 (memory-mapped I/O) */
                uint32_t bar0 = pci_read(bus, dev, 0, 0x10);
                e1000.io_base = bar0 & ~0x0F;

                /* Get IRQ */
                uint32_t ifr = pci_read(bus, dev, 0, 0x3C);
                e1000.irq = ifr & 0xFF;

                *bus_out = bus;
                *dev_out = dev;
                return 1;
            }
        }
    }
    return 0;
}

/* ---- Memory allocation (simple bump allocator) ---- */
static uint32_t bump_alloc_pos = 0x200000;  /* Start at 2MB */

static void *bump_alloc(uint32_t size, uint32_t align) {
    uint32_t aligned = (bump_alloc_pos + align - 1) & ~(align - 1);
    void *ptr = (void*)aligned;
    bump_alloc_pos = aligned + size;
    return ptr;
}

/* ---- Init ---- */
int boot_nic_init(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    uint8_t bus, dev;
    if (!e1000_find_pci(&bus, &dev)) {
        return SEC_ERR_NOT_FOUND;
    }

    /* MAC address from EEPROM */
    for (uint32_t i = 0; i < 3; i++) {
        uint16_t val = e1000_eeprom_read(i);
        e1000.mac[i * 2] = val & 0xFF;
        e1000.mac[i * 2 + 1] = (val >> 8) & 0xFF;
    }

    /* Software reset */
    e1000_write(E1000_CTRL, E1000_CTRL_RST);
    for (volatile uint32_t i = 0; i < 100000; i++) {}

    /* Re-read MAC after reset */
    for (uint32_t i = 0; i < 3; i++) {
        uint16_t val = e1000_eeprom_read(i);
        e1000.mac[i * 2] = val & 0xFF;
        e1000.mac[i * 2 + 1] = (val >> 8) & 0xFF;
    }

    /* Set MAC address in RAL/RAH */
    uint32_t ral = e1000.mac[0] | (e1000.mac[1] << 8) |
                  (e1000.mac[2] << 16) | (e1000.mac[3] << 24);
    uint32_t rah = e1000.mac[4] | (e1000.mac[5] << 8) | (1 << 31);
    e1000_write(E1000_RAL, ral);
    e1000_write(E1000_RAH, rah);

    /* Clear multicast table */
    for (uint32_t i = 0; i < 128; i++) e1000_write(E1000_MTA + i * 4, 0);

    /* Setup receive descriptors */
    e1000.rx_descs = bump_alloc(sizeof(e1000_rx_desc_t) * RX_DESC_COUNT, 16);
    for (uint32_t i = 0; i < RX_DESC_COUNT; i++) {
        e1000.rx_buffers[i] = bump_alloc(RX_BUFFER_SIZE, 16);
        e1000.rx_descs[i].addr = (uint64_t)e1000.rx_buffers[i];
        e1000.rx_descs[i].status = 0;
    }
    e1000.rx_tail = 0;

    e1000_write(E1000_RDBAL, (uint32_t)(uint64_t)e1000.rx_descs);
    e1000_write(E1000_RDBAH, 0);
    e1000_write(E1000_RDLEN, sizeof(e1000_rx_desc_t) * RX_DESC_COUNT);
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, RX_DESC_COUNT - 1);

    /* Setup transmit descriptors */
    e1000.tx_descs = bump_alloc(sizeof(e1000_tx_desc_t) * TX_DESC_COUNT, 16);
    for (uint32_t i = 0; i < TX_DESC_COUNT; i++) {
        e1000.tx_buffers[i] = bump_alloc(TX_BUFFER_SIZE, 16);
        e1000.tx_descs[i].addr = (uint64_t)e1000.tx_buffers[i];
        e1000.tx_descs[i].status = 0;
    }
    e1000.tx_tail = 0;

    e1000_write(E1000_TDBAL, (uint32_t)(uint64_t)e1000.tx_descs);
    e1000_write(E1000_TDBAH, 0);
    e1000_write(E1000_TDLEN, sizeof(e1000_tx_desc_t) * TX_DESC_COUNT);
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);

    /* Enable receiver */
    e1000_write(E1000_RCTL, E1000_RCTL_EN | E1000_RCTL_BAM | E1000_RCTL_BSIZE);

    /* Enable transmitter */
    e1000_write(E1000_TCTL, E1000_TCTL_EN | E1000_TCTL_PSP);

    /* Set link up */
    e1000_write(E1000_CTRL, E1000_CTRL_SLU);

    /* Check link status */
    uint32_t status = e1000_read(E1000_STATUS);
    e1000.link_up = (status & E1000_STATUS_LU) ? 1 : 0;
    e1000.speed = (status & 0x40) ? 1000 : (status & 0x20) ? 100 : 10;

    /* Fill nic struct */
    for (uint32_t i = 0; i < 6; i++) nic->mac[i] = e1000.mac[i];
    nic->driver_data = (void*)(uint64_t)&e1000;

    return SEC_OK;
}

/* ---- Get MAC ---- */
void boot_nic_get_mac(boot_nic_t *nic, uint8_t mac[6]) {
    if (!nic || !mac) return;
    for (uint32_t i = 0; i < 6; i++) mac[i] = e1000.mac[i];
}

/* ---- Send frame ---- */
int boot_nic_send(boot_nic_t *nic, const uint8_t *frame, uint32_t len) {
    if (!nic || !frame || len == 0) return SEC_ERR_BAD_PARAM;
    if (len > TX_BUFFER_SIZE) return SEC_ERR_BAD_PARAM;

    uint32_t tail = e1000.tx_tail;

    /* Copy frame to TX buffer */
    for (uint32_t i = 0; i < len; i++) e1000.tx_buffers[tail][i] = frame[i];

    e1000.tx_descs[tail].length = len;
    e1000.tx_descs[tail].cmd = 0x08 | 0x01; /* RS | EOP */
    e1000.tx_descs[tail].status = 0;

    e1000.tx_tail = (tail + 1) % TX_DESC_COUNT;
    e1000_write(E1000_TDT, e1000.tx_tail);

    e1000.packets_sent++;
    return SEC_OK;
}

/* ---- Receive frame ---- */
int boot_nic_recv(boot_nic_t *nic, uint8_t *frame, uint32_t *len, uint32_t timeout_ms) {
    if (!nic || !frame || !len) return SEC_ERR_BAD_PARAM;

    uint32_t head = e1000.rx_tail;

    /* Wait for descriptor to be filled */
    uint32_t elapsed = 0;
    while (!(e1000.rx_descs[head].status & 0x01)) {
        if (elapsed >= timeout_ms) return SEC_ERR_TIMEOUT;
        for (volatile uint32_t i = 0; i < 1000; i++) {}
        elapsed++;
    }

    uint16_t pkt_len = e1000.rx_descs[head].length;
    if (pkt_len > *len) pkt_len = *len;

    for (uint32_t i = 0; i < pkt_len; i++) frame[i] = e1000.rx_buffers[head][i];

    /* Clear descriptor status */
    e1000.rx_descs[head].status = 0;
    e1000.rx_tail = (head + 1) % RX_DESC_COUNT;
    e1000_write(E1000_RDT, e1000.rx_tail);

    *len = pkt_len;
    e1000.packets_recv++;
    return SEC_OK;
}

/* ---- Set MAC (for test purposes) ---- */
void boot_nic_set_mac(boot_nic_t *nic, const uint8_t mac[6]) {
    if (!nic || !mac) return;
    for (uint32_t i = 0; i < 6; i++) e1000.mac[i] = mac[i];

    uint32_t ral = mac[0] | (mac[1] << 8) | (mac[2] << 16) | (mac[3] << 24);
    uint32_t rah = mac[4] | (mac[5] << 8) | (1 << 31);
    e1000_write(E1000_RAL, ral);
    e1000_write(E1000_RAH, rah);
}

/* ---- Get link status ---- */
int boot_nic_get_link(boot_nic_t *nic) {
    if (!nic) return 0;
    uint32_t status = e1000_read(E1000_STATUS);
    e1000.link_up = (status & E1000_STATUS_LU) ? 1 : 0;
    return e1000.link_up;
}
