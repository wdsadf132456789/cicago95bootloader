/**
 * Chicago-95 NVMe Storage Controller Driver
 * Polling-based: init, identify, I/O queue, LBA read/write
 */

#include "../kernel.h"
#include "../kmalloc.h"
#include "../pci.h"
#include "nvme.h"

nvme_state_t nvme;

static void nvme_mmio_write(uint32_t offset, uint32_t val) {
    nvme.mmio[offset / 4] = val;
}

static uint32_t nvme_mmio_read(uint32_t offset) {
    return nvme.mmio[offset / 4];
}

/* Submit a command to the admin or I/O submission queue */
static void nvme_submit_cmd(volatile uint32_t *sq, uint32_t *tail, nvme_cmd_t *cmd) {
    volatile uint32_t *slot = &sq[*tail * 16];
    for (int i = 0; i < 16; i++) slot[i] = cmd->dword[i];
    (*tail)++;
    if (*tail >= nvme.max_queue_entries) *tail = 0;
}

/* Wait for admin completion */
static int nvme_wait_admin_cpl(void) {
    for (volatile int i = 0; i < 10000000; i++) {
        nvme_cpl_t *cpl = (nvme_cpl_t *)&nvme.acq[nvme.cpl_head * 4];
        uint32_t phase = cpl->dword[3] & 1;
        if (phase == 1) {
            uint32_t status = (cpl->dword[3] >> 17) & 0x7FF;
            nvme.cpl_head = (nvme.cpl_head + 1) % nvme.max_queue_entries;
            return (status >> 1) & 0xFF;  /* Status field */
        }
    }
    return -1;  /* Timeout */
}

int nvme_init(uint16_t bus, uint8_t dev, uint8_t func) {
    __builtin_memset(&nvme, 0, sizeof(nvme));

    uint32_t bar0 = 0;
    uint32_t addr = 0x80000000 | (bus << 16) | (dev << 11) | (func << 8);
    outl(0xCF8, addr | 0x10);
    bar0 = inl(0xCFC);
    if (bar0 == 0 || bar0 == 0xFFFFFFFF) return -1;

    uint64_t mmio_phys = bar0 & ~0xF;
    nvme.mmio = (volatile uint32_t *)(uint64_t)mmio_phys;

    /* Enable bus mastering */
    outl(0xCF8, addr | 0x04);
    outw(0xCFC, inw(0xCFC) | 0x04 | 0x01);

    return 0;
}

