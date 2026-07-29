/**
 * Chicago-95 WiFi Auto-Detection
 * Multi-vendor PCI bus scan and driver matching
 */

#ifndef WIFI_AUTODETECT_H
#define WIFI_AUTODETECT_H

#include <stdint.h>

#define WIFI_MAX_DETECTED 8

typedef struct {
    uint32_t bus;
    uint32_t dev;
    uint32_t func;
    uint32_t vendor_id;
    uint32_t device_id;
    uint32_t bar0;
    uint32_t class_sub;
    uint8_t  irq;
    char     driver_name[48];
    int      driver_index;
} wifi_pci_device_t;

/* Scan PCI bus for all wireless controllers */
int wifi_autodetect_scan(void);

/* Init the first detected WiFi device */
int wifi_autodetect_init(void);

/* Query results */
uint32_t wifi_autodetect_get_count(void);
const wifi_pci_device_t *wifi_autodetect_get_device(uint32_t index);
int wifi_autodetect_get_active(wifi_pci_device_t *out);

#endif
