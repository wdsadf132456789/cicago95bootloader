/**
 * Chicago-95 Bootloader — WiFi Unified API
 * Hardware-agnostic WiFi interface: scan, associate, auth, TX/RX frames.
 * PCI bus scan detects chipset, dispatches to correct driver.
 */

#ifndef BOOT_WIFI_H
#define BOOT_WIFI_H

#include <stdint.h>

/* ======================================================================== */
/* WiFi frame types                                                          */
/* ======================================================================== */

#define WIFI_FRAME_MGMT    0
#define WIFI_FRAME_DATA    2
#define WIFI_FRAME_CTL     1

/* Management subtypes */
#define WIFI_MGMT_ASSOC_REQ    0x00
#define WIFI_MGMT_ASSOC_RESP   0x01
#define WIFI_MGMT_REASSOC_REQ  0x02
#define WIFI_MGMT_REASSOC_RESP 0x03
#define WIFI_MGMT_PROBE_REQ    0x04
#define WIFI_MGMT_PROBE_RESP   0x05
#define WIFI_MGMT_BEACON       0x08
#define WIFI_MGMT_ATIM         0x09
#define WIFI_MGMT_DISASSOC     0x0A
#define WIFI_MGMT_AUTH         0x0B
#define WIFI_MGMT_DEAUTH       0x0C
#define WIFI_MGMT_ACTION       0x0D

/* Control subtypes */
#define WIFI_CTL_BLOCK_ACK_REQ 0x08
#define WIFI_CTL_BLOCK_ACK     0x09
#define WIFI_CTL_RTS           0x0B
#define WIFI_CTL_CTS           0x0C
#define WIFI_CTL_ACK           0x0D

/* ======================================================================== */
/* WiFi bands                                                                */
/* ======================================================================== */

#define WIFI_BAND_2GHZ     (1 << 0)
#define WIFI_BAND_5GHZ     (1 << 1)
#define WIFI_BAND_6GHZ     (1 << 2)

/* ======================================================================== */
/* WiFi channel numbers (2.4 GHz)                                            */
/* ======================================================================== */

#define WIFI_CH_1    1
#define WIFI_CH_2    2
#define WIFI_CH_3    3
#define WIFI_CH_4    4
#define WIFI_CH_5    5
#define WIFI_CH_6    6
#define WIFI_CH_7    7
#define WIFI_CH_8    8
#define WIFI_CH_9    9
#define WIFI_CH_10   10
#define WIFI_CH_11   11
#define WIFI_CH_12   12
#define WIFI_CH_13   13
#define WIFI_CH_14   14

/* 5 GHz channels */
#define WIFI_CH_36   36
#define WIFI_CH_40   40
#define WIFI_CH_44   44
#define WIFI_CH_48   48
#define WIFI_CH_52   52
#define WIFI_CH_56   56
#define WIFI_CH_60   60
#define WIFI_CH_64   64
#define WIFI_CH_100  100
#define WIFI_CH_104  104
#define WIFI_CH_108  108
#define WIFI_CH_112  112
#define WIFI_CH_116  116
#define WIFI_CH_120  120
#define WIFI_CH_124  124
#define WIFI_CH_128  128
#define WIFI_CH_132  132
#define WIFI_CH_136  136
#define WIFI_CH_140  140
#define WIFI_CH_144  144
#define WIFI_CH_149  149
#define WIFI_CH_153  153
#define WIFI_CH_157  157
#define WIFI_CH_161  161
#define WIFI_CH_165  165

/* ======================================================================== */
/* WiFi security types                                                       */
/* ======================================================================== */

#define WIFI_SEC_NONE     0
#define WIFI_SEC_WEP      1
#define WIFI_SEC_WPA      2
#define WIFI_SEC_WPA2     3
#define WIFI_SEC_WPA3     4

/* ======================================================================== */
/* WiFi scan result                                                          */
/* ======================================================================== */

#define WIFI_SSID_MAX_LEN 32

typedef struct {
    uint8_t  bssid[6];
    uint8_t  ssid[WIFI_SSID_MAX_LEN];
    uint8_t  ssid_len;
    uint8_t  channel;
    uint8_t  band;
    uint8_t  security;
    int8_t   rssi;
    uint8_t  signal_percent;
    uint8_t  is_hidden;
    uint8_t  wpa_version;
    uint8_t  cipher;       /* CCMP=1, TKIP=2, GCMP=4 */
    uint8_t  auth;         /* PSK=1, SAE=2, ENTERPRISE=3 */
    uint16_t beacon_interval;
} __attribute__((packed)) wifi_scan_result_t;

