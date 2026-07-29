/**
 * Chicago-95 xHCI (USB 3.0) Host Controller Driver
 * Polling-based: init, control transfers, bulk transfers, port detection
 */

#include "../kernel.h"
#include "../kmalloc.h"
#include "../pci.h"
#include "xhci.h"
#include "usb.h"

xhci_state_t xhci;

static uint64_t xhci_mmio_read(uint32_t offset) {
    return xhci.mmio[offset / 4];
}

static void xhci_mmio_write(uint32_t offset, uint32_t val) {
    xhci.mmio[offset / 4] = val;
}

static uint64_t xhci_op_read(uint32_t offset) {
    return xhci.op[offset / 4];
}

static void xhci_op_write(uint32_t offset, uint32_t val) {
    xhci.op[offset / 4] = val;
}

static void xhci_ring_doorbell(uint8_t slot, uint8_t target) {
    xhci.doorbell[slot] = target;
}

static xhci_trb_t *xhci_alloc_ring(uint32_t entries) {
    xhci_trb_t *ring = (xhci_trb_t *)kmalloc_zero(entries * sizeof(xhci_trb_t));
    return ring;
}

/* Per-slot state */
typedef struct {
    uint8_t  addressed;
    uint8_t  configured;
    uint8_t  max_packet;
    uint8_t  slot_id;
    uint8_t  port;
    uint16_t frame_idx;
    xhci_trb_t *ep_ring[32];      /* One ring per endpoint */
    uint32_t ep_ring_enqueue[32];
    uint32_t ep_ring_cycle[32];
    /* Transfer ring for slot 0 (events) */
    xhci_trb_t *event_ring;
    uint32_t event_dequeue;
    uint32_t event_cycle;
} xhci_slot_state_t;

static xhci_slot_state_t xhci_slots[128];

int xhci_init(uint16_t bus, uint8_t dev, uint8_t func) {
    __builtin_memset(&xhci, 0, sizeof(xhci));
    __builtin_memset(xhci_slots, 0, sizeof(xhci_slots));

    /* Read BAR0 */
    uint32_t bar0 = 0;
    uint32_t addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8);
    outl(0xCF8, addr | 0x10);
    bar0 = inl(0xCFC);
    if (bar0 == 0 || bar0 == 0xFFFFFFFF) return -1;

    uint64_t mmio_phys = bar0 & ~0xF;
    xhci.mmio = (volatile uint32_t *)(uint64_t)mmio_phys;

    xhci.caplength = xhci.mmio[XHCI_CAPLENGTH / 4] & 0xFF;
    xhci.op = (volatile uint32_t *)((uint8_t *)xhci.mmio + xhci.caplength);
    xhci.runtime = (volatile uint32_t *)((uint8_t *)xhci.mmio + xhci_mmio_read(XHCI_RTSOFF));
    uint32_t dboff = xhci_mmio_read(XHCI_DBOFF);
    xhci.doorbell = (volatile uint32_t *)((uint8_t *)xhci.mmio + dboff);

    uint32_t hcs1 = xhci_mmio_read(XHCI_HCSPARAMS1);
    xhci.max_slots = hcs1 & 0xFF;
    xhci.max_ports = (hcs1 >> 24) & 0xFF;
    xhci.max_interrupts = ((hcs1 >> 16) & 0x3FF) + 1;
    xhci.page_size = (xhci_op_read(XHCI_PAGESIZE) & 0xF) + 1;
    if (xhci.page_size < 12) xhci.page_size = 12;
    xhci.page_size = 1 << xhci.page_size;

    return 0;
}

int xhci_reset(void) {
    xhci_op_write(XHCI_USBCMD, xhci_op_read(XHCI_USBCMD) | 1);
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(xhci_op_read(XHCI_USBCMD) & 1)) break;
    }
    if (xhci_op_read(XHCI_USBCMD) & 1) return -1;

    xhci.dcba = (uint8_t *)kmalloc_zero((xhci.max_slots + 1) * 64);
    if (!xhci.dcba) return -1;

    xhci_op_write(XHCI_DCBAAP, (uint32_t)(uint64_t)xhci.dcba);
    xhci_op_write(XHCI_DCBAAP + 4, (uint32_t)((uint64_t)xhci.dcba >> 32));

    xhci.cmd_ring = xhci_alloc_ring(256);
    if (!xhci.cmd_ring) return -1;
    xhci.cmd_ring_cycle = 1;
    xhci.cmd_ring_enqueue = 0;

    uint32_t crcr_lo = (uint32_t)(uint64_t)xhci.cmd_ring | xhci.cmd_ring_cycle;
    uint32_t crcr_hi = (uint32_t)((uint64_t)xhci.cmd_ring >> 32);
    xhci_op_write(XHCI_CRCR, crcr_lo);
    xhci_op_write(XHCI_CRCR + 4, crcr_hi);

    xhci.event_ring = xhci_alloc_ring(256);
    xhci.event_ring_cycle = 1;
    xhci.event_ring_dequeue = 0;

    xhci_op_write(XHCI_CONFIG, xhci.max_slots);

    xhci.runtime[XHCI_IMAN(0) / 4] = 3;

    xhci_op_write(XHCI_USBCMD, xhci_op_read(XHCI_USBCMD) | 2);

    for (volatile int i = 0; i < 1000000; i++) {
        if (!(xhci_op_read(XHCI_USBSTS) & 1)) break;
    }

    return 0;
}

