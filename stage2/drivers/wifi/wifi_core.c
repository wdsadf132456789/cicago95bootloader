/**
 * Chicago-95 Bootloader — WiFi Unified Core
 * Driver registry, PCI bus scan, frame TX/RX dispatch, scan/assoc state machine.
 */

#include "drivers/wifi.h"
#include "boot/security.h"

/* ======================================================================== */
/* State                                                                    */
/* ======================================================================== */

#define WIFI_MAX_DRIVERS 16

static const wifi_driver_ops_t *wifi_drivers[WIFI_MAX_DRIVERS];
static uint32_t wifi_driver_count = 0;
static wifi_interface_t wifi_iface;

/* ======================================================================== */
/* Init                                                                     */
/* ======================================================================== */

int wifi_init(void) {
    sec_memzero(&wifi_iface, sizeof(wifi_interface_t));
    wifi_iface.state = WIFI_STATE_DISCONNECTED;
    wifi_driver_count = 0;
    return 0;
}

int wifi_register_driver(const wifi_driver_ops_t *ops) {
    if (!ops || wifi_driver_count >= WIFI_MAX_DRIVERS) return -1;
    wifi_drivers[wifi_driver_count++] = ops;
    return 0;
}

int wifi_select_driver(uint32_t pci_vendor, uint32_t pci_device) {
    for (uint32_t i = 0; i < wifi_driver_count; i++) {
        if (wifi_drivers[i]->pci_vendor == pci_vendor &&
            wifi_drivers[i]->pci_device == pci_device) {
            wifi_iface.driver = wifi_drivers[i];
            return 0;
        }
    }
    return -1;
}

const wifi_driver_ops_t *wifi_get_driver(void) {
    return wifi_iface.driver;
}

/* ======================================================================== */
/* Scan                                                                     */
/* ======================================================================== */

int wifi_scan_start(uint8_t channel, uint8_t band) {
    if (!wifi_iface.driver) return -1;
    if (!wifi_iface.driver->scan_start) return -2;

    wifi_iface.state = WIFI_STATE_SCANNING;
    wifi_iface.channel = channel;
    wifi_iface.band = band;
    wifi_iface.scan_count = 0;

    return wifi_iface.driver->scan_start(wifi_iface.bar0, channel, band);
}

int wifi_scan_get(wifi_scan_result_t *result) {
    if (!result) return -1;
    if (!wifi_iface.driver || !wifi_iface.driver->scan_get_result) return -2;

    if (wifi_iface.scan_count >= WIFI_MAX_SCAN_RESULTS) return -3;

    int ret = wifi_iface.driver->scan_get_result(wifi_iface.bar0, result);
    if (ret == 0) {
        for (uint32_t i = 0; i < sizeof(wifi_scan_result_t); i++)
            ((uint8_t *)&wifi_iface.scan_results[wifi_iface.scan_count])[i] =
                ((uint8_t *)result)[i];
        wifi_iface.scan_count++;
    }
    return ret;
}

uint32_t wifi_scan_count(void) {
    return wifi_iface.scan_count;
}

/* ======================================================================== */
/* Auth / Assoc                                                              */
/* ======================================================================== */

int wifi_auth(const uint8_t *bssid, const uint8_t *ssid,
              uint8_t security, const uint8_t *key, uint32_t key_len) {
    if (!bssid || !ssid) return -1;
    if (!wifi_iface.driver || !wifi_iface.driver->auth) return -2;

    wifi_iface.state = WIFI_STATE_AUTHENTICATING;
    wifi_iface.security = security;

    int ret = wifi_iface.driver->auth(wifi_iface.bar0, bssid, ssid,
                                       security, key, key_len);
    if (ret == 0) {
        for (uint32_t i = 0; i < 6; i++)
            wifi_iface.connected_bssid[i] = bssid[i];
        uint32_t slen = 0;
        while (slen < WIFI_SSID_MAX_LEN && ssid[slen]) slen++;
        for (uint32_t i = 0; i < slen; i++)
            wifi_iface.connected_ssid[i] = ssid[i];
    }
    return ret;
}

