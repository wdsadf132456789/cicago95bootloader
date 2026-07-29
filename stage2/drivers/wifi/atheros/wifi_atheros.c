/**
 * Chicago-95 Bootloader — Qualcomm Atheros WiFi Driver (ath9k / ath10k)
 * Supports: AR92xx, AR93xx, AR94xx, QCA988x, QCA9984, QCA6174
 * PCI vendor: 0x168C
 */

#include "drivers/wifi.h"
#include "boot/security.h"

#define ATH_VENDOR_ID       0x168C
#define AR9285_DID          0x002B
#define AR9287_DID          0x002E
#define AR9300_DID          0x0030
#define AR9485_DID          0x0032
#define AR9462_DID          0x0034
#define AR9580_DID          0x0036
#define AR9590_DID          0x0037
#define QCA988X_DID         0x003C
#define QCA9984_DID         0x0046
#define QCA6174_DID         0x003E
#define QCA9887_DID         0x0050

/* AR9300 MAC registers */
#define AR_MAC_BASE          0x4000
#define AR_CFG               (AR_MAC_BASE + 0x00)
#define AR_IER               (AR_MAC_BASE + 0x08)
#define AR_TXCFG             (AR_MAC_BASE + 0x028)
#define AR_RXCFG             (AR_MAC_BASE + 0x030)
#define AR_ISR               (AR_MAC_BASE + 0x020)
#define AR_RCFG              (AR_MAC_BASE + 0x024)

/* PHY registers */
#define AR_PHY_BASE          0x9800
#define AR_PHY_MODE           (AR_PHY_BASE + 0x10)
#define AR_PHY_TXPOWER        (AR_PHY_BASE + 0x20)
#define AR_PHY_RF_CTL         (AR_PHY_BASE + 0x30)

static struct {
    uint32_t bar0;
    uint8_t  mac[6];
    uint8_t  is_ht;          /* 802.11n */
    uint8_t  is_vht;         /* 802.11ac */
    uint8_t  band;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  associated;
    uint8_t  bssid[6];
} ath;

static inline void ath_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(ath.bar0 + reg) = val;
}
static inline uint32_t ath_read32(uint32_t reg) {
    return *(volatile uint32_t *)(ath.bar0 + reg);
}

static void ath_reset(uint32_t bar0) {
    ath.bar0 = bar0;
    ath_write32(AR_CFG, ath_read32(AR_CFG) | (1 << 0));
    for (volatile int i = 0; i < 20000; i++);
    ath_write32(AR_CFG, ath_read32(AR_CFG) & ~(1 << 0));
}

static int ath_init(uint32_t bar0, uint8_t *mac) {
    ath.bar0 = bar0;
    ath_reset(bar0);

    /* Read MAC from EEPROM */
    uint32_t mac0 = ath_read32(0x4020);
    uint32_t mac1 = ath_read32(0x4024);
    ath.mac[0] = mac0 & 0xFF;
    ath.mac[1] = (mac0 >> 8) & 0xFF;
    ath.mac[2] = (mac0 >> 16) & 0xFF;
    ath.mac[3] = (mac0 >> 24) & 0xFF;
    ath.mac[4] = mac1 & 0xFF;
    ath.mac[5] = (mac1 >> 8) & 0xFF;
    for (uint32_t i = 0; i < 6; i++) mac[i] = ath.mac[i];

    /* Enable interrupts */
    ath_write32(AR_IER, 1);
    ath_write32(AR_TXCFG, 0x10);
    ath_write32(AR_RXCFG, 0x10);

    ath.is_ht = 1;
    ath.is_vht = (ath_read32(0x4000) & 0xF000) == 0xD000;
    ath.associated = 0;

    return 0;
}

static int ath_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    ath.channel = channel;
    ath.band = band;

    /* Set channel in PHY */
    uint32_t phy_mode = ath_read32(AR_PHY_MODE);
    phy_mode &= ~0x3F;
    phy_mode |= (band == WIFI_BAND_2GHZ) ? 0x01 : 0x04;
    ath_write32(AR_PHY_MODE, phy_mode);

    /* Trigger passive scan */
    ath_write32(AR_MAC_BASE + 0x100, (channel << 2) | 0x01);
    ath_write32(AR_MAC_BASE + 0x104, 0x03);

    return 0;
}

