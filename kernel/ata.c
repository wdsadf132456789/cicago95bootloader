#include "ata.h"
#include "kernel.h"

static ata_info_t ata_devices[4] = {0};  /* Primary master, primary slave, secondary master, secondary slave */

static void ata_wait_ready(uint16_t io_base) {
    for (volatile int i = 0; i < 1000; i++)
        inb(io_base + 7);
}

static void ata_wait_drq(uint16_t io_base) {
    int timeout = 1000000;
    while (!(inb(io_base + 7) & 0x08)) {
        if (timeout-- <= 0) return;
    }
}

static int ata_poll(uint16_t io_base) {
    for (int i = 0; i < 4; i++) inb(io_base + 7);
    int timeout = 100000;
    while (inb(io_base + 7) & 0x80) {
        if (--timeout <= 0) return -1;
    }
    timeout = 100000;
    while (!(inb(io_base + 7) & 0x08)) {
        uint8_t status = inb(io_base + 7);
        if (status & 0x01) return -1;
        if (--timeout <= 0) return -1;
    }
    return 0;
}

static uint16_t ata_get_base(uint8_t drive) {
    if (drive < 2) return ATA_PRIMARY_IO;
    return ATA_SECONDARY_IO;
}

static uint16_t ata_get_ctrl(uint8_t drive) {
    if (drive < 2) return ATA_PRIMARY_CTRL;
    return ATA_SECONDARY_CTRL;
}

static uint8_t ata_get_devno(uint8_t drive) {
    return drive & 1;
}

void ata_init(void) {
    /* Software reset both channels */
    outb(ATA_PRIMARY_CTRL, 0x04);
    outb(ATA_SECONDARY_CTRL, 0x04);
    for (volatile int i = 0; i < 100000; i++);
    outb(ATA_PRIMARY_CTRL, 0x00);
    outb(ATA_SECONDARY_CTRL, 0x00);

    /* Detect all 4 possible drives */
    for (uint8_t d = 0; d < 4; d++) {
        ata_detect_drive(d, &ata_devices[d]);
    }
}

int ata_detect_drive(uint8_t drive, ata_info_t *info) {
    if (!info) return -1;
    __builtin_memset(info, 0, sizeof(ata_info_t));

    uint16_t io_base = ata_get_base(drive);
    uint16_t ctrl_base = ata_get_ctrl(drive);
    uint8_t devno = ata_get_devno(drive);

    /* Select drive */
    outb(io_base + 6, 0xA0 | (devno << 4));
    io_wait();

    /* Reset */
    outb(ctrl_base, 0x04);
    for (volatile int i = 0; i < 10000; i++);
    outb(ctrl_base, 0x00);

    /* Zero sector/feature registers */
    outb(io_base + 1, 0);
    outb(io_base + 2, 0);
    outb(io_base + 3, 0);
    outb(io_base + 4, 0);
    outb(io_base + 5, 0);

    /* Send IDENTIFY */
    outb(io_base + 7, 0xEC);

    /* Check if drive exists */
    uint8_t status = inb(io_base + 7);
    if (status == 0) return -1;  /* No drive */

    /* Wait for BSY to clear */
    int timeout = 1000000;
    while (inb(io_base + 7) & 0x80) {
        if (--timeout <= 0) return -1;
    }

    /* Check for non-ATA device (read back sector count) */
    if (inb(io_base + 2) != 1 || inb(io_base + 3) != 0) {
        return -1;
    }

    /* Poll until DRQ or error */
    if (ata_poll(io_base) < 0) return -1;

    /* Read 256 words of identify data */
    uint16_t id_buf[256];
    for (int i = 0; i < 256; i++) {
        id_buf[i] = inw(io_base);
    }

    info->present = 1;
    info->sector_size = 512;

    /* Model string (words 27-46, 40 chars, byte-swapped) */
    for (int i = 0; i < 20; i++) {
        info->model[i * 2]     = (id_buf[27 + i] >> 8) & 0xFF;
        info->model[i * 2 + 1] = id_buf[27 + i] & 0xFF;
    }
    info->model[40] = '\0';

    /* Trim trailing spaces from model */
    for (int i = 39; i >= 0; i--) {
        if (info->model[i] == ' ' || info->model[i] == '\0')
            info->model[i] = '\0';
        else break;
    }

    /* Max LBA (28-bit) from words 60-61 */
    info->max_lba = (uint32_t)id_buf[60] | ((uint32_t)id_buf[61] << 16);

    /* Check LBA48 support (word 83, bit 10) */
    if (id_buf[83] & (1 << 10)) {
        info->lba48 = 1;
        info->max_lba48 = (uint64_t)id_buf[100] |
                          ((uint64_t)id_buf[101] << 16) |
                          ((uint64_t)id_buf[102] << 32) |
                          ((uint64_t)id_buf[103] << 48);
    } else {
        info->max_lba48 = info->max_lba;
    }

    return 0;
}

int ata_identify(uint8_t drive, uint16_t *buffer) {
    uint16_t io_base = ata_get_base(drive);
    uint8_t devno = ata_get_devno(drive);

    outb(io_base + 6, 0xA0 | (devno << 4));
    io_wait();

    outb(io_base + 1, 0);
    outb(io_base + 2, 0);
    outb(io_base + 3, 0);
    outb(io_base + 4, 0);
    outb(io_base + 5, 0);
    outb(io_base + 7, ATA_CMD_IDENTIFY);

    uint8_t status = inb(io_base + 7);
    if (status == 0) return -1;

    if (ata_poll(io_base) < 0) return -1;

    for (int i = 0; i < 256; i++) {
        buffer[i] = inw(io_base);
    }
    return 0;
}

int ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint16_t *buffer) {
    uint16_t io_base = ata_get_base(drive);
    uint8_t devno = ata_get_devno(drive);
    uint8_t drive_head = ((devno << 4) | 0xE0) | ((lba >> 24) & 0x0F);

    ata_wait_ready(io_base);
    outb(io_base + 1, count);
    outb(io_base + 2, lba & 0xFF);
    outb(io_base + 3, (lba >> 8) & 0xFF);
    outb(io_base + 4, (lba >> 16) & 0xFF);
    outb(io_base + 6, drive_head);
    outb(io_base + 7, ATA_CMD_READ);

    for (int i = 0; i < count; i++) {
        if (ata_poll(io_base) < 0) return -1;
        for (int j = 0; j < 256; j++) {
            buffer[i * 256 + j] = inw(io_base);
        }
    }
    return 0;
}

int ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint16_t *buffer) {
    uint16_t io_base = ata_get_base(drive);
    uint8_t devno = ata_get_devno(drive);
    uint8_t drive_head = ((devno << 4) | 0xE0) | ((lba >> 24) & 0x0F);

    ata_wait_ready(io_base);
    outb(io_base + 1, count);
    outb(io_base + 2, lba & 0xFF);
    outb(io_base + 3, (lba >> 8) & 0xFF);
    outb(io_base + 4, (lba >> 16) & 0xFF);
    outb(io_base + 6, drive_head);
    outb(io_base + 7, ATA_CMD_WRITE);

    for (int i = 0; i < count; i++) {
        ata_wait_ready(io_base);
        ata_wait_drq(io_base);
        for (int j = 0; j < 256; j++) {
            outw(io_base, buffer[i * 256 + j]);
        }
        /* Flush cache */
        outb(io_base + 7, 0xE7);
        ata_wait_ready(io_base);
    }
    return 0;
}

int ata_read_sectors_lba48(uint8_t drive, uint64_t lba, uint16_t count, void *buffer) {
    uint16_t io_base = ata_get_base(drive);
    uint8_t devno = ata_get_devno(drive);

    ata_wait_ready(io_base);

    /* LBA48: feature registers are high/low bytes of count */
    outb(io_base + 1, 0);          /* features high */
    outb(io_base + 2, count & 0xFF);  /* count low */
    outb(io_base + 3, lba & 0xFF);    /* LBA low (bits 0-7) */
    outb(io_base + 4, (lba >> 8) & 0xFF);   /* LBA mid (bits 8-15) */
    outb(io_base + 5, (lba >> 16) & 0xFF);  /* LBA high (bits 16-23) */
    outb(io_base + 6, 0xA0 | (devno << 4));
    outb(io_base + 1, 0);          /* features low */
    outb(io_base + 2, (count >> 8) & 0xFF);  /* count high */
    outb(io_base + 3, (lba >> 24) & 0xFF);  /* LBA low ext (bits 24-31) */
    outb(io_base + 4, (lba >> 32) & 0xFF);  /* LBA mid ext (bits 32-39) */
    outb(io_base + 5, (lba >> 40) & 0xFF);  /* LBA high ext (bits 40-47) */
    outb(io_base + 7, ATA_CMD_READ_EXT);

    uint16_t *buf = (uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        if (ata_poll(io_base) < 0) return -1;
        for (int j = 0; j < 256; j++) {
            buf[i * 256 + j] = inw(io_base);
        }
    }
    return 0;
}

int ata_write_sectors_lba48(uint8_t drive, uint64_t lba, uint16_t count, const void *buffer) {
    uint16_t io_base = ata_get_base(drive);
    uint8_t devno = ata_get_devno(drive);

    ata_wait_ready(io_base);

    outb(io_base + 1, 0);
    outb(io_base + 2, count & 0xFF);
    outb(io_base + 3, lba & 0xFF);
    outb(io_base + 4, (lba >> 8) & 0xFF);
    outb(io_base + 5, (lba >> 16) & 0xFF);
    outb(io_base + 6, 0xA0 | (devno << 4));
    outb(io_base + 1, 0);
    outb(io_base + 2, (count >> 8) & 0xFF);
    outb(io_base + 3, (lba >> 24) & 0xFF);
    outb(io_base + 4, (lba >> 32) & 0xFF);
    outb(io_base + 5, (lba >> 40) & 0xFF);
    outb(io_base + 7, ATA_CMD_WRITE_EXT);

    const uint16_t *buf = (const uint16_t *)buffer;
    for (int i = 0; i < count; i++) {
        ata_wait_ready(io_base);
        ata_wait_drq(io_base);
        for (int j = 0; j < 256; j++) {
            outw(io_base, buf[i * 256 + j]);
        }
    }
    outb(io_base + 7, 0xE7);
    ata_wait_ready(io_base);
    return 0;
}

int ata_get_detected_count(void) {
    int count = 0;
    for (int i = 0; i < 4; i++) {
        if (ata_devices[i].present) count++;
    }
    return count;
}

ata_info_t *ata_get_info(uint8_t drive) {
    if (drive >= 4) return 0;
    if (!ata_devices[drive].present) return 0;
    return &ata_devices[drive];
}