int wifi_assoc(const uint8_t *bssid, uint8_t channel) {
    if (!bssid) return -1;
    if (!wifi_iface.driver || !wifi_iface.driver->assoc) return -2;

    wifi_iface.state = WIFI_STATE_ASSOCIATING;
    wifi_iface.channel = channel;

    int ret = wifi_iface.driver->assoc(wifi_iface.bar0, bssid, channel);
    if (ret == 0)
        wifi_iface.state = WIFI_STATE_CONNECTED;
    return ret;
}

int wifi_deauth(const uint8_t *bssid) {
    if (!bssid) return -1;
    if (!wifi_iface.driver || !wifi_iface.driver->deauth) return -2;

    wifi_iface.state = WIFI_STATE_DISCONNECTED;
    sec_memzero(wifi_iface.connected_bssid, 6);
    return wifi_iface.driver->deauth(wifi_iface.bar0, bssid);
}

/* ======================================================================== */
/* TX / RX                                                                   */
/* ======================================================================== */

int wifi_send(const uint8_t *frame, uint32_t len) {
    if (!frame || len == 0) return -1;
    if (!wifi_iface.driver || !wifi_iface.driver->send_frame) return -2;
    return wifi_iface.driver->send_frame(wifi_iface.bar0, frame, len);
}

int wifi_recv(uint8_t *frame, uint32_t *len) {
    if (!frame || !len) return -1;
    if (!wifi_iface.driver || !wifi_iface.driver->recv_frame) return -2;
    return wifi_iface.driver->recv_frame(wifi_iface.bar0, frame, len);
}

/* ======================================================================== */
/* Config                                                                    */
/* ======================================================================== */

void wifi_set_channel(uint8_t channel, uint8_t band) {
    if (wifi_iface.driver && wifi_iface.driver->set_channel)
        wifi_iface.driver->set_channel(wifi_iface.bar0, channel, band);
    wifi_iface.channel = channel;
    wifi_iface.band = band;
}

int wifi_set_power(uint8_t power) {
    if (!wifi_iface.driver || !wifi_iface.driver->set_power) return -1;
    return wifi_iface.driver->set_power(wifi_iface.bar0, power);
}

int wifi_get_rssi(void) {
    if (!wifi_iface.driver || !wifi_iface.driver->get_rssi) return -127;
    return wifi_iface.driver->get_rssi(wifi_iface.bar0);
}

void wifi_get_mac(uint8_t mac[6]) {
    if (wifi_iface.driver && wifi_iface.driver->get_mac)
        wifi_iface.driver->get_mac(wifi_iface.bar0, mac);
}

uint8_t wifi_get_state(void) {
    return wifi_iface.state;
}

/* ======================================================================== */
/* PCI bus scan (simplified — reads PCI config space)                        */
/* ======================================================================== */

static inline void pci_outl(uint32_t port, uint32_t val) {
    asm volatile("outl %0, %1" : : "a"(val), "Nd"((uint16_t)port));
}
static inline uint32_t pci_inl(uint32_t port) {
    uint32_t val;
    asm volatile("inl %1, %0" : "=a"(val) : "Nd"((uint16_t)port));
    return val;
}

int wifi_pci_scan(uint32_t *vendor_out, uint32_t *device_out, uint32_t *bar0_out) {
    for (uint32_t bus = 0; bus < 256; bus++) {
        for (uint32_t dev = 0; dev < 32; dev++) {
            for (uint32_t func = 0; func < 8; func++) {
                uint32_t addr = (1 << 31) | (bus << 16) | (dev << 11) | (func << 8);
                pci_outl(0xCF8, addr);
                uint32_t vendor = pci_inl(0xCFC) & 0xFFFF;
                if (vendor == 0xFFFF) continue;

                uint32_t device_id = pci_inl(0xCFC) >> 16;
                uint32_t class = pci_inl(0xCF8 + 8);
                uint8_t class_code = (class >> 24) & 0xFF;
                uint8_t subclass = (class >> 16) & 0xFF;

                /* Check for network controller (class 2) or wireless (subclass 80) */
                if (class_code == 2 && (subclass == 0x80 || subclass == 0)) {
                    uint32_t bar0 = pci_inl(0xCF8 + 16) & ~0x0F;
                    if (vendor_out) *vendor_out = vendor;
                    if (device_out) *device_out = device_id;
                    if (bar0_out) *bar0_out = bar0;
                    return 0;
                }
            }
        }
    }
    return -1;
}

wifi_interface_t *wifi_get_interface(void) {
    return &wifi_iface;
}
