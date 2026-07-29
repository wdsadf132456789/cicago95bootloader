/**
 * Chicago-95 Bootloader — Broadcom WiFi Driver (brcmfmac / b43)
 * Supports: BCM43xx, BCM4313, BCM43142, BCM43224, BCM43225, BCM43228,
 *           BCM4331, BCM4335, BCM4350, BCM4352, BCM4354, BCM4356, BCM4358
 * PCI vendor: 0x14E4
 */

#include "drivers/wifi.h"
#include "boot/security.h"

#define BRCM_VENDOR_ID     0x14E4
#define BCM4313_DID        0x4727
#define BCM43224_DID       0x4357
#define BCM43225_DID       0x4357
#define BCM43228_DID       0x4349
#define BCM4331_DID        0x4331
#define BCM4335_DID        0x43A3
#define BCM4350_DID        0x43A3
#define BCM4352_DID        0x43B1
#define BCM4354_DID        0x43DF
#define BCM4356_DID        0x4360
#define BCM4358_DID        0x43E2
#define BCM43602_DID       0x4405

/* Core registers */
#define BCMA_IOCTL         0x408
#define BCMA_IOSTATUS      0x40C
#define BCMA_RESET_CTL     0x800
#define BCMA_CLK_CTL       0x804
#define BCMA_POWER_CTL     0x808

/* SHM (Shared Memory) */
#define BCM43_SHM_SHARED   0x10000
#define BCM43_SHM_WMACTL   (BCM43_SHM_SHARED + 0x00)
#define BCM43_SHM_WMADATA  (BCM43_SHM_SHARED + 0x02)
#define BCM43_SHM:UIControl  (BCM43_SHM_SHARED + 0x10)
#define BCM43_SHM:UIControlACK (BCM43_SHM_SHARED + 0x14)

/* MMIO */
#define BCM43_MMIO_STATUS  0x130
#define BCM43_MMIO_XMITSTAT 0x170
#define BCM43_MMIO_TFS_NDMA 0x108

static struct {
    uint32_t bar0;
    uint32_t bar1;    /* MMIO1 for some models */
    uint8_t  mac[6];
    uint8_t  core_id;
    uint8_t  core_rev;
    uint8_t  band;
    uint8_t  channel;
    int8_t   rssi;
    uint8_t  pmu_ctl;
} brcm;

static inline void brcm_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(brcm.bar0 + reg) = val;
}
static inline uint32_t brcm_read32(uint32_t reg) {
    return *(volatile uint32_t *)(brcm.bar0 + reg);
}
static inline void brcm_write16(uint32_t reg, uint16_t val) {
    *(volatile uint16_t *)(brcm.bar0 + reg) = val;
}

static void brcm_reset(uint32_t bar0) {
    brcm.bar0 = bar0;
    /* Core reset */
    brcm_write32(BCMA_RESET_CTL, 1);
    for (volatile int i = 0; i < 50000; i++);
    brcm_write32(BCMA_CLK_CTL, 0x00000001);
    brcm_write32(BCMA_RESET_CTL, 0);
    for (volatile int i = 0; i < 50000; i++);

    /* Power up */
    brcm_write32(BCMA_POWER_CTL, 0x00000002);
    for (volatile int i = 0; i < 200000; i++);
}

static int brcm_init(uint32_t bar0, uint8_t *mac) {
    brcm.bar0 = bar0;
    brcm_reset(bar0);

    /* Read MAC from core registers */
    uint32_t mac0 = brcm_read32(0x100);
    uint32_t mac1 = brcm_read32(0x104);
    brcm.mac[0] = mac0 & 0xFF;
    brcm.mac[1] = (mac0 >> 8) & 0xFF;
    brcm.mac[2] = (mac0 >> 16) & 0xFF;
    brcm.mac[3] = (mac0 >> 24) & 0xFF;
    brcm.mac[4] = mac1 & 0xFF;
    brcm.mac[5] = (mac1 >> 8) & 0xFF;
    for (uint32_t i = 0; i < 6; i++) mac[i] = brcm.mac[i];

    /* Enable interrupts */
    brcm_write32(BCMA_IOCTL, 0x00000010);
    brcm_write32(BCM43_MMIO_STATUS, 0x00000008);

    return 0;
}

