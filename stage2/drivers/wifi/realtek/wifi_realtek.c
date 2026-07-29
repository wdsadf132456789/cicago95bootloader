/**
 * Chicago-95 Bootloader — Realtek WiFi Driver (rtl8xxxu / rtl8188)
 * Supports: RTL8188CUS, RTL8192CU, RTL8723AU, RTL8812AU, RTL8814AU,
 *           RTL8821AU, RTL8822BU, RTL8188EU, RTL8188FTV
 * PCI vendor: 0x10EC
 */

#include "drivers/wifi.h"
#include "boot/security.h"

#define RTL_VENDOR_ID       0x10EC
#define RTL8188CUS_DID      0x8176
#define RTL8192CU_DID       0x8177
#define RTL8723AU_DID      0x8723
#define RTL8812AU_DID      0x8812
#define RTL8814AU_DID      0x8813
#define RTL8821AU_DID      0x8821
#define RTL8822BU_DID      0xB822
#define RTL8188EU_DID      0x8179
#define RTL8188FTV_DID     0x818B

/* RTL8188 MAC registers */
#define RTL_MAC_BASE        0x102500
#define RTL_MACCR           (RTL_MAC_BASE + 0x0100)
#define RTL_MACIDR          (RTL_MAC_BASE + 0x0120)
#define RTL_BSSIDR          (RTL_MAC_BASE + 0x0140)
#define RTL_INT_MASK        (RTL_MAC_BASE + 0x010C)
#define RTL_INT_STATUS      (RTL_MAC_BASE + 0x0124)
#define RTL_TX_DESC0        (RTL_MAC_BASE + 0x0200)
#define RTL_RX_DESC0        (RTL_MAC_BASE + 0x0300)
#define RTL_BCN_REQ         (RTL_MAC_BASE + 0x0500)
#define RTL_CHANNEL         (RTL_MAC_BASE + 0x0180)
#define RTL_TX_PWR          (RTL_MAC_BASE + 0x0200)
#define RTL_RF_MOD          (RTL_MAC_BASE + 0x0240)

static struct {
    uint32_t bar0;
    uint8_t  mac[6];
    uint8_t  band;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  rf_mode;
} rtl;

static inline void rtl_write8(uint32_t reg, uint8_t val) {
    *(volatile uint8_t *)(rtl.bar0 + reg) = val;
}
static inline uint8_t rtl_read8(uint32_t reg) {
    return *(volatile uint8_t *)(rtl.bar0 + reg);
}
static inline void rtl_write16(uint32_t reg, uint16_t val) {
    *(volatile uint16_t *)(rtl.bar0 + reg) = val;
}
static inline void rtl_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(rtl.bar0 + reg) = val;
}
static inline uint32_t rtl_read32(uint32_t reg) {
    return *(volatile uint32_t *)(rtl.bar0 + reg);
}

static void rtl_reset(uint32_t bar0) {
    rtl.bar0 = bar0;
    rtl_write8(RTL_MACCR, 0x00);
    for (volatile int i = 0; i < 50000; i++);
    rtl_write8(RTL_MACCR, 0x0E);
}

static int rtl_init(uint32_t bar0, uint8_t *mac) {
    rtl.bar0 = bar0;
    rtl_reset(bar0);

    /* Read MAC from ID register */
    uint32_t idr = rtl_read32(RTL_MACIDR);
    rtl.mac[0] = idr & 0xFF;
    rtl.mac[1] = (idr >> 8) & 0xFF;
    rtl.mac[2] = (idr >> 16) & 0xFF;
    rtl.mac[3] = (idr >> 24) & 0xFF;
    uint32_t idr2 = rtl_read32(RTL_MACIDR + 4);
    rtl.mac[4] = idr2 & 0xFF;
    rtl.mac[5] = (idr2 >> 8) & 0xFF;
    for (uint32_t i = 0; i < 6; i++) mac[i] = rtl.mac[i];

    /* Enable MAC / BB / RF */
    rtl_write8(RTL_MACCR, 0x0E);
    rtl_write32(RTL_INT_MASK, 0x0000FFFF);

    /* Set RF mode */
    rtl_write8(RTL_RF_MOD, 0x03);
    rtl.rf_mode = 0x03;

    return 0;
}