static int xhci_send_command(uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3) {
    uint32_t idx = xhci.cmd_ring_enqueue;
    xhci.cmd_ring[idx].field[0] = d0;
    xhci.cmd_ring[idx].field[1] = d1;
    xhci.cmd_ring[idx].field[2] = d2;
    xhci.cmd_ring[idx].field[3] = d3 | (xhci.cmd_ring_cycle ? TRB_CYCLE : 0) | TRB_IOC;

    xhci.cmd_ring_enqueue = (xhci.cmd_ring_enqueue + 1) % 255;
    xhci_ring_doorbell(0, 0);

    for (volatile int i = 0; i < 10000000; i++) {
        xhci_event_t *evt = (xhci_event_t *)&xhci.event_ring[xhci.event_ring_dequeue];
        if ((evt->dword[3] & 1) == xhci.event_ring_cycle) {
            xhci.event_ring_dequeue = (xhci.event_ring_dequeue + 1) % 256;
            return evt->dword[2] >> 24;
        }
    }
    return -1;
}

static int xhci_enable_slot(uint8_t slot_type) {
    return xhci_send_command((TRB_COMMAND << 10) | (23 << 10), 0, slot_type << 16 | (slot_type << 24), 0);
}

static int xhci_address_device(uint8_t slot, uint32_t input_ctx_phys) {
    return xhci_send_command((TRB_COMMAND << 10) | (11 << 10),
                             input_ctx_phys, 0, (uint32_t)slot << 24);
}

static int xhci_configure_endpoint(uint8_t slot, uint32_t input_ctx_phys) {
    return xhci_send_command((TRB_COMMAND << 10) | (12 << 10),
                             input_ctx_phys, 0, (uint32_t)slot << 24);
}

int xhci_port_status(int port) {
    uint32_t portsc = xhci_op_read(XHCI_PORTSC(port));
    int connected = (portsc & XHCI_PORTSC_CCS) != 0;
    int enabled = (portsc & XHCI_PORTSC_PED) != 0;

    if (portsc & XHCI_PORTSC_CSC) {
        xhci_op_write(XHCI_PORTSC(port), portsc | XHCI_PORTSC_CSC);
    }
    if (portsc & XHCI_PORTSC_PRC) {
        xhci_op_write(XHCI_PORTSC(port), portsc | XHCI_PORTSC_PRC);
    }

    return connected ? (enabled ? 2 : 1) : 0;
}

void xhci_reset_port(int port) {
    uint32_t portsc = xhci_op_read(XHCI_PORTSC(port));
    xhci_op_write(XHCI_PORTSC(port), portsc | XHCI_PORTSC_PR);
    for (volatile int i = 0; i < 10000000; i++) {
        if (!(xhci_op_read(XHCI_PORTSC(port)) & XHCI_PORTSC_PR)) break;
    }
}

int xhci_get_device_address(int slot) {
    if (slot < 0 || slot >= 128) return 0;
    return xhci_slots[slot].addressed ? slot : 0;
}

int xhci_get_device_count(void) {
    int count = 0;
    for (uint32_t p = 0; p < xhci.max_ports; p++) {
        if (xhci_port_status(p) >= 2) count++;
    }
    return count;
}

void xhci_poll_events(void) {
    for (volatile int i = 0; i < 100000; i++) {
        xhci_event_t *evt = (xhci_event_t *)&xhci.event_ring[xhci.event_ring_dequeue];
        if ((evt->dword[3] & 1) == xhci.event_ring_cycle) {
            xhci.event_ring_dequeue = (xhci.event_ring_dequeue + 1) % 256;
        } else {
            break;
        }
    }
}

/* ---- Control Transfer (EP0) ---- */

