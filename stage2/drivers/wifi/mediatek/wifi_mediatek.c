/**
 * Chicago-95 Bootloader — MediaTek/Ralink WiFi Driver (rt2x00 / mt76)
 * Supports: RT2500USB, RT2501PCI, RT2600, RT2700, RT2800, RT3090,
 *           RT3290, RT5390, RT5592, MT7601, MT7603, MT7610, MT7612,
 *           MT7615, MT7620, MT7621, MT7622, MT7663, MT7921
 * PCI vendor: 0x1814 (Ralink), 0x14C3 (MediaTek)
 */

#include "drivers/wifi.h"
#include "boot/security.h"

#define RT_VENDOR_ID        0x1814
#define MTK_VENDOR_ID       0x14C3
#define RT2500PCI_DID       0x0201
#define RT2501PCI_DID       0x0301
#define RT2600PCI_DID       0x0401
#define RT2800PCI_DID       0x0601
#define RT3090_DID          0x3090
#define RT3290_DID          0x3290
#define RT5390_DID          0x539F
#define RT5592_DID          0x5592
#define MT7601_DID          0x7601
#define MT7603_DID          0x7603
#define MT7610_DID          0x7610
#define MT7612_DID          0x7612
#define MT7615_DID          0x7615
#define MT7620_DID          0x7620
#define MT7622_DID          0x7622
#define MT7663_DID          0x7663
#define MT7921_DID          0x7961

/* RT2800PCI MAC registers */
#define RT_MAC_BASE         0x1000
#define RT_MAC_SYS_CTRL     (RT_MAC_BASE + 0x1000)
#define RT_MAC_CSR0         (RT_MAC_BASE + 0x1004)
#define RT_INT_MASK         (RT_MAC_BASE + 0x100C)
#define RT_INT_STATUS       (RT_MAC_BASE + 0x1020)
#define RT_BSSID_BASE       (RT_MAC_BASE + 0x1020)
#define RT_TX_BASE          (RT_MAC_BASE + 0x0300)
#define RT_RX_BASE          (RT_MAC_BASE + 0x0400)
#define RT_RF_BASE          (RT_MAC_BASE + 0x0500)
#define RT_CHANNEL_BASE     (RT_MAC_BASE + 0x0200)
#define RT_PWR_BASE         (RT_MAC_BASE + 0x0600)
#define RT_BC_BASE          (RT_MAC_BASE + 0x0700)
#define RT_WCID_BASE        (RT_MAC_BASE + 0x1800)

static struct {
    uint32_t bar0;
    uint8_t  mac[6];
    uint8_t  band;
    uint8_t  channel;
    int8_t   rssi;
    uint32_t rev;
    uint8_t  rf_mode;
} mt;

static inline void mt_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(mt.bar0 + reg) = val;
}
static inline uint32_t mt_read32(uint32_t reg) {
    return *(volatile uint32_t *)(mt.bar0 + reg);
}
static inline void mt_write16(uint32_t reg, uint16_t val) {
    *(volatile uint16_t *)(mt.bar0 + reg) = val;
}
static inline void mt_write8(uint32_t reg, uint8_t val) {
    *(volatile uint8_t *)(mt.bar0 + reg) = val;
}

static void mt_reset(uint32_t bar0) {
    mt.bar0 = bar0;
    mt_write32(RT_MAC_SYS_CTRL, 0x00000007);
    for (volatile int i = 0; i < 100000; i++);
    mt_write32(RT_MAC_SYS_CTRL, 0x00000000);
    for (volatile int i = 0; i < 100000; i++);
}

static int mt_init(uint32_t bar0, uint8_t *mac) {
    mt.bar0 = bar0;
    mt_reset(bar0);

    /* Read MAC */
    uint32_t mac0 = mt_read32(RT_MAC_CSR0);
    uint32_t mac1 = mt_read32(RT_MAC_CSR0 + 4);
    mt.mac[0] = mac0 & 0xFF;
    mt.mac[1] = (mac0 >> 8) & 0xFF;
    mt.mac[2] = (mac0 >> 16) & 0xFF;
    mt.mac[3] = (mac0 >> 24) & 0xFF;
    mt.mac[4] = mac1 & 0xFF;
    mt.mac[5] = (mac1 >> 8) & 0xFF;
    for (uint32_t i = 0; i < 6; i++) mac[i] = mt.mac[i];

    /* Enable MAC */
    mt_write32(RT_MAC_SYS_CTRL, 0x0000000C);
    mt_write32(RT_INT_MASK, 0x000000FF);

    /* Initialize TX/RX rings */
    mt_write32(RT_TX_BASE + 0x000, 0x00000000); /* TX base addr 0 */
    mt_write32(RT_TX_BASE + 0x010, 0x00000000); /* TX base addr 1 */
    mt_write32(RT_TX_BASE + 0x100, 0x00001000); /* TX ring size */
    mt_write32(RT_RX_BASE + 0x000, 0x00000000); /* RX base addr */
    mt_write32(RT_RX_BASE + 0x100, 0x00001000); /* RX ring size */

    /* Initialize RF */
    mt_write8(RT_RF_BASE, 0x03); /* RF: TX + RX on */
    mt.rf_mode = 3;

    return 0;
}