/* ======================================================================== */
/* WiFi driver operations                                                    */
/* ======================================================================== */

typedef struct {
    const char *name;
    uint32_t    pci_vendor;
    uint32_t    pci_device;

    int  (*init)(uint32_t bar0, uint8_t *mac);
    void (*reset)(uint32_t bar0);
    int  (*scan_start)(uint32_t bar0, uint8_t channel, uint8_t band);
    int  (*scan_get_result)(uint32_t bar0, wifi_scan_result_t *result);
    int  (*auth)(uint32_t bar0, const uint8_t *bssid, const uint8_t *ssid,
                 uint8_t security, const uint8_t *key, uint32_t key_len);
    int  (*assoc)(uint32_t bar0, const uint8_t *bssid, uint8_t channel);
    int  (*deauth)(uint32_t bar0, const uint8_t *bssid);
    int  (*send_frame)(uint32_t bar0, const uint8_t *frame, uint32_t len);
    int  (*recv_frame)(uint32_t bar0, uint8_t *frame, uint32_t *len);
    void (*get_mac)(uint32_t bar0, uint8_t *mac);
    void (*set_channel)(uint32_t bar0, uint8_t channel, uint8_t band);
    int  (*set_power)(uint32_t bar0, uint8_t power);
    int  (*get_rssi)(uint32_t bar0);
} __attribute__((packed)) wifi_driver_ops_t;

/* ======================================================================== */
/* WiFi interface state                                                      */
/* ======================================================================== */

#define WIFI_STATE_DISCONNECTED  0
#define WIFI_STATE_SCANNING      1
#define WIFI_STATE_AUTHENTICATING 2
#define WIFI_STATE_ASSOCIATING   3
#define WIFI_STATE_CONNECTED     4
#define WIFI_STATE_ERROR         5

#define WIFI_MAX_SCAN_RESULTS 64

typedef struct {
    uint32_t         bar0;
    uint8_t          mac[6];
    uint8_t          state;
    uint8_t          channel;
    uint8_t          band;
    uint8_t          security;
    uint8_t          connected_bssid[6];
    uint8_t          connected_ssid[WIFI_SSID_MAX_LEN];
    int8_t           rssi;
    const wifi_driver_ops_t *driver;
    wifi_scan_result_t scan_results[WIFI_MAX_SCAN_RESULTS];
    uint32_t         scan_count;
} __attribute__((packed)) wifi_interface_t;

/* ======================================================================== */
/* Unified API                                                               */
/* ======================================================================== */

/* Init / driver registration */
int  wifi_init(void);
int  wifi_register_driver(const wifi_driver_ops_t *ops);
int  wifi_select_driver(uint32_t pci_vendor, uint32_t pci_device);
wifi_interface_t *wifi_get_interface(void);
const wifi_driver_ops_t *wifi_get_driver(void);

/* Scan */
int  wifi_scan_start(uint8_t channel, uint8_t band);
int  wifi_scan_get(wifi_scan_result_t *result);
uint32_t wifi_scan_count(void);

/* Auth / Assoc */
int  wifi_auth(const uint8_t *bssid, const uint8_t *ssid,
               uint8_t security, const uint8_t *key, uint32_t key_len);
int  wifi_assoc(const uint8_t *bssid, uint8_t channel);
int  wifi_deauth(const uint8_t *bssid);

/* TX / RX */
int  wifi_send(const uint8_t *frame, uint32_t len);
int  wifi_recv(uint8_t *frame, uint32_t *len);

/* Config */
void wifi_set_channel(uint8_t channel, uint8_t band);
int  wifi_set_power(uint8_t power);
int  wifi_get_rssi(void);
void wifi_get_mac(uint8_t mac[6]);
uint8_t wifi_get_state(void);

/* PCI bus scan */
int  wifi_pci_scan(uint32_t *vendor, uint32_t *device, uint32_t *bar0);

/* Individual driver registration */
int  wifi_intel_register(void);
int  wifi_atheros_register(void);
int  wifi_broadcom_register(void);
int  wifi_realtek_register(void);
int  wifi_mediatek_register(void);
int  wifi_prism5_register(void);
int  wifi_marvell_register(void);

#endif /* BOOT_WIFI_H */
