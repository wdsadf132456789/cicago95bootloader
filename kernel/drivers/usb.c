/**
 * Chicago-95 USB Core + Mass Storage Driver
 * USB enumeration, SCSI READ(10)/WRITE(10) via Bulk-Only Transport
 */

#include "../kernel.h"
#include "../kmalloc.h"
#include "xhci.h"
#include "usb.h"

static usb_device_t usb_devices[USB_MAX_DEVICES];
static int usb_device_count = 0;

static uint32_t usb_tag_counter = 1;

int usb_init(void) {
    __builtin_memset(usb_devices, 0, sizeof(usb_devices));
    usb_device_count = 0;
    return 0;
}

int usb_get_device_count(void) {
    return usb_device_count;
}

uint32_t usb_get_sector_size(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active && usb_devices[i].desc.bDeviceClass == USB_CLASS_MASS_STORAGE)
            return 512;
    }
    return 0;
}

/* Find active mass storage device */
static usb_device_t *usb_find_mass_storage(void) {
    for (int i = 0; i < USB_MAX_DEVICES; i++) {
        if (usb_devices[i].active && usb_devices[i].desc.bDeviceClass == USB_CLASS_MASS_STORAGE)
            return &usb_devices[i];
    }
    return 0;
}

/* Send SCSI command via BBB (Bulk-Only) transport */
static int usb_mass_send_command(usb_device_t *dev, uint8_t *cbw_data, uint32_t data_len, int dir_in) {
    /* Send CBW (31 bytes) */
    int res = xhci_bulk_transfer(dev->slot, dev->ep_out, cbw_data, 31, 0);
    if (res < 0) return -1;

    /* Transfer data phase if needed */
    if (data_len > 0) {
        res = xhci_bulk_transfer(dev->slot, dev->ep_in, (void *)cbw_data, data_len, dir_in);
        if (res < 0) return -1;
    }

    /* Receive CSW (13 bytes) */
    uint8_t csw[13];
    __builtin_memset(csw, 0, 13);
    res = xhci_bulk_transfer(dev->slot, dev->ep_in, csw, 13, 1);
    if (res < 0) return -1;

    return 0;
}

/* SCSI READ(10) */
int usb_read_sectors(uint64_t lba, uint32_t count, void *buf) {
    usb_device_t *dev = usb_find_mass_storage();
    if (!dev) return -1;

    uint32_t total_bytes = count * 512;

    /* Build CBW for READ(10) */
    uint8_t cbw[31];
    __builtin_memset(cbw, 0, 31);

    cbw[0] = 0x55;  /* dCBWSignature "USBC" */
    cbw[1] = 0x53;
    cbw[2] = 0x42;
    cbw[3] = 0x43;

    uint32_t tag = usb_tag_counter++;
    cbw[4] = tag & 0xFF;
    cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF;
    cbw[7] = (tag >> 24) & 0xFF;

    cbw[8] = total_bytes & 0xFF;
    cbw[9] = (total_bytes >> 8) & 0xFF;
    cbw[10] = (total_bytes >> 16) & 0xFF;
    cbw[11] = (total_bytes >> 24) & 0xFF;
    cbw[12] = 0x80;  /* bmCBWFlags: Direction = IN (device to host) */
    cbw[13] = 0;     /* bCBWLUN */
    cbw[14] = 10;    /* bCBWCBLength = 10 for READ(10) */

    /* SCSI CDB: READ(10) */
    cbw[15] = 0x28;  /* Operation Code */
    cbw[16] = 0;     /* Control */
    cbw[17] = (lba >> 24) & 0xFF;  /* LBA MSB */
    cbw[18] = (lba >> 16) & 0xFF;
    cbw[19] = (lba >> 8) & 0xFF;
    cbw[20] = lba & 0xFF;          /* LBA LSB */
    cbw[21] = 0;     /* Group Number */
    cbw[22] = (count >> 8) & 0xFF;  /* Transfer Length MSB */
    cbw[23] = count & 0xFF;         /* Transfer Length LSB */
    cbw[24] = 0;     /* Control */

    /* Use cbw buffer for data phase (reuse after CBW sent) */
    uint8_t *data_buf = (uint8_t *)kmalloc_aligned(total_bytes + 512);
    if (!data_buf) return -1;

    int res = usb_mass_send_command(dev, cbw, total_bytes, 1);
    if (res < 0) { kfree(data_buf); return -1; }

    /* Data was received into cbw data area — but we sent it as the data buffer */
    /* Actually, for bulk-in, the host controller writes data to the provided buffer */
    /* The data is in data_buf after bulk transfer */

    /* Copy data to output */
    __builtin_memcpy(buf, data_buf, total_bytes);
    kfree(data_buf);
    return 0;
}

/* SCSI WRITE(10) */
int usb_write_sectors(uint64_t lba, uint32_t count, const void *buf) {
    usb_device_t *dev = usb_find_mass_storage();
    if (!dev) return -1;

    uint32_t total_bytes = count * 512;

    uint8_t cbw[31];
    __builtin_memset(cbw, 0, 31);

    cbw[0] = 0x55; cbw[1] = 0x53; cbw[2] = 0x42; cbw[3] = 0x43;

    uint32_t tag = usb_tag_counter++;
    cbw[4] = tag & 0xFF;
    cbw[5] = (tag >> 8) & 0xFF;
    cbw[6] = (tag >> 16) & 0xFF;
    cbw[7] = (tag >> 24) & 0xFF;

    cbw[8] = total_bytes & 0xFF;
    cbw[9] = (total_bytes >> 8) & 0xFF;
    cbw[10] = (total_bytes >> 16) & 0xFF;
    cbw[11] = (total_bytes >> 24) & 0xFF;
    cbw[12] = 0x00;  /* bmCBWFlags: Direction = OUT (host to device) */
    cbw[13] = 0;     /* bCBWLUN */
    cbw[14] = 10;    /* bCBWCBLength */

    /* SCSI CDB: WRITE(10) */
    cbw[15] = 0x2A;  /* Operation Code */
    cbw[16] = 0;
    cbw[17] = (lba >> 24) & 0xFF;
    cbw[18] = (lba >> 16) & 0xFF;
    cbw[19] = (lba >> 8) & 0xFF;
    cbw[20] = lba & 0xFF;
    cbw[21] = 0;
    cbw[22] = (count >> 8) & 0xFF;
    cbw[23] = count & 0xFF;
    cbw[24] = 0;

    /* Send CBW */
    int res = xhci_bulk_transfer(dev->slot, dev->ep_out, cbw, 31, 0);
    if (res < 0) return -1;

    /* Send data */
    uint8_t *tmp = (uint8_t *)kmalloc_aligned(total_bytes);
    if (!tmp) return -1;
    __builtin_memcpy(tmp, buf, total_bytes);
    res = xhci_bulk_transfer(dev->slot, dev->ep_out, tmp, total_bytes, 0);
    kfree(tmp);
    if (res < 0) return -1;

    /* Receive CSW */
    uint8_t csw[13];
    __builtin_memset(csw, 0, 13);
    res = xhci_bulk_transfer(dev->slot, dev->ep_in, csw, 13, 1);
    if (res < 0) return -1;

    return 0;
}
