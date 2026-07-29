/**
 * Chicago-95 ATA/ATAPI Disk Driver
 * PIO-mode ATA disk read/write + ATAPI CD-ROM support
 * Supports both Primary and Secondary controllers
 */

#include "tape/tape.h"
#include "boot/security.h"

/* I/O port accessors */
static inline void ata_outb(uint16_t port, uint8_t val) {
    asm volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint8_t ata_inb(uint16_t port) {
    uint8_t val;
    asm volatile("inb %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}
static inline void ata_outw(uint16_t port, uint16_t val) {
    asm volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}
static inline uint16_t ata_inw(uint16_t port) {
    uint16_t val;
    asm volatile("inw %1, %0" : "=a"(val) : "Nd"(port));
    return val;
}

#define ATA_TIMEOUT     1000000
#define ATA_SECTOR_SIZE 512

/* Drive table */
static tape_drive_t g_drives[4] = {0};
static int g_drive_count = 0;

/* ---- Wait for BSY clear ---- */

static int ata_wait_bsy(uint16_t base) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        if (!(ata_inb(base + ATA_REG_STATUS) & ATA_SR_BSY))
            return 0;
    }
    return -1;
}

/* ---- Wait for DRQ ---- */

static int ata_wait_drq(uint16_t base) {
    for (int i = 0; i < ATA_TIMEOUT; i++) {
        uint8_t status = ata_inb(base + ATA_REG_STATUS);
        if (status & ATA_SR_ERR) return -2;
        if (status & ATA_SR_DRQ) return 0;
    }
    return -1;
}

/* ---- Software reset ---- */

static void ata_soft_reset(uint16_t ctrl) {
    ata_outb(ctrl, 0x04); /* Set SRST */
    for (int i = 0; i < 1000; i++) ata_inb(ctrl); /* Wait */
    ata_outb(ctrl, 0x00); /* Clear SRST */
    for (int i = 0; i < 1000; i++) ata_inb(ctrl);
}

/* ---- Detect drives on a controller ---- */

static int ata_detect(uint16_t base, uint16_t ctrl, uint8_t is_master) {
    uint8_t slave = is_master ? 0 : 1;

    /* Select drive */
    ata_outb(base + ATA_REG_DRIVE_HEAD, 0xA0 | (slave << 4));
    for (int i = 0; i < 1000; i++) ata_inb(base + ATA_REG_STATUS);

    /* Send IDENTIFY */
    ata_outb(base + ATA_REG_SECCOUNT, 0);
    ata_outb(base + ATA_REG_LBA_LO, 0);
    ata_outb(base + ATA_REG_LBA_MID, 0);
    ata_outb(base + ATA_REG_LBA_HI, 0);
    ata_outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);

    for (int i = 0; i < 1000; i++) ata_inb(base + ATA_REG_STATUS);

    uint8_t status = ata_inb(base + ATA_REG_STATUS);
    if (status == 0) return 0; /* No drive */

    if (ata_wait_bsy(base) < 0) return 0;

    uint8_t lba_mid = ata_inb(base + ATA_REG_LBA_MID);
    uint8_t lba_hi  = ata_inb(base + ATA_REG_LBA_HI);

    /* Check if ATAPI */
    int is_atapi = (lba_mid == 0x14 && lba_hi == 0xEB);

    if (ata_wait_drq(base) < 0) return 0;

    /* Read 256 words of identify data */
    uint16_t ident[256];
    for (int i = 0; i < 256; i++)
        ident[i] = ata_inw(base + ATA_REG_DATA);

    tape_drive_t *d = &g_drives[g_drive_count];
    d->present = 1;
    d->is_atapi = is_atapi;
    d->is_master = is_master;
    d->base_port = base;
    d->ctrl_port = ctrl;
    d->sector_size = ATA_SECTOR_SIZE;

    if (!is_atapi) {
        /* ATA drive - extract model, serial, capacity */
        d->lba48_support = (ident[83] >> 10) & 1;
        d->dma_support = (ident[49] >> 8) & 1;
        d->removable = (ident[0] >> 7) & 1;

        if (d->lba48_support) {
            d->sectors_lba48 = ((uint64_t)ident[100] << 32) |
                               ((uint64_t)ident[101] << 48) |
                               ((uint64_t)ident[102] << 16) |
                               ident[103];
        }
        d->sectors_lba28 = ((uint32_t)ident[60] << 16) | ident[61];

        /* Model string (20 words at offset 27) */
        for (int i = 0; i < 20; i++) {
            d->model[i * 2]     = (ident[27 + i] >> 8) & 0xFF;
            d->model[i * 2 + 1] = ident[27 + i] & 0xFF;
        }
        d->model[40] = '\0';

        /* Serial string (10 words at offset 10) */
        for (int i = 0; i < 10; i++) {
            d->serial[i * 2]     = (ident[10 + i] >> 8) & 0xFF;
            d->serial[i * 2 + 1] = ident[10 + i] & 0xFF;
        }
        d->serial[20] = '\0';

        d->drive_type = (base == ATA_PRIMARY_BASE) ? DRIVE_ATA_PRIMARY : DRIVE_ATA_SECONDARY;
    } else {
        d->drive_type = (base == ATA_PRIMARY_BASE) ? DRIVE_ATA_PRIMARY : DRIVE_ATA_SECONDARY;
        d->model[0] = 'A';
        d->model[1] = 'T';
        d->model[2] = 'A';
        d->model[3] = 'P';
        d->model[4] = 'I';
        d->model[5] = '\0';
    }

    g_drive_count++;
    return 1;
}