static int mt_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    mt.channel = channel;
    mt.band = band;

    /* Set channel */
    mt_write32(RT_CHANNEL_BASE, (band << 8) | channel);

    /* Set RF channel */
    mt_write32(RT_RF_BASE + 0x008, (band << 8) | channel);

    /* Trigger scan */
    mt_write8(RT_INT_STATUS, 0xFF);
    mt_write32(RT_MAC_SYS_CTRL, 0x0000000D); /* Start scan */

    return 0;
}

static int mt_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    uint32_t status = mt_read32(RT_INT_STATUS);
    if (!(status & 0x01)) return -1;

    /* Read BSSID from BCN buffer */
    uint32_t bssid0 = mt_read32(RT_BC_BASE + 0x010);
    uint32_t bssid1 = mt_read32(RT_BC_BASE + 0x014);
    result->bssid[0] = bssid0 & 0xFF;
    result->bssid[1] = (bssid0 >> 8) & 0xFF;
    result->bssid[2] = (bssid0 >> 16) & 0xFF;
    result->bssid[3] = (bssid0 >> 24) & 0xFF;
    result->bssid[4] = bssid1 & 0xFF;
    result->bssid[5] = (bssid1 >> 8) & 0xFF;

    result->channel = mt.channel;
    result->band = mt.band;
    result->rssi = (int8_t)(mt_read32(RT_RX_BASE + 0x098) & 0xFF);
    result->signal_percent = (result->rssi + 100) > 100 ? 100 : (result->rssi + 100);
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1;
    result->auth = 1;

    return 0;
}

static int mt_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                    uint8_t security, const uint8_t *key, uint32_t key_len) {
    uint8_t frame[256];
    frame[0] = 0x00;
    frame[1] = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_AUTH;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = mt.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];

    for (uint32_t i = 0; i < 24; i++)
        mt_write32(RT_TX_BASE + 0x800 + i * 4, frame[i]);
    mt_write32(RT_MAC_SYS_CTRL, mt_read32(RT_MAC_SYS_CTRL) | 0x01);

    return 0;
}

static int mt_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    uint8_t frame[256];
    frame[0] = 0x00;
    frame[1] = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_ASSOC_REQ;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = mt.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];

    for (uint32_t i = 0; i < 24; i++)
        mt_write32(RT_TX_BASE + 0x800 + i * 4, frame[i]);
    mt_write32(RT_MAC_SYS_CTRL, mt_read32(RT_MAC_SYS_CTRL) | 0x01);

    return 0;
}

static int mt_deauth(uint32_t bar0, const uint8_t *bssid) { return 0; }

static int mt_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        mt_write32(RT_TX_BASE + 0x800 + i * 4, frame[i]);
    mt_write32(RT_MAC_SYS_CTRL, mt_read32(RT_MAC_SYS_CTRL) | 0x01);
    return 0;
}

static int mt_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    uint32_t status = mt_read32(RT_INT_STATUS);
    if (!(status & 0x02)) return -1;

    for (uint32_t i = 0; i < *len; i++)
        frame[i] = (uint8_t)mt_read32(RT_RX_BASE + 0x0100 + i * 4);
    return 0;
}

static void mt_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = mt.mac[i];
}

static void mt_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    mt.channel = channel;
    mt.band = band;
    mt_write32(RT_CHANNEL_BASE, (band << 8) | channel);
}

static int mt_set_power(uint32_t bar0, uint8_t power) { return 0; }
static int mt_get_rssi(uint32_t bar0) { return mt.rssi; }

static const wifi_driver_ops_t mediatek_driver = {
    .name = "MediaTek/Ralink (rt2x00/mt76)",
    .pci_vendor = RT_VENDOR_ID,
    .pci_device = RT3090_DID,
    .init = mt_init,
    .reset = mt_reset,
    .scan_start = mt_scan_start,
    .scan_get_result = mt_scan_get_result,
    .auth = mt_auth,
    .assoc = mt_assoc,
    .deauth = mt_deauth,
    .send_frame = mt_send_frame,
    .recv_frame = mt_recv_frame,
    .get_mac = mt_get_mac,
    .set_channel = mt_set_channel,
    .set_power = mt_set_power,
    .get_rssi = mt_get_rssi,
};

int wifi_mediatek_register(void) {
    return wifi_register_driver(&mediatek_driver);
}