int nvme_reset(void) {
    /* Reset controller */
    nvme_mmio_write(0x00, 0x00);  /* Set CC.EN = 0 */
    for (volatile int i = 0; i < 1000000; i++) {
        if (!(nvme_mmio_read(0x08) & 1)) break;  /* CSTS.RDY = 0 */
    }

    /* AQA: admin queue entries - 1 */
    nvme_mmio_write(0x24, 0x03FF);   /* ASQ entries = 1024 */
    nvme_mmio_write(0x28, 0x03FF);   /* ACQ entries = 1024 */

    /* Allocate admin queues (4KB each) */
    nvme.aq = (volatile uint32_t *)kmalloc_zero(4096);
    nvme.acq = (volatile uint32_t *)kmalloc_zero(4096);
    if (!nvme.aq || !nvme.acq) return -1;

    /* ASQ and ACQ base addresses */
    nvme_mmio_write(0x28, (uint32_t)(uint64_t)nvme.aq);
    nvme_mmio_write(0x2C, (uint32_t)((uint64_t)nvme.aq >> 32));
    nvme_mmio_write(0x30, (uint32_t)(uint64_t)nvme.acq);
    nvme_mmio_write(0x34, (uint32_t)((uint64_t)nvme.acq >> 32));

    nvme.max_queue_entries = 1024;
    nvme.cmd_tail = 0;
    nvme.cpl_head = 0;

    /* Enable controller: CC.EN = 1, CSS = NVM command set, MPS = 0 (4KB) */
    uint32_t cc = (1 << 0)       /* EN */
                | (0 << 4)       /* CSS = NVM Command Set */
                | (0 << 7)       /* MPS = 4KB */
                | (0 << 10)      /* AMS = Round Robin */
                | (6 << 16)      /* IOSQES = 64 bytes (2^6) */
                | (4 << 20);     /* IOCQES = 16 bytes (2^4) */
    nvme_mmio_write(0x14, cc);

    /* Wait for CSTS.RDY = 1 */
    for (volatile int i = 0; i < 10000000; i++) {
        if (nvme_mmio_read(0x08) & 1) break;
    }
    if (!(nvme_mmio_read(0x08) & 1)) return -1;

    nvme.ready = 1;

    /* Identify Controller */
    void *id_buf = kmalloc_zero(4096);
    if (!id_buf) return -1;

    nvme_cmd_t cmd;
    __builtin_memset(&cmd, 0, sizeof(cmd));
    cmd.dword[0] = (NVME_CMD_ADMIN_IDENTIFY << 16) | 0;
    cmd.dword[1] = 0;
    cmd.dword[2] = 0;
    cmd.dword[3] = (1 << 14);  /* CNS = 1 (controller) */
    /* PRP1 = physical address of buffer */
    cmd.dword[6] = (uint32_t)(uint64_t)id_buf;
    cmd.dword[7] = (uint32_t)((uint64_t)id_buf >> 32);

    nvme_submit_cmd(nvme.aq, &nvme.cmd_tail, &cmd);
    /* Ring admin doorbell (SQ tail = 0, so doorbell offset 0) */
    nvme.mmio[0x1000 / 4] = nvme.cmd_tail;
    nvme_wait_admin_cpl();

    /* Read page size from controller data */
    uint8_t *id = (uint8_t *)id_buf;
    nvme.page_size = 1 << (12 + ((id[26] >> 4) & 0xF));
    nvme.ns_count = 0;

    /* Identify Namespaces */
    for (uint32_t nsid = 1; nsid <= 16; nsid++) {
        __builtin_memset(id_buf, 0, 4096);
        __builtin_memset(&cmd, 0, sizeof(cmd));
        cmd.dword[0] = (NVME_CMD_ADMIN_IDENTIFY << 16) | 0;
        cmd.dword[3] = (0 << 14);  /* CNS = 0 (namespace) */
        cmd.dword[10] = nsid;
        cmd.dword[6] = (uint32_t)(uint64_t)id_buf;
        cmd.dword[7] = (uint32_t)((uint64_t)id_buf >> 32);

        nvme_submit_cmd(nvme.aq, &nvme.cmd_tail, &cmd);
        nvme.mmio[0x1000 / 4] = nvme.cmd_tail;
        if (nvme_wait_admin_cpl() == 0) {
            /* Parse LBA format from namespace data */
            uint32_t ns_size = *(uint32_t *)&id[72];   /* NSZE in sectors */
            uint8_t lba_fmt = id[26];                   /* FLBAS */
            uint8_t fmt_idx = lba_fmt & 0x0F;
            uint32_t lbaf0_lbas = *(uint32_t *)&id[128 + fmt_idx * 4 + 4];
            uint8_t lbaf0_lbads = lbaf0_lbas & 0xFF;
            nvme.sector_size = 1 << lbaf0_lbads;
            nvme.total_sectors = ns_size;
            nvme.ns_count = nsid;
        } else {
            break;
        }
    }

    /* Allocate I/O queues */
    nvme.iosq = (volatile uint32_t *)kmalloc_zero(4096);
    nvme.iocq = (volatile uint32_t *)kmalloc_zero(4096);

    /* Create I/O Completion Queue */
    __builtin_memset(&cmd, 0, sizeof(cmd));
    cmd.dword[0] = (NVME_CMD_ADMIN_CREATE_IOQS << 16) | 0;
    cmd.dword[1] = (uint32_t)(uint64_t)nvme.iocq;
    cmd.dword[2] = (uint32_t)((uint64_t)nvme.iocq >> 32);
    cmd.dword[3] = (1 << 14) | ((4 & 0xF) << 16) | ((1023 & 0xFFFF) << 16);
    nvme_submit_cmd(nvme.aq, &nvme.cmd_tail, &cmd);
    nvme.mmio[0x1000 / 4] = nvme.cmd_tail;
    nvme_wait_admin_cpl();

    /* Create I/O Submission Queue */
    __builtin_memset(&cmd, 0, sizeof(cmd));
    cmd.dword[0] = (NVME_CMD_ADMIN_CREATE_IOQS << 16) | 1;
    cmd.dword[1] = (uint32_t)(uint64_t)nvme.iosq;
    cmd.dword[2] = (uint32_t)((uint64_t)nvme.iosq >> 32);
    cmd.dword[3] = (0 << 14) | ((6 & 0xF) << 16) | ((1023 & 0xFFFF) << 16) | (NVME_IO_QUEUE << 16);
    nvme_submit_cmd(nvme.aq, &nvme.cmd_tail, &cmd);
    nvme.mmio[0x1000 / 4] = nvme.cmd_tail;
    nvme_wait_admin_cpl();

    kfree(id_buf);
    return 0;
}