/* ---- Init ---- */

int tape_ata_init(void) {
    g_drive_count = 0;

    /* Software reset both controllers */
    ata_soft_reset(ATA_PRIMARY_CTRL);
    ata_soft_reset(ATA_SECONDARY_CTRL);

    /* Detect Primary Master + Slave */
    ata_detect(ATA_PRIMARY_BASE, ATA_PRIMARY_CTRL, 1);
    ata_detect(ATA_PRIMARY_BASE, ATA_PRIMARY_CTRL, 0);

    /* Detect Secondary Master + Slave */
    ata_detect(ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 1);
    ata_detect(ATA_SECONDARY_BASE, ATA_SECONDARY_CTRL, 0);

    return g_drive_count;
}

/* ---- Identify ---- */

int tape_ata_identify(tape_drive_t *drive, uint16_t *out) {
    if (!drive->present) return -1;

    uint16_t base = drive->base_port;

    ata_outb(base + ATA_REG_COMMAND, ATA_CMD_IDENTIFY);
    if (ata_wait_bsy(base) < 0) return -1;
    if (ata_wait_drq(base) < 0) return -1;

    for (int i = 0; i < 256; i++)
        out[i] = ata_inw(base + ATA_REG_DATA);

    return 0;
}

/* ---- Read sectors (LBA28) ---- */

int tape_ata_read_sectors(tape_drive_t *drive, uint32_t lba, uint32_t count, void *buf) {
    if (!drive->present) return -1;

    uint16_t base = drive->base_port;
    uint8_t *ptr = (uint8_t *)buf;

    for (uint32_t s = 0; s < count; s++) {
        uint32_t l = lba + s;

        if (ata_wait_bsy(base) < 0) return -1;

        ata_outb(base + ATA_REG_DRIVE_HEAD, 0xE0 | ((drive->is_master ? 0 : 1) << 4) | ((l >> 24) & 0x0F));
        ata_outb(base + ATA_REG_SECCOUNT, 1);
        ata_outb(base + ATA_REG_LBA_LO, l & 0xFF);
        ata_outb(base + ATA_REG_LBA_MID, (l >> 8) & 0xFF);
        ata_outb(base + ATA_REG_LBA_HI, (l >> 16) & 0xFF);
        ata_outb(base + ATA_REG_COMMAND, ATA_CMD_READ_PIO);

        if (ata_wait_drq(base) < 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t word = ata_inw(base + ATA_REG_DATA);
            ptr[s * ATA_SECTOR_SIZE + i * 2]     = word & 0xFF;
            ptr[s * ATA_SECTOR_SIZE + i * 2 + 1] = (word >> 8) & 0xFF;
        }
    }
    return 0;
}

/* ---- Write sectors (LBA28) ---- */

int tape_ata_write_sectors(tape_drive_t *drive, uint32_t lba, uint32_t count, const void *buf) {
    if (!drive->present) return -1;

    uint16_t base = drive->base_port;
    const uint8_t *ptr = (const uint8_t *)buf;

    for (uint32_t s = 0; s < count; s++) {
        uint32_t l = lba + s;

        if (ata_wait_bsy(base) < 0) return -1;

        ata_outb(base + ATA_REG_DRIVE_HEAD, 0xE0 | ((drive->is_master ? 0 : 1) << 4) | ((l >> 24) & 0x0F));
        ata_outb(base + ATA_REG_SECCOUNT, 1);
        ata_outb(base + ATA_REG_LBA_LO, l & 0xFF);
        ata_outb(base + ATA_REG_LBA_MID, (l >> 8) & 0xFF);
        ata_outb(base + ATA_REG_LBA_HI, (l >> 16) & 0xFF);
        ata_outb(base + ATA_REG_COMMAND, ATA_CMD_WRITE_PIO);

        if (ata_wait_drq(base) < 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t word = ptr[s * ATA_SECTOR_SIZE + i * 2] |
                           ((uint16_t)ptr[s * ATA_SECTOR_SIZE + i * 2 + 1] << 8);
            ata_outw(base + ATA_REG_DATA, word);
        }

        /* Flush */
        ata_outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_wait_bsy(base);
    }
    return 0;
}

