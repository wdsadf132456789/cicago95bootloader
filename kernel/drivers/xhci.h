#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>

/* xHCI PCI register offsets (BAR0 MMIO) */
#define XHCI_CAPLENGTH    0x00
#define XHCI_HCIVERSION   0x02
#define XHCI_HCSPARAMS1   0x04
#define XHCI_HCSPARAMS2   0x08
#define XHCI_HCCPARAMS    0x10
#define XHCI_DBOFF        0x14
#define XHCI_RTSOFF       0x18

/* Operational registers (base = BAR0 + CAPLENGTH) */
#define XHCI_USBCMD       0x00
#define XHCI_USBSTS       0x04
#define XHCI_PAGESIZE     0x08
#define XHCI_DNCTRL       0x14
#define XHCI_CRCR         0x18
#define XHCI_DCBAAP       0x30
#define XHCI_CONFIG       0x38

/* Port Status/Control */
#define XHCI_PORTSC(p)    (0x400 + 0x10 * (p))
#define XHCI_PORTSC_CCS   (1 << 0)   /* Current Connect Status */
#define XHCI_PORTSC_PED   (1 << 2)   /* Port Enabled/Disabled */
#define XHCI_PORTSC_PR    (1 << 4)   /* Port Reset */
#define XHCI_PORTSC_PLS   (0xF << 5) /* Port Link State */
#define XHCI_PORTSC_CSC   (1 << 17)  /* Connect Status Change */
#define XHCI_PORTSC_PRC   (1 << 21)  /* Port Reset Change */
#define XHCI_PORTSC_WPS   (1 << 31)  /* Warm Port Reset */

/* Runtime registers */
#define XHCI_MFINDEX      0x00
#define XHCI_IMAN(p)      (0x20 + 0x20 * (p))
#define XHCI_IMOD(p)      (0x24 + 0x20 * (p))
#define XHCI_ERSTSZ(p)    (0x28 + 0x20 * (p))
#define XHCI_ERSTBA(p)    (0x30 + 0x20 * (p))
#define XHCI_ERDP(p)      (0x38 + 0x20 * (p))
#define XHCI hexatrigesimal(p)   (0x40 + 0x20 * (p))

/* TRB types */
#define TRB_NORMAL        1
#define TRB_SETUP_STAGE   2
#define TRB_DATA_STAGE    3
#define TRB_STATUS_STAGE  4
#define TRB_LINK          6
#define TRB_COMMAND       23
#define TRB_EVENT         32

/* TRB flags */
#define TRB_CYCLE         (1 << 0)
#define TRB_IOC           (1 << 5)
#define TRB_IDT           (1 << 6)
#define TRB_DIR           (1 << 16)  /* 0=OUT, 1=IN */

/* USB Request Codes */
#define USB_REQ_GET_STATUS     0
#define USB_REQ_CLEAR_FEATURE  1
#define USB_REQ_SET_FEATURE    3
#define USB_REQ_SET_ADDRESS    5
#define USB_REQ_GET_DESCRIPTOR 6
#define USB_REQ_SET_DESCRIPTOR 7
#define USB_REQ_GET_CONFIG     8
#define USB_REQ_SET_CONFIG     9

/* Descriptor types */
#define USB_DESC_DEVICE        1
#define USB_DESC_CONFIG        2
#define USB_DESC_STRING        3
#define USB_DESC_INTERFACE     4
#define USB_DESC_ENDPOINT      5

typedef struct {
    uint32_t field[4];
} __attribute__((aligned(16))) xhci_trb_t;

typedef struct {
    uint32_t dword[4];
} __attribute__((packed)) xhci_event_t;

typedef struct {
    volatile uint32_t *mmio;
    uint32_t caplength;
    volatile uint32_t *op;
    volatile uint32_t *runtime;
    volatile uint32_t *doorbell;
    uint32_t max_slots;
    uint32_t max_ports;
    uint32_t max_interrupts;
    uint32_t page_size;
    uint8_t  *dcba;       /* Device Context Base Address Array */
    xhci_trb_t *cmd_ring;
    xhci_trb_t *event_ring;
    uint32_t cmd_ring_cycle;
    uint32_t event_ring_cycle;
    uint32_t cmd_ring_enqueue;
    uint32_t event_ring_dequeue;
} xhci_state_t;

extern xhci_state_t xhci;

int      xhci_init(uint16_t bus, uint8_t dev, uint8_t func);
int      xhci_reset(void);
int      xhci_port_status(int port);
void     xhci_reset_port(int port);
int      xhci_get_device_address(int slot);
int      xhci_control_transfer(int slot, uint8_t ep, const void *setup, void *data, uint16_t len);
int      xhci_bulk_transfer(int slot, uint8_t ep, void *data, uint32_t len, int dir_in);
void     xhci_poll_events(void);
int      xhci_get_device_count(void);

#endif
