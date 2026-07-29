/**
 * Chicago-95 Multi-Vendor WiFi Auto-Detection
 * Fixed PCI bus scan, enumerate all wireless controllers, vendor matching
 */

#include <stdint.h>
#include "drivers/wifi.h"

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
    int      driver_index; /* -1 = no driver matched */
} wifi_pci_device_t;

typedef struct {
    wifi_pci_device_t devices[WIFI_MAX_DETECTED];
    uint32_t          count;
    uint32_t          active_index;
    uint32_t          detected_vendors[WIFI_MAX_DETECTED];
} wifi_autodetect_state_t;

static wifi_autodetect_state_t wifi_autodetect;

/* PCI Configuration Mechanism 1 */
static inline void wifi_pci_outl(uint32_t addr) {
    __asm__ volatile("outl %0, %1" : : "a"(addr), "Nd"((uint16_t)0xCF8));
}
static inline uint32_t wifi_pci_inl(uint16_t port) {
    uint32_t val;
    __asm__ volatile("inl %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

static uint32_t wifi_pci_read(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    wifi_pci_outl(addr);
    return wifi_pci_inl(0xCFC);
}

static void wifi_pci_write(uint8_t bus, uint8_t dev, uint8_t func, uint8_t off, uint32_t val) {
    uint32_t addr = (1U << 31) | ((uint32_t)bus << 16) | ((uint32_t)dev << 11) |
                    ((uint32_t)func << 8) | (off & 0xFC);
    wifi_pci_outl(addr);
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"((uint16_t)0xCFC));
}

/* Check if vendor/device pair matches a known WiFi driver */
static int wifi_match_driver(uint32_t vendor, uint32_t device, char *name_out) {
    /* Intel */
    if (vendor == 0x8086) {
        if (device == 0x08B1 || device == 0x095A || device == 0x095B ||
            device == 0x24F3 || device == 0x24FD || device == 0x2526 ||
            device == 0xA370 || device == 0x2723 || device == 0x2725 ||
            device == 0x272B || device == 0x272C) {
            uint32_t j = 0;
            const char *n = "Intel WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 0;
        }
    }
    /* Qualcomm Atheros */
    if (vendor == 0x168C) {
        if (device == 0x002B || device == 0x002E || device == 0x0030 ||
            device == 0x0032 || device == 0x0034 || device == 0x0036 ||
            device == 0x0037 || device == 0x003C || device == 0x0046 ||
            device == 0x003E || device == 0x0050) {
            uint32_t j = 0;
            const char *n = "Atheros WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 1;
        }
    }
    /* Broadcom */
    if (vendor == 0x14E4) {
        if (device == 0x4727 || device == 0x4357 || device == 0x4349 ||
            device == 0x4331 || device == 0x43A3 || device == 0x43B1 ||
            device == 0x43DF || device == 0x4360 || device == 0x43E2 ||
            device == 0x4405) {
            uint32_t j = 0;
            const char *n = "Broadcom WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 2;
        }
    }
    /* Realtek */
    if (vendor == 0x10EC) {
        if (device == 0x8176 || device == 0x8177 || device == 0x8723 ||
            device == 0x8812 || device == 0x8813 || device == 0x8821 ||
            device == 0xB822 || device == 0x8179 || device == 0x818B) {
            uint32_t j = 0;
            const char *n = "Realtek WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 3;
        }
    }
    /* MediaTek/Ralink */
    if (vendor == 0x1814 || vendor == 0x14C3) {
        if (device == 0x0201 || device == 0x0301 || device == 0x0401 ||
            device == 0x0601 || device == 0x3090 || device == 0x3290 ||
            device == 0x539F || device == 0x5592 || device == 0x7601 ||
            device == 0x7603 || device == 0x7610 || device == 0x7612 ||
            device == 0x7615 || device == 0x7620 || device == 0x7622 ||
            device == 0x7663 || device == 0x7961) {
            uint32_t j = 0;
            const char *n = "MediaTek WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 4;
        }
    }
    /* Prism5/Intersil */
    if (vendor == 0x1260) {
        if (device == 0x3877 || device == 0x3880 || device == 0x3886 ||
            device == 0x3887 || device == 0x3890) {
            uint32_t j = 0;
            const char *n = "Prism5 WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 5;
        }
    }
    /* Marvell */
    if (vendor == 0x11AB) {
        if (device == 0x2A01 || device == 0x2A02 || device == 0x2A03 ||
            device == 0x2A04 || device == 0x2B38 || device == 0x2B39) {
            uint32_t j = 0;
            const char *n = "Marvell WiFi";
            while (n[j] && j < 47) { name_out[j] = n[j]; j++; }
            name_out[j] = 0;
            return 6;
        }
    }
    return -1;
}

/* Scan PCI bus for ALL wireless controllers */
int wifi_autodetect_scan(void) {
    uint8_t *zero = (uint8_t *)&wifi_autodetect;
    for (uint32_t i = 0; i < sizeof(wifi_autodetect_state_t); i++) zero[i] = 0;

    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t dev = 0; dev < 32; dev++) {
            uint32_t v0 = wifi_pci_read(bus, dev, 0, 0);
            uint16_t vid = (uint16_t)(v0 & 0xFFFF);
            if (vid == 0xFFFF) continue; /* no device */

            uint16_t did = (uint16_t)((v0 >> 16) & 0xFFFF);

            /* Read class/subclass (offset 0x08, high 16 bits) */
            uint32_t class_reg = wifi_pci_read(bus, dev, 0, 0x08);
            uint8_t class_code = (class_reg >> 24) & 0xFF;
            uint8_t subclass   = (class_reg >> 16) & 0xFF;

            /* Match: class 2 (network), subclass 0x80 (wireless) or 0x00 (ethernet) */
            int is_network = (class_code == 2) && (subclass == 0x80);
            if (!is_network) {
                /* Also check by known WiFi vendor IDs */
                char tmp_name[48];
                if (wifi_match_driver(vid, did, tmp_name) < 0) continue;
                is_network = 1;
            }

            if (!is_network) continue;

            /* Enable bus mastering + memory space */
            uint32_t cmd = wifi_pci_read(bus, dev, 0, 0x04);
            cmd |= 0x07; /* I/O space + Memory space + Bus master */
            wifi_pci_write(bus, dev, 0, 0x04, cmd);

            /* Read BAR0 */
            uint32_t bar0_raw = wifi_pci_read(bus, dev, 0, 0x10);
            uint32_t bar0 = bar0_raw & 0xFFFFFFF0;
            if (bar0_raw & 0x01) {
                /* I/O BAR */
                bar0 = bar0_raw & ~0x03;
            }

            /* Read IRQ */
            uint32_t irq_reg = wifi_pci_read(bus, dev, 0, 0x3C);
            uint8_t irq = (uint8_t)(irq_reg & 0xFF);

            /* Match against known drivers */
            char driver_name[48];
            int driver_idx = wifi_match_driver(vid, did, driver_name);

            if (wifi_autodetect.count < WIFI_MAX_DETECTED) {
                wifi_pci_device_t *d = &wifi_autodetect.devices[wifi_autodetect.count];
                d->bus = bus;
                d->dev = dev;
                d->func = 0;
                d->vendor_id = vid;
                d->device_id = did;
                d->bar0 = bar0;
                d->class_sub = (uint32_t)(class_code << 8 | subclass);
                d->irq = irq;
                d->driver_index = driver_idx;
                uint32_t j = 0;
                while (driver_name[j] && j < 47) { d->driver_name[j] = driver_name[j]; j++; }
                d->driver_name[j] = 0;

                wifi_autodetect.count++;
            }

            /* Check multi-function devices */
            uint32_t hdr = wifi_pci_read(bus, dev, 0, 0x0C);
            if (hdr & 0x00800000) {
                /* Multi-function: scan functions 1-7 */
                for (uint8_t func = 1; func < 8; func++) {
                    uint32_t v1 = wifi_pci_read(bus, dev, func, 0);
                    if ((uint16_t)(v1 & 0xFFFF) == 0xFFFF) continue;

                    uint16_t d1 = (uint16_t)((v1 >> 16) & 0xFFFF);
                    uint32_t c1 = wifi_pci_read(bus, dev, func, 0x08);
                    uint8_t cc1 = (c1 >> 24) & 0xFF;
                    uint8_t sc1 = (c1 >> 16) & 0xFF;

                    char dn1[48];
                    int di1 = wifi_match_driver((uint32_t)(uint16_t)(v1 & 0xFFFF), d1, dn1);

                    uint32_t b0 = wifi_pci_read(bus, dev, func, 0x10) & 0xFFFFFFF0;

                    if (wifi_autodetect.count < WIFI_MAX_DETECTED) {
                        wifi_pci_device_t *d2 = &wifi_autodetect.devices[wifi_autodetect.count];
                        d2->bus = bus;
                        d2->dev = dev;
                        d2->func = func;
                        d2->vendor_id = (uint32_t)(uint16_t)(v1 & 0xFFFF);
                        d2->device_id = d1;
                        d2->bar0 = b0;
                        d2->class_sub = (uint32_t)(cc1 << 8 | sc1);
                        d2->irq = (uint8_t)(wifi_pci_read(bus, dev, func, 0x3C) & 0xFF);
                        d2->driver_index = di1;
                        uint32_t j = 0;
                        while (dn1[j] && j < 47) { d2->driver_name[j] = dn1[j]; j++; }
                        d2->driver_name[j] = 0;
                        wifi_autodetect.count++;
                    }
                }
            }
        }
    }

    /* Select first detected device with a matching driver as active */
    wifi_autodetect.active_index = 0;
    for (uint32_t i = 0; i < wifi_autodetect.count; i++) {
        if (wifi_autodetect.devices[i].driver_index >= 0) {
            wifi_autodetect.active_index = i;
            break;
        }
    }

    return (int)wifi_autodetect.count;
}

