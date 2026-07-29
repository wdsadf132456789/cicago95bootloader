#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 32

typedef struct {
    uint8_t bus;
    uint8_t slot;
    uint8_t func;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t revision;
    uint8_t irq;
    uint32_t bar[6];
    uint32_t bar_size[6];
} __attribute__((packed)) pci_device_t;

void pci_init(void);
uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset);
void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val);
void pci_scan(void);
int pci_find_device(uint16_t vendor_id, uint16_t device_id);
int pci_find_class(uint8_t class, uint8_t subclass);
pci_device_t *pci_get_device(int index);
int pci_get_device_count(void);
uint32_t pci_read_bar(int index, int bar);
void pci_enable_bus_mastering(pci_device_t *dev);

#endif
