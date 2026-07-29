#ifndef USB_H
#define USB_H

#include <stdint.h>

/* USB Descriptor Types */
#define USB_DESC_DEVICE         1
#define USB_DESC_CONFIGURATION  2
#define USB_DESC_STRING         3
#define USB_DESC_INTERFACE      4
#define USB_DESC_ENDPOINT       5

/* USB Class Codes */
#define USB_CLASS_MASS_STORAGE  0x08
#define USB_CLASS_HUB           0x09

/* USB Mass Storage protocols */
#define USB_MASS_BBB            0  /* Bulk-Only Transport */

/* USB Setup Packet (8 bytes) */
typedef struct {
    uint8_t  bmRequestType;
    uint8_t  bRequest;
    uint16_t wValue;
    uint16_t wIndex;
    uint16_t wLength;
} __attribute__((packed)) usb_setup_t;

/* USB Device Descriptor (18 bytes) */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdUSB;
    uint8_t  bDeviceClass;
    uint8_t  bDeviceSubClass;
    uint8_t  bDeviceProtocol;
    uint8_t  bMaxPacketSize0;
    uint16_t idVendor;
    uint16_t idProduct;
    uint16_t bcdDevice;
    uint8_t  iManufacturer;
    uint8_t  iProduct;
    uint8_t  iSerialNumber;
    uint8_t  bNumConfigurations;
} __attribute__((packed)) usb_device_desc_t;

/* USB Endpoint Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bEndpointAddress;
    uint8_t  bmAttributes;
    uint16_t wMaxPacketSize;
    uint8_t  bInterval;
} __attribute__((packed)) usb_endpoint_desc_t;

/* USB Interface Descriptor */
typedef struct {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint8_t  bInterfaceNumber;
    uint8_t  bAlternateSetting;
    uint8_t  bNumEndpoints;
    uint8_t  bInterfaceClass;
    uint8_t  bInterfaceSubClass;
    uint8_t  bInterfaceProtocol;
    uint8_t  iInterface;
} __attribute__((packed)) usb_interface_desc_t;

/* Bulk-Only Command Block Wrapper */
typedef struct {
    uint32_t dCBWSignature;
    uint32_t dCBWTag;
    uint32_t dCBWDataTransferLength;
    uint8_t  bmCBWFlags;
    uint8_t  bCBWLUN;
    uint8_t  bCBWCBLength;
    uint8_t  CBWCB[16];
} __attribute__((packed)) usb_mass_cbw_t;

/* Bulk-Only Command Status Wrapper */
typedef struct {
    uint32_t dCSWSignature;
    uint32_t dCSWTag;
    uint32_t dCSWDataResidue;
    uint8_t  bCSWStatus;
} __attribute__((packed)) usb_mass_csw_t;

typedef struct {
    uint8_t  port;           /* USB port number */
    uint8_t  slot;           /* xHCI slot ID */
    uint8_t  address;        /* USB device address */
    uint8_t  ep_in;          /* Bulk IN endpoint */
    uint8_t  ep_out;         /* Bulk OUT endpoint */
    uint8_t  max_packet;     /* Max packet size */
    uint8_t  active;         /* Device is active */
    usb_device_desc_t desc;
} usb_device_t;

#define USB_MAX_DEVICES  16

int      usb_init(void);
int      usb_get_device_count(void);
int      usb_read_sectors(uint64_t lba, uint32_t count, void *buf);
int      usb_write_sectors(uint64_t lba, uint32_t count, const void *buf);
uint32_t usb_get_sector_size(void);

#endif