/* ---- Read sectors (LBA48) ---- */

int tape_ata_read_sectors_lba48(tape_drive_t *drive, uint64_t lba, uint32_t count, void *buf) {
    if (!drive->present || !drive->lba48_support) return -1;

    uint16_t base = drive->base_port;
    uint8_t *ptr = (uint8_t *)buf;

    for (uint32_t s = 0; s < count; s++) {
        uint64_t l = lba + s;

        if (ata_wait_bsy(base) < 0) return -1;

        /* LBA48 uses two sequential writes for high/low */
        ata_outb(base + ATA_REG_DRIVE_HEAD, 0x40 | ((drive->is_master ? 0 : 1) << 4));
        ata_outb(base + ATA_REG_SECCOUNT, (l >> 24) & 0xFF); /* High */
        ata_outb(base + ATA_REG_LBA_LO, (l >> 32) & 0xFF);
        ata_outb(base + ATA_REG_LBA_MID, (l >> 40) & 0xFF);
        ata_outb(base + ATA_REG_LBA_HI, (l >> 48) & 0xFF);
        ata_outb(base + ATA_REG_SECCOUNT, 1);                /* Low */
        ata_outb(base + ATA_REG_LBA_LO, l & 0xFF);
        ata_outb(base + ATA_REG_LBA_MID, (l >> 8) & 0xFF);
        ata_outb(base + ATA_REG_LBA_HI, (l >> 16) & 0xFF);
        ata_outb(base + ATA_REG_COMMAND, 0x24); /* READ SECTORS EXT */

        if (ata_wait_drq(base) < 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t word = ata_inw(base + ATA_REG_DATA);
            ptr[s * ATA_SECTOR_SIZE + i * 2]     = word & 0xFF;
            ptr[s * ATA_SECTOR_SIZE + i * 2 + 1] = (word >> 8) & 0xFF;
        }
    }
    return 0;
}

/* ---- Write sectors (LBA48) ---- */

int tape_ata_write_sectors_lba48(tape_drive_t *drive, uint64_t lba, uint32_t count, const void *buf) {
    if (!drive->present || !drive->lba48_support) return -1;

    uint16_t base = drive->base_port;
    const uint8_t *ptr = (const uint8_t *)buf;

    for (uint32_t s = 0; s < count; s++) {
        uint64_t l = lba + s;

        if (ata_wait_bsy(base) < 0) return -1;

        ata_outb(base + ATA_REG_DRIVE_HEAD, 0x40 | ((drive->is_master ? 0 : 1) << 4));
        ata_outb(base + ATA_REG_SECCOUNT, (l >> 24) & 0xFF);
        ata_outb(base + ATA_REG_LBA_LO, (l >> 32) & 0xFF);
        ata_outb(base + ATA_REG_LBA_MID, (l >> 40) & 0xFF);
        ata_outb(base + ATA_REG_LBA_HI, (l >> 48) & 0xFF);
        ata_outb(base + ATA_REG_SECCOUNT, 1);
        ata_outb(base + ATA_REG_LBA_LO, l & 0xFF);
        ata_outb(base + ATA_REG_LBA_MID, (l >> 8) & 0xFF);
        ata_outb(base + ATA_REG_LBA_HI, (l >> 16) & 0xFF);
        ata_outb(base + ATA_REG_COMMAND, 0x34); /* WRITE SECTORS EXT */

        if (ata_wait_drq(base) < 0) return -1;

        for (int i = 0; i < 256; i++) {
            uint16_t word = ptr[s * ATA_SECTOR_SIZE + i * 2] |
                           ((uint16_t)ptr[s * ATA_SECTOR_SIZE + i * 2 + 1] << 8);
            ata_outw(base + ATA_REG_DATA, word);
        }

        ata_outb(base + ATA_REG_COMMAND, ATA_CMD_CACHE_FLUSH);
        ata_wait_bsy(base);
    }
    return 0;
}

/* ---- ATAPI read ---- */

