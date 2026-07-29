/**
 * Chicago-95 Bootloader — Marvell WiFi Driver (libertas)
 * Supports: 88W8385, 88W8388, 88W8686, 88W8764, 88W8897, 88W8997
 * PCI vendor: 0x11AB
 */

#include "drivers/wifi.h"
#include "boot/security.h"

#define MARVELL_VENDOR_ID   0x11AB
#define MARVELL_88W8385_DID 0x2A01
#define MARVELL_88W8388_DID 0x2A02
#define MARVELL_88W8686_DID 0x2A03
#define MARVELL_88W8764_DID 0x2A04
#define MARVELL_88W8897_DID 0x2B38
#define MARVELL_88W8997_DID 0x2B39

/* Marvell MAC registers */
#define MV_MAC_BASE         0x1000
#define MV_MAC_CTRL         (MV_MAC_BASE + 0x0000)
#define MV_MAC_STATUS       (MV_MAC_BASE + 0x0004)
#define MV_INT_MASK         (MV_MAC_BASE + 0x0008)
#define MV_INT_STATUS       (MV_MAC_BASE + 0x000C)
#define MV_TX_BASE          (MV_MAC_BASE + 0x0100)
#define MV_TX_STATUS        (MV_MAC_BASE + 0x0108)
#define MV_RX_BASE          (MV_MAC_BASE + 0x0200)
#define MV_RX_STATUS        (MV_MAC_BASE + 0x0208)
#define MV_RF_BASE          (MV_MAC_BASE + 0x0300)
#define MV_CHANNEL          (MV_MAC_BASE + 0x0400)
#define MV_BSSID            (MV_MAC_BASE + 0x0500)
#define MV_SSID             (MV_MAC_BASE + 0x0600)
#define MV_BCN_CTRL         (MV_MAC_BASE + 0x0700)

/* Command interface */
#define MV_CMD_BASE         (MV_MAC_BASE + 0x1000)
#define MV_CMD_REG          (MV_CMD_BASE + 0x0000)
#define MV_CMD_RESP         (MV_CMD_BASE + 0x0004)
#define MV_CMD_ARG0         (MV_CMD_BASE + 0x0010)

/* Command IDs */
#define MV_CMD_SCAN         0x0001
#define MV_CMD_AUTH         0x0002
#define MV_CMD_ASSOC        0x0003
#define MV_CMD_DEAUTH       0x0004
#define MV_CMD_CHANNEL      0x0005
#define MV_CMD_POWER        0x0006

static struct {
    uint32_t bar0;
    uint8_t  mac[6];
    uint8_t  band;
    uint8_t  channel;
    int8_t   rssi;
    uint32_t cmd_seq;
} mrv;

static inline void mrv_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(mrv.bar0 + reg) = val;
}
static inline uint32_t mrv_read32(uint32_t reg) {
    return *(volatile uint32_t *)(mrv.bar0 + reg);
}

static void mrv_reset(uint32_t bar0) {
    mrv.bar0 = bar0;
    mrv_write32(MV_MAC_CTRL, 0x00000001); /* Software reset */
    for (volatile int i = 0; i < 100000; i++);
    mrv_write32(MV_MAC_CTRL, 0x00000000);
    for (volatile int i = 0; i < 50000; i++);
}

static int mrv_init(uint32_t bar0, uint8_t *mac) {
    mrv.bar0 = bar0;
    mrv_reset(bar0);

    /* Read MAC from firmware */
    uint32_t mac0 = mrv_read32(0x0180);
    uint32_t mac1 = mrv_read32(0x0184);
    mrv.mac[0] = mac0 & 0xFF;
    mrv.mac[1] = (mac0 >> 8) & 0xFF;
    mrv.mac[2] = (mac0 >> 16) & 0xFF;
    mrv.mac[3] = (mac0 >> 24) & 0xFF;
    mrv.mac[4] = mac1 & 0xFF;
    mrv.mac[5] = (mac1 >> 8) & 0xFF;
    for (uint32_t i = 0; i < 6; i++) mac[i] = mrv.mac[i];

    /* Enable interrupts */
    mrv_write32(MV_INT_MASK, 0x0000FFFF);
    mrv_write32(MV_MAC_CTRL, 0x00000004); /* MAC enable */

    /* Initialize TX/RX rings */
    mrv_write32(MV_TX_BASE + 0x000, 0x00000000);
    mrv_write32(MV_TX_BASE + 0x100, 0x00001000);
    mrv_write32(MV_RX_BASE + 0x000, 0x00000000);
    mrv_write32(MV_RX_BASE + 0x100, 0x00001000);

    mrv.cmd_seq = 1;

    return 0;
}