static int ath_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    uint32_t status = ath_read32(AR_MAC_BASE + 0x108);
    if (!(status & 0x01)) return -1;

    uint32_t bssid_lo = ath_read32(AR_MAC_BASE + 0x110);
    uint32_t bssid_hi = ath_read32(AR_MAC_BASE + 0x114);
    result->bssid[0] = bssid_lo & 0xFF;
    result->bssid[1] = (bssid_lo >> 8) & 0xFF;
    result->bssid[2] = (bssid_lo >> 16) & 0xFF;
    result->bssid[3] = (bssid_lo >> 24) & 0xFF;
    result->bssid[4] = bssid_hi & 0xFF;
    result->bssid[5] = (bssid_hi >> 8) & 0xFF;

    result->channel = ath.channel;
    result->band = ath.band;
    result->rssi = (int8_t)ath_read32(AR_MAC_BASE + 0x120);
    result->signal_percent = (result->rssi + 100) > 100 ? 100 : (result->rssi + 100);
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1;
    result->auth = 1;

    return 0;
}

static int ath_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                     uint8_t security, const uint8_t *key, uint32_t key_len) {
    /* Send auth frame */
    uint8_t frame[256];
    uint32_t ftype = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_AUTH;
    frame[0] = 0x00; frame[1] = ftype;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = ath.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];
    frame[20] = 0x00; frame[21] = 0x00; /* Seq num */
    frame[22] = 0x00; frame[23] = 0x00; /* Auth algo */
    frame[24] = 0x01; frame[25] = 0x00; /* Auth seq */

    for (uint32_t i = 0; i < 26; i++)
        ath_write32(0x800 + i * 4, frame[i]);
    ath_write32(AR_TXCFG, ath_read32(AR_TXCFG) | 0x01);

    return 0;
}

static int ath_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    uint8_t frame[256];
    uint32_t ftype = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_ASSOC_REQ;
    frame[0] = 0x00; frame[1] = ftype;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = ath.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];

    for (uint32_t i = 0; i < 24; i++)
        ath_write32(0x800 + i * 4, frame[i]);
    ath_write32(AR_TXCFG, ath_read32(AR_TXCFG) | 0x01);

    for (uint32_t i = 0; i < 6; i++) ath.bssid[i] = bssid[i];
    return 0;
}

static int ath_deauth(uint32_t bar0, const uint8_t *bssid) {
    return 0;
}

static int ath_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        ath_write32(0x800 + i * 4, frame[i]);
    ath_write32(AR_TXCFG, ath_read32(AR_TXCFG) | 0x01);
    return 0;
}

static int ath_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    uint32_t status = ath_read32(AR_ISR);
    if (!(status & 0x02)) return -1;

    uint32_t rx_len = *len;
    for (uint32_t i = 0; i < rx_len; i++)
        frame[i] = (uint8_t)ath_read32(0x900 + i * 4);

    return 0;
}

static void ath_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = ath.mac[i];
}

static void ath_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    ath.channel = channel;
    ath.band = band;
}

static int ath_set_power(uint32_t bar0, uint8_t power) { return 0; }
static int ath_get_rssi(uint32_t bar0) { return ath.rssi; }

static const wifi_driver_ops_t atheros_driver = {
    .name = "Qualcomm Atheros (ath9k/ath10k)",
    .pci_vendor = ATH_VENDOR_ID,
    .pci_device = AR9300_DID,
    .init = ath_init,
    .reset = ath_reset,
    .scan_start = ath_scan_start,
    .scan_get_result = ath_scan_get_result,
    .auth = ath_auth,
    .assoc = ath_assoc,
    .deauth = ath_deauth,
    .send_frame = ath_send_frame,
    .recv_frame = ath_recv_frame,
    .get_mac = ath_get_mac,
    .set_channel = ath_set_channel,
    .set_power = ath_set_power,
    .get_rssi = ath_get_rssi,
};

int wifi_atheros_register(void) {
    return wifi_register_driver(&atheros_driver);
}