int xhci_control_transfer(int slot, uint8_t ep, const void *setup_ptr, void *data, uint16_t len) {
    if (slot < 0 || slot >= 128) return -1;
    const usb_setup_t *setup = (const usb_setup_t *)setup_ptr;

    /* Allocate TRB ring for EP0 if not done */
    if (!xhci_slots[slot].ep_ring[ep]) {
        xhci_slots[slot].ep_ring[ep] = xhci_alloc_ring(32);
        xhci_slots[slot].ep_ring_enqueue[ep] = 0;
        xhci_slots[slot].ep_ring_cycle[ep] = 1;
    }

    xhci_trb_t *ring = xhci_slots[slot].ep_ring[ep];
    uint32_t *enqueue = &xhci_slots[slot].ep_ring_enqueue[ep];
    uint32_t cycle = xhci_slots[slot].ep_ring_cycle[ep];

    /* Setup Stage TRB */
    uint32_t d0 = *(uint32_t *)setup;
    uint32_t d1 = ((uint32_t *)setup)[1];
    ring[*enqueue].field[0] = d0;
    ring[*enqueue].field[1] = d1;
    ring[*enqueue].field[2] = 8;  /* TRB transfer length = 8 for setup packet */
    ring[*enqueue].field[3] = (TRB_SETUP_STAGE << 10) | (cycle ? TRB_CYCLE : 0) | TRB_IOC |
                               (1 << 16);  /* DIR = IN */
    *enqueue = (*enqueue + 1) % 32;

    /* Data Stage TRB (if data phase) */
    if (len > 0 && data) {
        uint64_t data_phys = (uint64_t)data;
        ring[*enqueue].field[0] = (uint32_t)data_phys;
        ring[*enqueue].field[1] = (uint32_t)(data_phys >> 32);
        ring[*enqueue].field[2] = len;
        int dir_in = (setup->bmRequestType & 0x80) != 0;
        ring[*enqueue].field[3] = (TRB_DATA_STAGE << 10) | (cycle ? TRB_CYCLE : 0) |
                                   (dir_in ? TRB_DIR : 0);
        *enqueue = (*enqueue + 1) % 32;
    }

    /* Status Stage TRB */
    ring[*enqueue].field[0] = 0;
    ring[*enqueue].field[1] = 0;
    ring[*enqueue].field[2] = 0;
    int dir_in = (setup->bmRequestType & 0x80) != 0;
    ring[*enqueue].field[3] = (TRB_STATUS_STAGE << 10) | (cycle ? TRB_CYCLE : 0) |
                               (dir_in ? 0 : TRB_DIR) | TRB_IOC;
    *enqueue = (*enqueue + 1) % 32;

    /* Ring doorbell */
    xhci_ring_doorbell(slot, ep);

    /* Poll for completion */
    for (volatile int i = 0; i < 10000000; i++) {
        xhci_event_t *evt = (xhci_event_t *)&xhci.event_ring[xhci.event_ring_dequeue];
        if ((evt->dword[3] & 1) == xhci.event_ring_cycle) {
            uint32_t comp_code = (evt->dword[2] >> 24) & 0xFF;
            uint32_t evt_slot = (evt->dword[3] >> 24) & 0xFF;
            xhci.event_ring_dequeue = (xhci.event_ring_dequeue + 1) % 256;
            if (evt_slot == (uint32_t)slot) {
                return (comp_code == 1) ? 0 : -1;  /* 1 = Success */
            }
        }
    }
    return -1;
}

/* ---- Bulk Transfer ---- */

int xhci_bulk_transfer(int slot, uint8_t ep, void *data, uint32_t len, int dir_in) {
    if (slot < 0 || slot >= 128) return -1;

    int ring_idx = ep & 0x1F;
    if (!xhci_slots[slot].ep_ring[ring_idx]) {
        xhci_slots[slot].ep_ring[ring_idx] = xhci_alloc_ring(32);
        xhci_slots[slot].ep_ring_enqueue[ring_idx] = 0;
        xhci_slots[slot].ep_ring_cycle[ring_idx] = 1;
    }

    xhci_trb_t *ring = xhci_slots[slot].ep_ring[ring_idx];
    uint32_t *enqueue = &xhci_slots[slot].ep_ring_enqueue[ring_idx];
    uint32_t cycle = xhci_slots[slot].ep_ring_cycle[ring_idx];

    uint64_t data_phys = (uint64_t)data;
    ring[*enqueue].field[0] = (uint32_t)data_phys;
    ring[*enqueue].field[1] = (uint32_t)(data_phys >> 32);
    ring[*enqueue].field[2] = len;
    ring[*enqueue].field[3] = (TRB_NORMAL << 10) | (cycle ? TRB_CYCLE : 0) | TRB_IOC |
                               (dir_in ? TRB_DIR : 0);
    *enqueue = (*enqueue + 1) % 32;

    xhci_ring_doorbell(slot, ep);

    for (volatile int i = 0; i < 10000000; i++) {
        xhci_event_t *evt = (xhci_event_t *)&xhci.event_ring[xhci.event_ring_dequeue];
        if ((evt->dword[3] & 1) == xhci.event_ring_cycle) {
            uint32_t comp_code = (evt->dword[2] >> 24) & 0xFF;
            uint32_t evt_slot = (evt->dword[3] >> 24) & 0xFF;
            xhci.event_ring_dequeue = (xhci.event_ring_dequeue + 1) % 256;
            if (evt_slot == (uint32_t)slot) {
                return (comp_code == 1) ? 0 : -1;
            }
        }
    }
    return -1;
}