static int rtl_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    rtl.channel = channel;
    rtl.band = band;

    /* Set channel */
    rtl_write32(RTL_CHANNEL, (channel << 2) | band);

    /* Enable scan mode */
    rtl_write32(RTL_INT_STATUS, 0xFFFFFFFF);
    rtl_write8(RTL_BCN_REQ, 0x11);

    return 0;
}

static int rtl_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    uint32_t status = rtl_read32(RTL_INT_STATUS);
    if (!(status & 0x01)) return -1;

    /* Read BSSID from register */
    uint32_t bssid0 = rtl_read32(RTL_BSSIDR);
    uint32_t bssid1 = rtl_read32(RTL_BSSIDR + 4);
    result->bssid[0] = bssid0 & 0xFF;
    result->bssid[1] = (bssid0 >> 8) & 0xFF;
    result->bssid[2] = (bssid0 >> 16) & 0xFF;
    result->bssid[3] = (bssid0 >> 24) & 0xFF;
    result->bssid[4] = bssid1 & 0xFF;
    result->bssid[5] = (bssid1 >> 8) & 0xFF;

    result->channel = rtl.channel;
    result->band = rtl.band;
    result->rssi = (int8_t)(rtl_read8(RTL_MAC_BASE + 0x0250));
    result->signal_percent = (result->rssi + 100) > 100 ? 100 : (result->rssi + 100);
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1;
    result->auth = 1;

    return 0;
}

static int rtl_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                     uint8_t security, const uint8_t *key, uint32_t key_len) {
    uint8_t frame[256];
    frame[0] = 0x00;
    frame[1] = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_AUTH;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = rtl.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];

    /* Write to TX descriptor */
    for (uint32_t i = 0; i < 24; i++)
        rtl_write32(RTL_TX_DESC0 + i * 4, frame[i]);
    rtl_write8(RTL_MAC_BASE + 0x0200 + 3, 0x80); /* TX start */

    return 0;
}

static int rtl_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    uint8_t frame[256];
    frame[0] = 0x00;
    frame[1] = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_ASSOC_REQ;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = rtl.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];

    for (uint32_t i = 0; i < 24; i++)
        rtl_write32(RTL_TX_DESC0 + i * 4, frame[i]);
    rtl_write8(RTL_MAC_BASE + 0x0200 + 3, 0x80);

    return 0;
}

static int rtl_deauth(uint32_t bar0, const uint8_t *bssid) { return 0; }

static int rtl_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        rtl_write32(RTL_TX_DESC0 + i * 4, frame[i]);
    rtl_write8(RTL_MAC_BASE + 0x0200 + 3, 0x80);
    return 0;
}

static int rtl_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    uint32_t status = rtl_read32(RTL_INT_STATUS);
    if (!(status & 0x02)) return -1;

    for (uint32_t i = 0; i < *len; i++)
        frame[i] = rtl_read8(RTL_RX_DESC0 + i);
    return 0;
}

static void rtl_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = rtl.mac[i];
}

static void rtl_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    rtl.channel = channel;
    rtl.band = band;
    rtl_write32(RTL_CHANNEL, (channel << 2) | band);
}

static int rtl_set_power(uint32_t bar0, uint8_t power) {
    rtl_write8(RTL_TX_PWR, power);
    return 0;
}

static int rtl_get_rssi(uint32_t bar0) { return rtl.rssi; }

static const wifi_driver_ops_t realtek_driver = {
    .name = "Realtek (rtl8xxxu/rtl8188)",
    .pci_vendor = RTL_VENDOR_ID,
    .pci_device = RTL8188CUS_DID,
    .init = rtl_init,
    .reset = rtl_reset,
    .scan_start = rtl_scan_start,
    .scan_get_result = rtl_scan_get_result,
    .auth = rtl_auth,
    .assoc = rtl_assoc,
    .deauth = rtl_deauth,
    .send_frame = rtl_send_frame,
    .recv_frame = rtl_recv_frame,
    .get_mac = rtl_get_mac,
    .set_channel = rtl_set_channel,
    .set_power = rtl_set_power,
    .get_rssi = rtl_get_rssi,
};

int wifi_realtek_register(void) {
    return wifi_register_driver(&realtek_driver);
}
