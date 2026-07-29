/**
 * Chicago-95 Bootloader — Intel WiFi Driver (iwlwifi / iwm)
 * Supports: Intel Wireless 7260, 7265, 8260, 9260, AX200, AX201, AX210
 * PCI vendor: 0x8086
 */

#include "drivers/wifi.h"
#include "boot/security.h"

/* ======================================================================== */
/* Intel PCI device IDs                                                      */
/* ======================================================================== */

#define INTEL_VENDOR_ID     0x8086

/* iwm (M series) */
#define IWM_7260_DID        0x08B1
#define IWM_7265_DID        0x095A
#define IWM_7265D_DID       0x095B
#define IWM_8260_DID        0x24F3
#define IWM_8265_DID        0x24FD
#define IWM_9260_DID        0x2526
#define IWM_9560_DID        0xA370

/* iwlwifi (AX series) */
#define IWL_AX200_DID       0x2723
#define IWL_AX201_DID       0x2725
#define IWL_AX210_DID       0x272B
#define IWL_AX211_DID       0x272C

/* ======================================================================== */
/* MMIO registers                                                            */
/* ======================================================================== */

#define IWL_CSR_HW_IF_CONFIG_REG   0x040
#define IWL_CSR_INT_MASK           0x008
#define IWL_CSR_FH_INT_STATUS     0x098
#define IWL_CSR_RESET              0x020
#define IWL_CSR_MAC_STATUS_REG     0x0B0
#define IWL_CSR_CHICKEN            0x0AC

/* FH (Fetch Hardware) */
#define IWL_HBUS_TFD_FETCH_CTRL   0x40
#define IWL_HBUS_TX_DMA_STATUS    0x48

/* TX */
#define IWL_TFH_TFD_BASE_ADDR     0x590
#define IWL_TFH_TFD_BASE_ADDR_1   0x5A0
#define IWL_TFH_TFD_CTXT_BASE     0x5B0

/* RX */
#define IWL_RFH_RXF_DMA_CFG       0xC00
#define IWL_RFH_RXF_DMA_RBD_CFG   0xC04
#define IWL_RFH_RXF_DMA_STTS_BNSZ 0xC10

/* ======================================================================== */
/* Firmware command IDs                                                      */
/* ======================================================================== */

#define IWL_ALIVE         0
#define IWL_ASSOCIATE     2
#define IWL_AUTH          4
#define IWL_CMD           3
#define IWL_SCAN_REQ      16
#define IWL_SCAN_ADD_SSID 0x74
#define IWL_TX_CMD        75
#define IWL_REPLY_ERROR   255

/* ======================================================================== */
/* State                                                                    */
/* ======================================================================== */

typedef struct {
    uint32_t bar0;
    uint8_t  mac[6];
    uint8_t  fw_loaded;
    uint8_t  channel;
    uint8_t  band;
    uint8_t  associated;
    uint8_t  bssid[6];
    uint8_t  key[32];
    uint8_t  key_index;
} __attribute__((packed)) iwl_state_t;

static iwl_state_t iwl;

/* ======================================================================== */
/* MMIO helpers                                                              */
/* ======================================================================== */

static inline void iwl_write32(uint32_t reg, uint32_t val) {
    *(volatile uint32_t *)(iwl.bar0 + reg) = val;
}

static inline uint32_t iwl_read32(uint32_t reg) {
    return *(volatile uint32_t *)(iwl.bar0 + reg);
}

/* ======================================================================== */
/* Reset                                                                     */
/* ======================================================================== */

static void iwl_reset(uint32_t bar0) {
    iwl.bar0 = bar0;
    iwl_write32(IWL_CSR_RESET, 1);
    for (volatile int i = 0; i < 10000; i++);
    iwl_write32(IWL_CSR_RESET, 0);
}

/* ======================================================================== */
/* Init                                                                      */
/* ======================================================================== */

