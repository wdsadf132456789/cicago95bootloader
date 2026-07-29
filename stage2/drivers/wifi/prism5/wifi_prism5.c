/**
 * Chicago-95 Bootloader — Prism5 (Intersil) WiFi Driver
 * Supports: ISL38xx, ISL3877, ISL3880, ISL3886, ISL3887, ISL3890
 * PCI vendor: 0x1260
 */

#include "drivers/wifi.h"
#include "boot/security.h"

#define PRISM5_VENDOR_ID    0x1260
#define ISL3877_DID         0x3877
#define ISL3880_DID         0x3880
#define ISL3886_DID         0x3886
#define ISL3887_DID         0x3887
#define ISL3890_DID         0x3890

/* ISL38xx registers */
#define PRISM_BASE          0x0000
#define PRISM_HI_CTL        (PRISM_BASE + 0x0000)
#define PRISM_HI_STATUS     (PRISM_BASE + 0x0004)
#define PRISM_HI_TXCMPQ     (PRISM_BASE + 0x0008)
#define PRISM_HI_RXQ        (PRISM_BASE + 0x0010)
#define PRISM_HI_ADDRDMA    (PRISM_BASE + 0x0018)
#define PRISM_HI_GEN_CTL    (PRISM_BASE + 0x001C)

/* Control/Status Interface */
#define PRISM_CMD_BASE      (PRISM_BASE + 0x0100)
#define PRISM_CMD_REQ       (PRISM_CMD_BASE + 0x0000)
#define PRISM_CMD_STAT      (PRISM_CMD_BASE + 0x0004)
#define PRISM_CMD_RESP      (PRISM_CMD_BASE + 0x0008)
#define PRISM_CMD_CODE      (PRISM_CMD_BASE + 0x000C)
#define PRISM_CMD_ARGS      (PRISM_CMD_BASE + 0x0010)

/* Information Element IDs */
#define PRISM_IE_SSID       0
#define PRISM_IE_RATES      1
#define PRISM_IE_CHANNEL    3
#define PRISM_IE_ENCRYPT    5
#define PRISM_IE_BMODE      6

static struct {
    uint32_t bar0;
    uint8_t  mac[6];
    uint8_t  band;
    uint8_t  channel;
    int8_t   rssi;
    uint32_t firmware_ver;
} prism;

static inline void prism_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(prism.bar0 + reg) = val;
}
static inline uint32_t prism_read32(uint32_t reg) {
    return *(volatile uint32_t *)(prism.bar0 + reg);
}

static void prism_reset(uint32_t bar0) {
    prism.bar0 = bar0;
    prism_write32(PRISM_HI_CTL, 0x00000002); /* Reset */
    for (volatile int i = 0; i < 100000; i++);
    prism_write32(PRISM_HI_CTL, 0x00000000);
    for (volatile int i = 0; i < 100000; i++);
}

static int prism_init(uint32_t bar0, uint8_t *mac) {
    prism.bar0 = bar0;
    prism_reset(bar0);

    /* Read MAC from flash */
    uint32_t mac0 = prism_read32(0x0180);
    uint32_t mac1 = prism_read32(0x0184);
    prism.mac[0] = mac0 & 0xFF;
    prism.mac[1] = (mac0 >> 8) & 0xFF;
    prism.mac[2] = (mac0 >> 16) & 0xFF;
    prism.mac[3] = (mac0 >> 24) & 0xFF;
    prism.mac[4] = mac1 & 0xFF;
    prism.mac[5] = (mac1 >> 8) & 0xFF;
    for (uint32_t i = 0; i < 6; i++) mac[i] = prism.mac[i];

    /* Initialize host interface */
    prism_write32(PRISM_HI_GEN_CTL, 0x00000000);
    prism_write32(PRISM_HI_CTL, 0x00000001);

    /* Download firmware */
    prism_write32(PRISM_CMD_REQ, 0x00000001);
    prism.firmware_ver = prism_read32(PRISM_CMD_RESP);

    return 0;
}

static int prism_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    prism.channel = channel;
    prism.band = band;

    /* Build scan command */
    prism_write32(PRISM_CMD_CODE, 0x00000001); /* SCAN_REQ */
    prism_write32(PRISM_CMD_ARGS + 0, (band << 8) | channel);
    prism_write32(PRISM_CMD_ARGS + 4, 0x00000000); /* SSID len */
    prism_write32(PRISM_CMD_ARGS + 8, 0x00000000); /* Scan type */
    prism_write32(PRISM_CMD_REQ, 0x00000001);

    return 0;
}