/* Initialize the active WiFi driver */
int wifi_autodetect_init(void) {
    int count = wifi_autodetect_scan();

    if (count == 0) return -1;

    /* Try to init each detected device with its matching driver via wifi_select_driver */
    for (uint32_t i = 0; i < wifi_autodetect.count; i++) {
        wifi_pci_device_t *dev = &wifi_autodetect.devices[i];
        if (dev->driver_index < 0) continue;

        /* Use the core wifi_select_driver to match vendor+device */
        int result = wifi_select_driver(dev->vendor_id, dev->device_id);
        if (result != 0) continue;

        /* Driver selected — init it via the vtable */
        wifi_interface_t *iface = wifi_get_interface();
        if (iface && iface->driver && iface->driver->init) {
            result = iface->driver->init(dev->bar0, iface->mac);
            if (result == 0) {
                wifi_autodetect.active_index = i;
                return 0;
            }
        }
    }

    return -2; /* all init attempts failed */
}

/* Get auto-detect results */
uint32_t wifi_autodetect_get_count(void) {
    return wifi_autodetect.count;
}

const wifi_pci_device_t *wifi_autodetect_get_device(uint32_t index) {
    if (index >= wifi_autodetect.count) return (void *)0;
    return &wifi_autodetect.devices[index];
}

int wifi_autodetect_get_active(wifi_pci_device_t *out) {
    if (wifi_autodetect.count == 0) return -1;
    *out = wifi_autodetect.devices[wifi_autodetect.active_index];
    return 0;
}