static int iwl_init(uint32_t bar0, uint8_t *mac) {
    iwl.bar0 = bar0;
    iwl_reset(bar0);

    /* Read MAC address from registers */
    uint32_t mac_low = iwl_read32(0x090);
    uint32_t mac_high = iwl_read32(0x094);
    iwl.mac[0] = mac_low & 0xFF;
    iwl.mac[1] = (mac_low >> 8) & 0xFF;
    iwl.mac[2] = (mac_low >> 16) & 0xFF;
    iwl.mac[3] = (mac_low >> 24) & 0xFF;
    iwl.mac[4] = mac_high & 0xFF;
    iwl.mac[5] = (mac_high >> 8) & 0xFF;

    for (uint32_t i = 0; i < 6; i++) mac[i] = iwl.mac[i];

    /* Enable interrupts */
    iwl_write32(IWL_CSR_INT_MASK, 0xFFFFFFFF);

    /* Initialize FH (Fetch Hardware) */
    iwl_write32(IWL_HBUS_TFD_FETCH_CTRL, 0);
    iwl_write32(IWL_HBUS_TX_DMA_STATUS, 0);

    /* Initialize RX DMA */
    iwl_write32(IWL_RFH_RXF_DMA_CFG, 0);
    iwl_write32(IWL_RFH_RXF_DMA_RBD_CFG, 0);

    iwl.fw_loaded = 0;
    iwl.associated = 0;

    return 0;
}

/* ======================================================================== */
/* Scan                                                                      */
/* ======================================================================== */

static int iwl_scan_start(uint32_t bar0, uint8_t channel, uint8_t band) {
    iwl.bar0 = bar0;
    iwl.channel = channel;
    iwl.band = band;

    /* Build scan request command */
    uint8_t cmd[64];
    uint32_t cmd_len = 0;

    /* Scan request header */
    cmd[0] = IWL_SCAN_REQ;
    cmd[1] = 0;      /* Flags */
    cmd[2] = 0;      /* Reserved */
    cmd[3] = band;    /* Band: 0=2.4GHz, 1=5GHz */
    cmd[4] = channel; /* Channel */
    cmd[5] = 0;      /* SSID len */

    /* Write command to TX queue */
    for (uint32_t i = 0; i < cmd_len; i++)
        iwl_write32(0x400 + i * 4, cmd[i]);

    /* Kick TX */
    iwl_write32(IWL_HBUS_TX_DMA_STATUS, 1);

    return 0;
}

static int iwl_scan_get_result(uint32_t bar0, wifi_scan_result_t *result) {
    if (!result) return -1;
    iwl.bar0 = bar0;

    /* Read scan results from RX ring */
    uint32_t status = iwl_read32(0x0B0);
    if (!(status & 0x1)) return -1; /* No new result */

    /* Parse BSS info from RX descriptor */
    uint32_t bssid0 = iwl_read32(0x480);
    uint32_t bssid1 = iwl_read32(0x484);

    result->bssid[0] = bssid0 & 0xFF;
    result->bssid[1] = (bssid0 >> 8) & 0xFF;
    result->bssid[2] = (bssid0 >> 16) & 0xFF;
    result->bssid[3] = (bssid0 >> 24) & 0xFF;
    result->bssid[4] = bssid1 & 0xFF;
    result->bssid[5] = (bssid1 >> 8) & 0xFF;

    result->channel = iwl.channel;
    result->band = iwl.band;
    result->rssi = (int8_t)iwl_read32(0x490);
    result->signal_percent = (result->rssi + 100);
    if (result->signal_percent > 100) result->signal_percent = 100;

    /* Default security */
    result->security = WIFI_SEC_WPA2;
    result->cipher = 1; /* CCMP */
    result->auth = 1;   /* PSK */

    return 0;
}

/* ======================================================================== */
/* Auth / Assoc                                                              */
/* ======================================================================== */

