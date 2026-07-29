#include "pci.h"
#include "kernel.h"

static pci_device_t pci_devices[PCI_MAX_DEVICES];
static int pci_device_count = 0;

uint32_t pci_read_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t address = (1 << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)(slot & 0x1F) << 11) |
        ((uint32_t)(func & 0x07) << 8) |
        (offset & 0xFC);
    outb(0xCF8, address & 0xFF);
    outb(0xCF8 + 1, (address >> 8) & 0xFF);
    outb(0xCF8 + 2, (address >> 16) & 0xFF);
    outb(0xCF8 + 3, (address >> 24) & 0xFF);
    uint32_t result = inb(0xCFC);
    result |= (uint32_t)inb(0xCFC + 1) << 8;
    result |= (uint32_t)inb(0xCFC + 2) << 16;
    result |= (uint32_t)inb(0xCFC + 3) << 24;
    return result;
}

uint16_t pci_read_word(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_dword(bus, slot, func, offset);
    return (uint16_t)((dword >> ((offset & 2) * 8)) & 0xFFFF);
}

uint8_t pci_read_byte(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset) {
    uint32_t dword = pci_read_dword(bus, slot, func, offset);
    return (uint8_t)((dword >> ((offset & 3) * 8)) & 0xFF);
}

void pci_write_dword(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset, uint32_t val) {
    uint32_t address = (1 << 31) |
        ((uint32_t)bus << 16) |
        ((uint32_t)(slot & 0x1F) << 11) |
        ((uint32_t)(func & 0x07) << 8) |
        (offset & 0xFC);
    outb(0xCF8, address & 0xFF);
    outb(0xCF8 + 1, (address >> 8) & 0xFF);
    outb(0xCF8 + 2, (address >> 16) & 0xFF);
    outb(0xCF8 + 3, (address >> 24) & 0xFF);
    outb(0xCFC, val & 0xFF);
    outb(0xCFC + 1, (val >> 8) & 0xFF);
    outb(0xCFC + 2, (val >> 16) & 0xFF);
    outb(0xCFC + 3, (val >> 24) & 0xFF);
}

static void pci_check_device(uint8_t bus, uint8_t slot) {
    uint16_t vendor = pci_read_word(bus, slot, 0, 0);
    if (vendor == 0xFFFF) return;

    if (pci_device_count >= PCI_MAX_DEVICES) return;

    pci_device_t *dev = &pci_devices[pci_device_count++];
    dev->bus = bus;
    dev->slot = slot;
    dev->func = 0;
    dev->vendor_id = vendor;
    dev->device_id = pci_read_word(bus, slot, 0, 2);
    dev->class = pci_read_byte(bus, slot, 0, 8);
    dev->subclass = pci_read_byte(bus, slot, 0, 9);
    dev->prog_if = pci_read_byte(bus, slot, 0, 10);
    dev->revision = pci_read_byte(bus, slot, 0, 8);
    dev->irq = pci_read_byte(bus, slot, 0, 0x3C);

    /* Read BARs (offsets 0x10-0x24) */
    for (int b = 0; b < 6; b++) {
        dev->bar[b] = pci_read_dword(bus, slot, 0, 0x10 + b * 4);
    }

    uint8_t header_type = pci_read_byte(bus, slot, 0, 14);
    if (header_type & 0x80) {
        for (uint8_t func = 1; func < 8; func++) {
            uint16_t v = pci_read_word(bus, slot, func, 0);
            if (v != 0xFFFF && pci_device_count < PCI_MAX_DEVICES) {
                dev = &pci_devices[pci_device_count++];
                dev->bus = bus;
                dev->slot = slot;
                dev->func = func;
                dev->vendor_id = v;
                dev->device_id = pci_read_word(bus, slot, func, 2);
                dev->class = pci_read_byte(bus, slot, func, 8);
                dev->subclass = pci_read_byte(bus, slot, func, 9);
                dev->prog_if = pci_read_byte(bus, slot, func, 10);
                dev->revision = pci_read_byte(bus, slot, func, 8);
                dev->irq = pci_read_byte(bus, slot, func, 0x3C);
                for (int b = 0; b < 6; b++) {
                    dev->bar[b] = pci_read_dword(bus, slot, func, 0x10 + b * 4);
                }
            }
        }
    }
}

void pci_init(void) {
    pci_device_count = 0;
    __builtin_memset(pci_devices, 0, sizeof(pci_devices));
    pci_scan();
}

void pci_scan(void) {
    pci_device_count = 0;
    for (uint16_t bus = 0; bus < 256; bus++) {
        for (uint8_t slot = 0; slot < 32; slot++) {
            pci_check_device(bus, slot);
        }
    }
}

int pci_find_device(uint16_t vendor_id, uint16_t device_id) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].vendor_id == vendor_id && pci_devices[i].device_id == device_id) {
            return i;
        }
    }
    return -1;
}

pci_device_t *pci_get_device(int index) {
    if (index < 0 || index >= pci_device_count) return 0;
    return &pci_devices[index];
}

int pci_get_device_count(void) {
    return pci_device_count;
}

int pci_find_class(uint8_t class, uint8_t subclass) {
    for (int i = 0; i < pci_device_count; i++) {
        if (pci_devices[i].class == class && pci_devices[i].subclass == subclass)
            return i;
    }
    return -1;
}

uint32_t pci_read_bar(int index, int bar) {
    if (index < 0 || index >= pci_device_count || bar < 0 || bar >= 6) return 0;
    return pci_devices[index].bar[bar];
}

void pci_enable_bus_mastering(pci_device_t *dev) {
    if (!dev) return;
    uint16_t cmd = pci_read_word(dev->bus, dev->slot, dev->func, 0x04);
    cmd |= (1 << 2);  /* Bus Master Enable */
    cmd |= (1 << 1);  /* Memory Space Enable */
    pci_write_dword(dev->bus, dev->slot, dev->func, 0x04, cmd);
}