int tape_atapi_read(tape_drive_t *drive, uint32_t lba, uint32_t count, void *buf, uint32_t buf_len) {
    if (!drive->present || !drive->is_atapi) return -1;

    uint16_t base = drive->base_port;
    uint8_t *ptr = (uint8_t *)buf;

    /* Select drive */
    ata_outb(base + ATA_REG_DRIVE_HEAD, 0xA0 | ((drive->is_master ? 0 : 1) << 4));
    for (int i = 0; i < 1000; i++) ata_inb(base + ATA_REG_STATUS);

    /* PACKET command */
    ata_outb(base + ATA_REG_COMMAND, ATA_CMD_PACKET);
    if (ata_wait_drq(base) < 0) return -1;

    /* ATAPI packet (12 bytes) */
    uint16_t packet[6] = {0};
    packet[0] = 0xA8;              /* READ(12) */
    packet[1] = 0;
    packet[2] = (lba >> 24) & 0xFF;
    packet[3] = (lba >> 16) & 0xFF;
    packet[4] = (lba >> 8) & 0xFF;
    packet[5] = lba & 0xFF;
    packet[6] = (count >> 24) & 0xFF;
    packet[7] = (count >> 16) & 0xFF;
    packet[8] = (count >> 8) & 0xFF;
    packet[9] = count & 0xFF;

    for (int i = 0; i < 6; i++)
        ata_outw(base + ATA_REG_DATA, packet[i]);

    /* Read data */
    uint32_t bytes_read = 0;
    uint32_t bytes_wanted = count * ATA_SECTOR_SIZE;
    if (bytes_wanted > buf_len) bytes_wanted = buf_len;

    while (bytes_read < bytes_wanted) {
        if (ata_wait_drq(base) < 0) return -1;

        uint16_t bytes_this = ata_inw(base + ATA_REG_DATA);
        uint16_t words_this = bytes_this / 2;

        for (int i = 0; i < words_this && (bytes_read + i * 2 + 1) < bytes_wanted; i++) {
            uint16_t word = ata_inw(base + ATA_REG_DATA);
            ptr[bytes_read + i * 2]     = word & 0xFF;
            ptr[bytes_read + i * 2 + 1] = (word >> 8) & 0xFF;
        }
        bytes_read += bytes_this;
    }

    return 0;
}

/* ---- ATA packet (generic) ---- */

int tape_ata_packet(tape_drive_t *drive, const uint16_t *cmd, uint32_t cmd_len,
                    void *buf, uint32_t buf_len, int read) {
    if (!drive->present || !drive->is_atapi) return -1;

    uint16_t base = drive->base_port;

    ata_outb(base + ATA_REG_DRIVE_HEAD, 0xA0 | ((drive->is_master ? 0 : 1) << 4));
    for (int i = 0; i < 1000; i++) ata_inb(base + ATA_REG_STATUS);

    ata_outb(base + ATA_REG_COMMAND, ATA_CMD_PACKET);
    if (ata_wait_drq(base) < 0) return -1;

    uint32_t words_to_send = (cmd_len + 1) / 2;
    for (uint32_t i = 0; i < words_to_send; i++)
        ata_outw(base + ATA_REG_DATA, cmd[i]);

    if (read && buf) {
        uint8_t *ptr = (uint8_t *)buf;
        uint32_t read = 0;
        while (read < buf_len) {
            if (ata_wait_drq(base) < 0) break;
            uint16_t word = ata_inw(base + ATA_REG_DATA);
            if (read + 1 < buf_len) ptr[read] = word & 0xFF;
            if (read + 2 < buf_len) ptr[read + 1] = (word >> 8) & 0xFF;
            read += 2;
        }
    }

    return 0;
}

/* ---- Unified API ---- */

int tape_read(uint8_t drive_type, uint8_t drive_num, uint64_t lba, uint32_t count, void *buf) {
    /* Find drive */
    tape_drive_t *d = (void *)0;
    for (int i = 0; i < g_drive_count; i++) {
        if (g_drives[i].drive_type == drive_type &&
            ((drive_num == 0 && g_drives[i].is_master) ||
             (drive_num == 1 && !g_drives[i].is_master))) {
            d = &g_drives[i];
            break;
        }
    }
    if (!d || !d->present) return -1;

    if (d->is_atapi)
        return tape_atapi_read(d, lba, count, buf, count * ATA_SECTOR_SIZE);
    else if (d->lba48_support)
        return tape_ata_read_sectors_lba48(d, lba, count, buf);
    else
        return tape_ata_read_sectors(d, (uint32_t)lba, count, buf);
}

int tape_write(uint8_t drive_type, uint8_t drive_num, uint64_t lba, uint32_t count, const void *buf) {
    tape_drive_t *d = (void *)0;
    for (int i = 0; i < g_drive_count; i++) {
        if (g_drives[i].drive_type == drive_type &&
            ((drive_num == 0 && g_drives[i].is_master) ||
             (drive_num == 1 && !g_drives[i].is_master))) {
            d = &g_drives[i];
            break;
        }
    }
    if (!d || !d->present || d->is_atapi) return -1;

    if (d->lba48_support)
        return tape_ata_write_sectors_lba48(d, lba, count, buf);
    else
        return tape_ata_write_sectors(d, (uint32_t)lba, count, buf);
}

int tape_get_drive_count(void) { return g_drive_count; }

tape_drive_t *tape_get_drive(uint8_t index) {
    if (index < g_drive_count) return &g_drives[index];
    return (void *)0;
}