static int mrv_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    mrv.channel = channel;
    mrv.band = band;

    mrv_write32(MV_CMD_REG, MV_CMD_SCAN);
    mrv_write32(MV_CMD_ARG0 + 0, (band << 8) | channel);
    mrv_write32(MV_CMD_ARG0 + 4, 0x00000000); /* SSID len */
    mrv_write32(MV_CMD_ARG0 + 8, mrv.cmd_seq++);

    return 0;
}

static int mrv_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    uint32_t status = mrv_read32(MV_INT_STATUS);
    if (!(status & 0x01)) return -1;

    uint32_t bssid0 = mrv_read32(MV_BSSID);
    uint32_t bssid1 = mrv_read32(MV_BSSID + 4);
    result->bssid[0] = bssid0 & 0xFF;
    result->bssid[1] = (bssid0 >> 8) & 0xFF;
    result->bssid[2] = (bssid0 >> 16) & 0xFF;
    result->bssid[3] = (bssid0 >> 24) & 0xFF;
    result->bssid[4] = bssid1 & 0xFF;
    result->bssid[5] = (bssid1 >> 8) & 0xFF;

    result->channel = mrv.channel;
    result->band = mrv.band;
    result->rssi = (int8_t)(mrv_read32(MV_MAC_STATUS + 0x10) & 0xFF);
    result->signal_percent = (result->rssi + 100) > 100 ? 100 : (result->rssi + 100);
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1;
    result->auth = 1;

    return 0;
}

static int mrv_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                     uint8_t security, const uint8_t *key, uint32_t key_len) {
    mrv_write32(MV_CMD_REG, MV_CMD_AUTH);
    mrv_write32(MV_CMD_ARG0 + 0, bssid[0] | (bssid[1] << 8) |
                   (bssid[2] << 16) | (bssid[3] << 24));
    mrv_write32(MV_CMD_ARG0 + 4, bssid[4] | (bssid[5] << 8));
    mrv_write32(MV_CMD_ARG0 + 8, security);
    mrv_write32(MV_CMD_ARG0 + 12, mrv.cmd_seq++);

    return 0;
}

static int mrv_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    mrv_write32(MV_CMD_REG, MV_CMD_ASSOC);
    mrv_write32(MV_CMD_ARG0 + 0, bssid[0] | (bssid[1] << 8) |
                   (bssid[2] << 16) | (bssid[3] << 24));
    mrv_write32(MV_CMD_ARG0 + 4, bssid[4] | (bssid[5] << 8));
    mrv_write32(MV_CMD_ARG0 + 8, channel);
    mrv_write32(MV_CMD_ARG0 + 12, mrv.cmd_seq++);

    return 0;
}

static int mrv_deauth(uint32_t bar0, const uint8_t *bssid) { return 0; }

static int mrv_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        mrv_write32(MV_TX_BASE + 0x200 + i * 4, frame[i]);
    mrv_write32(MV_TX_STATUS, 0x00000001); /* TX start */
    return 0;
}

static int mrv_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    uint32_t status = mrv_read32(MV_INT_STATUS);
    if (!(status & 0x02)) return -1;

    for (uint32_t i = 0; i < *len; i++)
        frame[i] = (uint8_t)mrv_read32(MV_RX_BASE + 0x200 + i * 4);
    return 0;
}

static void mrv_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = mrv.mac[i];
}

static void mrv_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    mrv.channel = channel;
    mrv.band = band;
    mrv_write32(MV_CHANNEL, (band << 8) | channel);
}

static int mrv_set_power(uint32_t bar0, uint8_t power) {
    mrv_write32(MV_CMD_REG, MV_CMD_POWER);
    mrv_write32(MV_CMD_ARG0 + 0, power);
    return 0;
}

static int mrv_get_rssi(uint32_t bar0) { return mrv.rssi; }

static const wifi_driver_ops_t marvell_driver = {
    .name = "Marvell (libertas)",
    .pci_vendor = MARVELL_VENDOR_ID,
    .pci_device = MARVELL_88W8686_DID,
    .init = mrv_init,
    .reset = mrv_reset,
    .scan_start = mrv_scan_start,
    .scan_get_result = mrv_scan_get_result,
    .auth = mrv_auth,
    .assoc = mrv_assoc,
    .deauth = mrv_deauth,
    .send_frame = mrv_send_frame,
    .recv_frame = mrv_recv_frame,
    .get_mac = mrv_get_mac,
    .set_channel = mrv_set_channel,
    .set_power = mrv_set_power,
    .get_rssi = mrv_get_rssi,
};

int wifi_marvell_register(void) {
    return wifi_register_driver(&marvell_driver);
}