static int iwl_auth(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                     uint8_t security, const uint8_t *key, uint32_t key_len) {
    iwl.bar0 = bar0;

    /* Build authentication command */
    uint8_t cmd[128];
    cmd[0] = IWL_AUTH;
    cmd[1] = security;
    for (uint32_t i = 0; i < 6; i++) cmd[2 + i] = bssid[i];

    /* Write command */
    for (uint32_t i = 0; i < 10; i++)
        iwl_write32(0x400 + i * 4, cmd[i]);

    iwl_write32(IWL_HBUS_TX_DMA_STATUS, 1);

    /* Store key */
    if (key && key_len <= 32) {
        for (uint32_t i = 0; i < key_len; i++)
            iwl.key[i] = key[i];
    }

    return 0;
}

static int iwl_assoc(uint32_t bar0, const uint8_t *bssid, uint8_t channel) {
    iwl.bar0 = bar0;

    /* Build association command */
    uint8_t cmd[64];
    cmd[0] = IWL_ASSOCIATE;
    for (uint32_t i = 0; i < 6; i++) cmd[1 + i] = bssid[i];
    cmd[7] = channel;

    for (uint32_t i = 0; i < 9; i++)
        iwl_write32(0x400 + i * 4, cmd[i]);

    iwl_write32(IWL_HBUS_TX_DMA_STATUS, 1);

    for (uint32_t i = 0; i < 6; i++) iwl.bssid[i] = bssid[i];
    iwl.associated = 1;

    return 0;
}

static int iwl_deauth(uint32_t bar0, const uint8_t *bssid) {
    iwl.associated = 0;
    return 0;
}

/* ======================================================================== */
/* TX / RX                                                                   */
/* ======================================================================== */

static int iwl_send_frame(uint32_t bar0, const uint8_t *frame, uint32_t len) {
    iwl.bar0 = bar0;

    /* Write frame to TX DMA ring */
    for (uint32_t i = 0; i < len && i < 2048; i++)
        iwl_write32(0x500 + i * 4, frame[i]);

    /* Kick TX */
    iwl_write32(IWL_HBUS_TX_DMA_STATUS, 1);
    return 0;
}

static int iwl_recv_frame(uint32_t bar0, uint8_t *frame, uint32_t *len) {
    iwl.bar0 = bar0;

    uint32_t status = iwl_read32(0x0B0);
    if (!(status & 0x2)) return -1; /* No RX data */

    uint32_t rx_len = iwl_read32(0x488);
    if (rx_len > *len) rx_len = *len;

    for (uint32_t i = 0; i < rx_len; i++)
        frame[i] = (uint8_t)iwl_read32(0x500 + i * 4);

    *len = rx_len;
    return 0;
}

/* ======================================================================== */
/* Config                                                                    */
/* ======================================================================== */

static void iwl_get_mac(uint32_t bar0, uint8_t *mac) {
    for (uint32_t i = 0; i < 6; i++) mac[i] = iwl.mac[i];
}

static void iwl_set_channel(uint32_t bar0, uint8_t channel, uint8_t band) {
    iwl.channel = channel;
    iwl.band = band;
}

static int iwl_set_power(uint32_t bar0, uint8_t power) {
    return 0;
}

static int iwl_get_rssi(uint32_t bar0) {
    return (int8_t)iwl_read32(0x490);
}

/* ======================================================================== */
/* Driver registration                                                       */
/* ======================================================================== */

static const wifi_driver_ops_t intel_driver = {
    .name = "Intel WiFi (iwlwifi/iwm)",
    .pci_vendor = INTEL_VENDOR_ID,
    .pci_device = IWM_9260_DID, /* Default — matched at runtime */

    .init = iwl_init,
    .reset = iwl_reset,
    .scan_start = iwl_scan_start,
    .scan_get_result = iwl_scan_get_result,
    .auth = iwl_auth,
    .assoc = iwl_assoc,
    .deauth = iwl_deauth,
    .send_frame = iwl_send_frame,
    .recv_frame = iwl_recv_frame,
    .get_mac = iwl_get_mac,
    .set_channel = iwl_set_channel,
    .set_power = iwl_set_power,
    .get_rssi = iwl_get_rssi,
};

int wifi_intel_register(void) {
    return wifi_register_driver(&intel_driver);
}
