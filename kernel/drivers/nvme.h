#ifndef NVME_H
#define NVME_H

#include <stdint.h>

#define NVME_CMD_ADMIN_CREATE_IOQS   0x05
#define NVME_CMD_ADMIN_IDENTIFY      0x06
#define NVME_CMD_IO_READ             0x02
#define NVME_CMD_IO_WRITE            0x01

#define NVME_ADMIN_QUEUE  0
#define NVME_IO_QUEUE     1

typedef struct {
    uint32_t dword[16];
} __attribute__((aligned(64))) nvme_cmd_t;

typedef struct {
    uint32_t dword[4];
} __attribute__((aligned(16))) nvme_cpl_t;

/* NVMe registers (BAR0) */
typedef struct {
    volatile uint32_t *mmio;
    volatile uint32_t *aq;      /* Admin submission queue */
    volatile uint32_t *acq;     /* Admin completion queue */
    volatile uint32_t *iosq;    /* I/O submission queue */
    volatile uint32_t *iocq;    /* I/O completion queue */
    uint32_t page_size;
    uint32_t max_queue_entries;
    uint32_t ns_count;
    uint32_t sector_size;
    uint64_t total_sectors;
    uint32_t cmd_tail;
    uint32_t cpl_head;
    uint8_t  ready;
} nvme_state_t;

extern nvme_state_t nvme;

int      nvme_init(uint16_t bus, uint8_t dev, uint8_t func);
int      nvme_reset(void);
int      nvme_read(uint32_t ns, uint64_t lba, uint32_t count, void *buf);
int      nvme_write(uint32_t ns, uint64_t lba, uint32_t count, const void *buf);
uint32_t nvme_get_sector_size(uint32_t ns);
uint64_t nvme_get_total_sectors(uint32_t ns);
int      nvme_get_device_count(void);

#endif
