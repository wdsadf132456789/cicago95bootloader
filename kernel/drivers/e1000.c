/**
 * Chicago-95 Intel e1000 NIC Driver
 * MMIO-based, polling TX/RX rings, basic send/receive
 */

#include "../kernel.h"
#include "../kmalloc.h"
#include "../pci.h"
#include "e1000.h"

static e1000_state_t e1000;

/* MMIO helpers */
static uint32_t e1000_read(uint32_t reg) {
    return e1000.mmio[reg / 4];
}

static void e1000_write(uint32_t reg, uint32_t val) {
    e1000.mmio[reg / 4] = val;
}

/* EEPROM read (if present) */
static uint16_t e1000_eeprom_read(uint8_t addr) {
    e1000_write(E1000_EERD, (1 << 4) | ((uint32_t)addr << 2));
    for (volatile int i = 0; i < 10000; i++) {
        uint32_t val = e1000_read(E1000_EERD);
        if (val & (1 << 4)) {
            return (val >> 16) & 0xFFFF;
        }
    }
    return 0;
}

/* Read MAC address from EEPROM or receive address registers */
static void e1000_read_mac(void) {
    /* Try EEPROM first */
    uint16_t mac0 = e1000_eeprom_read(0);
    uint16_t mac1 = e1000_eeprom_read(1);
    uint16_t mac2 = e1000_eeprom_read(2);

    if (mac0 != 0 && mac0 != 0xFFFF) {
        e1000.mac[0] = mac0 & 0xFF;
        e1000.mac[1] = (mac0 >> 8) & 0xFF;
        e1000.mac[2] = mac1 & 0xFF;
        e1000.mac[3] = (mac1 >> 8) & 0xFF;
        e1000.mac[4] = mac2 & 0xFF;
        e1000.mac[5] = (mac2 >> 8) & 0xFF;
    } else {
        /* No EEPROM — read from RAL/RAH registers */
        uint32_t ral = e1000_read(E1000_RAL);
        uint32_t rah = e1000_read(E1000_RAH);
        e1000.mac[0] = ral & 0xFF;
        e1000.mac[1] = (ral >> 8) & 0xFF;
        e1000.mac[2] = (ral >> 16) & 0xFF;
        e1000.mac[3] = (ral >> 24) & 0xFF;
        e1000.mac[4] = rah & 0xFF;
        e1000.mac[5] = (rah >> 8) & 0xFF;
    }
}

/* Initialize e1000 device at given PCI location */
int e1000_init(uint16_t bus, uint8_t dev, uint8_t func) {
    __builtin_memset(&e1000, 0, sizeof(e1000));

    /* Read BAR0 (memory BAR) */
    uint32_t addr_pci = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8);
    outl(0xCF8, addr_pci | 0x10);
    uint32_t bar0 = inl(0xCFC);
    if (bar0 == 0 || bar0 == 0xFFFFFFFF) return -1;

    uint64_t mmio_phys = bar0 & ~0xF;
    e1000.mmio = (volatile uint32_t *)(uint64_t)mmio_phys;

    /* Enable bus mastering + memory space */
    outl(0xCF8, addr_pci | 0x04);
    uint16_t cmd = inw(0xCFC);
    cmd |= (1 << 2) | (1 << 1);  /* Bus Master + Memory Space */
    outw(0xCFC, cmd);

    /* Disable interrupts during init */
    e1000_write(E1000_IMS, 0);

    /* Reset device */
    e1000_write(E1000_CTRL, E1000_CTRL_RST);
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(e1000_read(E1000_CTRL) & E1000_CTRL_RST)) break;
    }

    /* Read MAC address */
    e1000_read_mac();

    /* Set receive address */
    uint32_t ral = e1000.mac[0] | (e1000.mac[1] << 8) |
                   (e1000.mac[2] << 16) | (e1000.mac[3] << 24);
    uint32_t rah = e1000.mac[4] | (e1000.mac[5] << 8);
    e1000_write(E1000_RAL, ral);
    e1000_write(E1000_RAH, rah | (1 << 31));  /* AV (Address Valid) */

    /* Allocate TX descriptor ring (16 entries) */
    e1000.tx_ring = (e1000_desc_t *)kmalloc_zero(E1000_TX_RING_SIZE * sizeof(e1000_desc_t));
    if (!e1000.tx_ring) return -1;

    /* Allocate TX buffers */
    for (int i = 0; i < E1000_TX_RING_SIZE; i++) {
        e1000.tx_buffers[i] = (uint8_t *)kmalloc_zero(E1000_BUF_SIZE);
    }

    /* Set TX descriptor base address */
    uint64_t tx_phys = (uint64_t)e1000.tx_ring;
    e1000_write(E1000_TDBAL, (uint32_t)tx_phys);
    e1000_write(E1000_TDBAH, (uint32_t)(tx_phys >> 32));
    e1000_write(E1000_TDLEN, E1000_TX_RING_SIZE * sizeof(e1000_desc_t));
    e1000_write(E1000_TDH, 0);
    e1000_write(E1000_TDT, 0);
    e1000.tx_tail = 0;
    e1000.tx_head = 0;

    /* Allocate RX descriptor ring (16 entries) */
    e1000.rx_ring = (e1000_desc_t *)kmalloc_zero(E1000_RX_RING_SIZE * sizeof(e1000_desc_t));
    if (!e1000.rx_ring) return -1;

    /* Allocate RX buffers and set descriptor buffer addresses */
    for (int i = 0; i < E1000_RX_RING_SIZE; i++) {
        e1000.rx_buffers[i] = (uint8_t *)kmalloc_zero(E1000_BUF_SIZE);
        uint64_t buf_phys = (uint64_t)e1000.rx_buffers[i];
        e1000.rx_ring[i].addr = (uint32_t)buf_phys;
        e1000.rx_ring[i].addr_hi = (uint32_t)(buf_phys >> 32);
    }

    /* Set RX descriptor base address */
    uint64_t rx_phys = (uint64_t)e1000.rx_ring;
    e1000_write(E1000_RDBAL, (uint32_t)rx_phys);
    e1000_write(E1000_RDBAH, (uint32_t)(rx_phys >> 32));
    e1000_write(E1000_RDLEN, E1000_RX_RING_SIZE * sizeof(e1000_desc_t));
    e1000_write(E1000_RDH, 0);
    e1000_write(E1000_RDT, E1000_RX_RING_SIZE - 1);
    e1000.rx_head = 0;
    e1000.rx_tail = E1000_RX_RING_SIZE - 1;

    /* Enable TX: CT=512, COLD=Full Duplex */
    e1000_write(E1000_TXCTL, (1 << 1) |  /* TX Enable */
                               (1 << 3) |  /* Pad Short Packets */
                               (3 << 4));  /* CT = 512 bytes */

    /* Enable RX */
    e1000_write(E1000_RXCTL, (1 << 1)   /* RX Enable */
                            | (1 << 2)   /* Strip Ethernet CRC */
                            | (1 << 4)   /* Broadcast Enable */
                            | (15 << 16) /* BSIZE = 4096+16 */
                            | (1 << 26));/* BAM (Broadcast Accept Mode) */

    /* Set interrupt mask: RX + TX */
    e1000_write(E1000_IMS, E1000_ICR_RXT0 | E1000_ICR_TXDW);

    e1000.initialized = 1;
    return 0;
}