int nvme_read(uint32_t ns, uint64_t lba, uint32_t count, void *buf) {
    if (!nvme.ready || !buf) return -1;

    nvme_cmd_t cmd;
    __builtin_memset(&cmd, 0, sizeof(cmd));
    cmd.dword[0] = (NVME_CMD_IO_READ << 16) | (ns & 0xFFFF);
    cmd.dword[10] = lba & 0xFFFFFFFF;
    cmd.dword[11] = (lba >> 32) & 0xFFFFFFFF;
    cmd.dword[12] = (count - 1) & 0xFFFF;
    cmd.dword[6] = (uint32_t)(uint64_t)buf;
    cmd.dword[7] = (uint32_t)((uint64_t)buf >> 32);

    nvme_submit_cmd(nvme.iosq, &nvme.cmd_tail, &cmd);
    nvme.mmio[0x1000 / 4 + 2 * NVME_IO_QUEUE] = nvme.cmd_tail;

    /* Poll I/O completion queue */
    for (volatile int i = 0; i < 10000000; i++) {
        nvme_cpl_t *cpl = (nvme_cpl_t *)&nvme.iocq[nvme.cpl_head * 4];
        if ((cpl->dword[3] & 1) == 1) {
            uint32_t status = (cpl->dword[3] >> 17) & 0xFF;
            nvme.cpl_head = (nvme.cpl_head + 1) % 1024;
            return (status == 0) ? 0 : -1;
        }
    }
    return -1;
}

int nvme_write(uint32_t ns, uint64_t lba, uint32_t count, const void *buf) {
    if (!nvme.ready || !buf) return -1;

    nvme_cmd_t cmd;
    __builtin_memset(&cmd, 0, sizeof(cmd));
    cmd.dword[0] = (NVME_CMD_IO_WRITE << 16) | (ns & 0xFFFF);
    cmd.dword[10] = lba & 0xFFFFFFFF;
    cmd.dword[11] = (lba >> 32) & 0xFFFFFFFF;
    cmd.dword[12] = (count - 1) & 0xFFFF;
    cmd.dword[6] = (uint32_t)(uint64_t)buf;
    cmd.dword[7] = (uint32_t)((uint64_t)buf >> 32);

    nvme_submit_cmd(nvme.iosq, &nvme.cmd_tail, &cmd);
    nvme.mmio[0x1000 / 4 + 2 * NVME_IO_QUEUE] = nvme.cmd_tail;

    for (volatile int i = 0; i < 10000000; i++) {
        nvme_cpl_t *cpl = (nvme_cpl_t *)&nvme.iocq[nvme.cpl_head * 4];
        if ((cpl->dword[3] & 1) == 1) {
            uint32_t status = (cpl->dword[3] >> 17) & 0xFF;
            nvme.cpl_head = (nvme.cpl_head + 1) % 1024;
            return (status == 0) ? 0 : -1;
        }
    }
    return -1;
}

uint32_t nvme_get_sector_size(uint32_t ns) {
    return nvme.sector_size ? nvme.sector_size : 512;
}

uint64_t nvme_get_total_sectors(uint32_t ns) {
    return nvme.total_sectors;
}

int nvme_get_device_count(void) {
    return nvme.ready ? 1 : 0;
}