static int prism_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    uint32_t stat = prism_read32(PRISM_CMD_STAT);
    if (!(stat & 0x01)) return -1;

    uint32_t bssid0 = prism_read32(PRISM_CMD_ARGS + 0x10);
    uint32_t bssid1 = prism_read32(PRISM_CMD_ARGS + 0x14);
    result->bssid[0] = bssid0 & 0xFF;
    result->bssid[1] = (bssid0 >> 8) & 0xFF;
    result->bssid[2] = (bssid0 >> 16) & 0xFF;
    result->bssid[3] = (bssid0 >> 24) & 0xFF;
    result->bssid[4] = bssid1 & 0xFF;
    result->bssid[5] = (bssid1 >> 8) & 0xFF;

    result->channel = prism.channel;
    result->band = prism.band;
    result->rssi = (int8_t)(prism_read32(PRISM_CMD_ARGS + 0x20) & 0xFF);
    result->signal_percent = (result->rssi + 100) > 100 ? 100 : (result->rssi + 100);
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1;
    result->auth = 1;

    return 0;
}

static int prism_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                       uint8_t security, const uint8_t *key, uint32_t key_len) {
    prism_write32(PRISM_CMD_CODE, 0x00000003); /* AUTH */
    prism_write32(PRISM_CMD_ARGS + 0, bssid[0] | (bssid[1] << 8) |
                   (bssid[2] << 16) | (bssid[3] << 24));
    prism_write32(PRISM_CMD_ARGS + 4, bssid[4] | (bssid[5] << 8));
    prism_write32(PRISM_CMD_ARGS + 8, security);
    prism_write32(PRISM_CMD_REQ, 0x00000001);

    return 0;
}

static int prism_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    prism_write32(PRISM_CMD_CODE, 0x00000004); /* ASSOC */
    prism_write32(PRISM_CMD_ARGS + 0, bssid[0] | (bssid[1] << 8) |
                   (bssid[2] << 16) | (bssid[3] << 24));
    prism_write32(PRISM_CMD_ARGS + 4, bssid[4] | (bssid[5] << 8));
    prism_write32(PRISM_CMD_ARGS + 8, channel);
    prism_write32(PRISM_CMD_REQ, 0x00000001);

    return 0;
}

static int prism_deauth(uint32_t bar0, const uint8_t *bssid) { return 0; }

static int prism_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    prism_write32(PRISM_HI_TXCMPQ, len);
    for (uint32_t i = 0; i < len; i++)
        prism_write32(PRISM_HI_TXCMPQ + 4 + i * 4, frame[i]);
    prism_write32(PRISM_HI_CTL, prism_read32(PRISM_HI_CTL) | 0x01);
    return 0;
}

static int prism_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    uint32_t status = prism_read32(PRISM_HI_STATUS);
    if (!(status & 0x01)) return -1;

    uint32_t rx_len = prism_read32(PRISM_HI_RXQ);
    if (rx_len > *len) rx_len = *len;

    for (uint32_t i = 0; i < rx_len; i++)
        frame[i] = (uint8_t)prism_read32(PRISM_HI_RXQ + 4 + i * 4);
    *len = rx_len;

    return 0;
}

static void prism_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = prism.mac[i];
}

static void prism_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    prism.channel = channel;
    prism.band = band;
}

static int prism_set_power(uint32_t bar0, uint8_t power) { return 0; }
static int prism_get_rssi(uint32_t bar0) { return prism.rssi; }

static const wifi_driver_ops_t prism5_driver = {
    .name = "Prism5 (Intersil)",
    .pci_vendor = PRISM5_VENDOR_ID,
    .pci_device = ISL3890_DID,
    .init = prism_init,
    .reset = prism_reset,
    .scan_start = prism_scan_start,
    .scan_get_result = prism_scan_get_result,
    .auth = prism_auth,
    .assoc = prism_assoc,
    .deauth = prism_deauth,
    .send_frame = prism_send_frame,
    .recv_frame = prism_recv_frame,
    .get_mac = prism_get_mac,
    .set_channel = prism_set_channel,
    .set_power = prism_set_power,
    .get_rssi = prism_get_rssi,
};

int wifi_prism5_register(void) {
    return wifi_register_driver(&prism5_driver);
}