/* Send a raw Ethernet frame */
int e1000_send(const void *data, uint32_t len) {
    if (!e1000.initialized) return -1;
    if (len > E1000_BUF_SIZE) return -1;

    uint16_t tail = e1000.tx_tail;

    /* Copy data to TX buffer */
    __builtin_memcpy(e1000.tx_buffers[tail], data, len);

    /* Set up descriptor */
    uint64_t buf_phys = (uint64_t)e1000.tx_buffers[tail];
    e1000.tx_ring[tail].addr = (uint32_t)buf_phys;
    e1000.tx_ring[tail].addr_hi = (uint32_t)(buf_phys >> 32);
    e1000.tx_ring[tail].length = len;
    e1000.tx_ring[tail].status = 0;
    e1000.tx_ring[tail].cmd = (1 << 0) |  /* EOP (End of Packet) */
                               (1 << 1) |  /* IFCS (Insert FCS) */
                               (1 << 3);   /* RS (Report Status) */

    /* Advance tail */
    e1000.tx_tail = (tail + 1) % E1000_TX_RING_SIZE;
    e1000_write(E1000_TDT, e1000.tx_tail);

    /* Wait for TX completion */
    for (volatile int i = 0; i < 1000000; i++) {
        if (e1000.tx_ring[tail].status & 1) break;
    }

    return 0;
}

/* Receive a raw Ethernet frame. Returns length or -1 if none available. */
int e1000_recv(void *buf, uint32_t max_len) {
    if (!e1000.initialized) return -1;

    uint16_t head = e1000.rx_head;

    /* Check if there's a packet available (DD bit set) */
    if (!(e1000.rx_ring[head].status & 1)) return -1;

    uint32_t len = e1000.rx_ring[head].length;
    if (len > max_len) len = max_len;

    /* Copy data from RX buffer */
    __builtin_memcpy(buf, e1000.rx_buffers[head], len);

    /* Reset descriptor */
    e1000.rx_ring[head].status = 0;
    e1000.rx_ring[head].length = 0;
    uint64_t buf_phys = (uint64_t)e1000.rx_buffers[head];
    e1000.rx_ring[head].addr = (uint32_t)buf_phys;
    e1000.rx_ring[head].addr_hi = (uint32_t)(buf_phys >> 32);

    /* Advance head */
    e1000.rx_head = (head + 1) % E1000_RX_RING_SIZE;
    e1000_write(E1000_RDT, e1000.rx_head);

    return (int)len;
}

/* Get MAC address */
void e1000_get_mac(uint8_t mac[6]) {
    __builtin_memcpy(mac, e1000.mac, 6);
}

/* Check if device is initialized */
int e1000_is_ready(void) {
    return e1000.initialized;
}