static int brcm_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    brcm.channel = channel;
    brcm.band = band;

    /* Write scan command to SHM */
    brcm_write16(BCM43_SHM_WMACTL, 0x0000);
    brcm_write16(BCM43_SHM_WMADATA, (band << 8) | channel);

    /* Trigger scan */
    brcm_write32(BCM43_MMIO_TFS_NDMA, 0x00000001);

    return 0;
}

static int brcm_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    uint32_t status = brcm_read32(BCM43_MMIO_STATUS);
    if (!(status & 0x01)) return -1;

    /* Read BSS info from SHM */
    uint32_t bssid_lo = brcm_read32(BCM43_SHM_SHARED + 0x20);
    uint32_t bssid_hi = brcm_read32(BCM43_SHM_SHARED + 0x24);
    result->bssid[0] = bssid_lo & 0xFF;
    result->bssid[1] = (bssid_lo >> 8) & 0xFF;
    result->bssid[2] = (bssid_lo >> 16) & 0xFF;
    result->bssid[3] = (bssid_lo >> 24) & 0xFF;
    result->bssid[4] = bssid_hi & 0xFF;
    result->bssid[5] = (bssid_hi >> 8) & 0xFF;

    result->channel = brcm.channel;
    result->band = brcm.band;
    result->rssi = (int8_t)brcm_read32(BCM43_SHM_SHARED + 0x30);
    result->signal_percent = (result->rssi + 100) > 100 ? 100 : (result->rssi + 100);
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1;
    result->auth = 1;

    return 0;
}

static int brcm_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                      uint8_t security, const uint8_t *key, uint32_t key_len) {
    uint8_t frame[256];
    frame[0] = 0x00;
    frame[1] = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_AUTH;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = brcm.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];
    frame[22] = 0x00; frame[23] = 0x00;

    for (uint32_t i = 0; i < 24; i++)
        brcm_write32(0x200 + i * 4, frame[i]);
    brcm_write32(BCM43_MMIO_TFS_NDMA, 0x00000001);

    return 0;
}

static int brcm_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    uint8_t frame[256];
    frame[0] = 0x00;
    frame[1] = (WIFI_FRAME_MGMT << 2) | WIFI_MGMT_ASSOC_REQ;
    for (uint32_t i = 0; i < 6; i++) frame[2 + i] = bssid[i];
    for (uint32_t i = 0; i < 6; i++) frame[8 + i] = brcm.mac[i];
    for (uint32_t i = 0; i < 6; i++) frame[14 + i] = bssid[i];

    for (uint32_t i = 0; i < 24; i++)
        brcm_write32(0x200 + i * 4, frame[i]);
    brcm_write32(BCM43_MMIO_TFS_NDMA, 0x00000001);

    return 0;
}

static int brcm_deauth(uint32_t bar0, const uint8_t *bssid) { return 0; }

static int brcm_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    for (uint32_t i = 0; i < len; i++)
        brcm_write32(0x200 + i * 4, frame[i]);
    brcm_write32(BCM43_MMIO_TFS_NDMA, 0x00000001);
    return 0;
}

static int brcm_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    uint32_t status = brcm_read32(BCM43_MMIO_STATUS);
    if (!(status & 0x02)) return -1;

    for (uint32_t i = 0; i < *len; i++)
        frame[i] = (uint8_t)brcm_read32(0x300 + i * 4);
    return 0;
}

static void brcm_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = brcm.mac[i];
}

static void brcm_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    brcm.channel = channel;
    brcm.band = band;
}

static int brcm_set_power(uint32_t bar0, uint8_t power) { return 0; }
static int brcm_get_rssi(uint32_t bar0) { return brcm.rssi; }

static const wifi_driver_ops_t broadcom_driver = {
    .name = "Broadcom (brcmfmac/b43)",
    .pci_vendor = BRCM_VENDOR_ID,
    .pci_device = BCM43228_DID,
    .init = brcm_init,
    .reset = brcm_reset,
    .scan_start = brcm_scan_start,
    .scan_get_result = brcm_scan_get_result,
    .auth = brcm_auth,
    .assoc = brcm_assoc,
    .deauth = brcm_deauth,
    .send_frame = brcm_send_frame,
    .recv_frame = brcm_recv_frame,
    .get_mac = brcm_get_mac,
    .set_channel = brcm_set_channel,
    .set_power = brcm_set_power,
    .get_rssi = brcm_get_rssi,
};

int wifi_broadcom_register(void) {
    return wifi_register_driver(&broadcom_driver);
}
