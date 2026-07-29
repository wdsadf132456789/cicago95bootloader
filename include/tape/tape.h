/**
 * Chicago-95 Tape/Disk Driver Interface
 * ATA/ATAPI (PIO mode) and Floppy disk support for boot-time I/O
 */

#ifndef CHICAGO_TAPE_H
#define CHICAGO_TAPE_H

#include <stdint.h>

/* ATA I/O ports */
#define ATA_PRIMARY_BASE        0x1F0
#define ATA_PRIMARY_CTRL        0x3F6
#define ATA_SECONDARY_BASE      0x170
#define ATA_SECONDARY_CTRL      0x376

/* ATA registers (offset from base) */
#define ATA_REG_DATA            0
#define ATA_REG_ERROR           1
#define ATA_REG_FEATURES        1
#define ATA_REG_SECCOUNT        2
#define ATA_REG_LBA_LO          3
#define ATA_REG_LBA_MID         4
#define ATA_REG_LBA_HI          5
#define ATA_REG_DRIVE_HEAD      6
#define ATA_REG_STATUS          7
#define ATA_REG_COMMAND         7

/* ATA status bits */
#define ATA_SR_BSY              0x80
#define ATA_SR_DRDY             0x40
#define ATA_SR_DRQ              0x08
#define ATA_SR_ERR              0x01

/* ATA commands */
#define ATA_CMD_IDENTIFY        0xEC
#define ATA_CMD_IDENTIFY_PACKET 0xA1
#define ATA_CMD_READ_PIO        0x20
#define ATA_CMD_WRITE_PIO       0x30
#define ATA_CMD_READ_DMA        0xC8
#define ATA_CMD_WRITE_DMA       0xCA
#define ATA_CMD_PACKET          0xA0
#define ATA_CMD_CACHE_FLUSH     0xE7

/* ATAPI commands */
#define ATAPI_CMD_READ_12       0xA8
#define ATAPI_CMD_READ_CAPACITY 0x25

/* Floppy I/O ports */
#define FLOPPY_BASE             0x3F0
#define FLOPPY_DOR              (FLOPPY_BASE + 2)
#define FLOPPY_MSR              (FLOPPY_BASE + 4)
#define FLOPPY_FIFO             (FLOPPY_BASE + 5)

/* Floppy commands */
#define FLOPPY_CMD_READ          0xE6
#define FLOPPY_CMD_WRITE         0xC5
#define FLOPPY_CMD_SEEK          0x0F
#define FLOPPY_CMD_RECALIBRATE  0x07
#define FLOPPY_CMD_SPECIFY      0x03
#define FLOPPY_CMD_SENSE_INT    0x04
#define FLOPPY_CMD_CONFIGURE    0x13

/* Drive types */
#define DRIVE_ATA_PRIMARY       0
#define DRIVE_ATA_SECONDARY     1
#define DRIVE_FLOPPY_0          2
#define DRIVE_FLOPPY_1          3

/* Drive info */
typedef struct {
    uint8_t  present;
    uint8_t  is_atapi;
    uint8_t  is_master;
    uint8_t  drive_type;        /* DRIVE_ATA_* or DRIVE_FLOPPY_* */
    uint16_t base_port;
    uint16_t ctrl_port;
    uint32_t sectors_lba28;
    uint64_t sectors_lba48;
    uint16_t sector_size;
    char     model[41];
    char     serial[21];
    uint8_t  dma_support;
    uint8_t  lba48_support;
    uint8_t  removable;
    uint8_t  media_type;        /* 0=fixed, 1=removable, 2=CD-ROM */
} tape_drive_t;

/* DMA PRD (Physical Region Descriptor) for ATA DMA */
typedef struct {
    uint32_t phys_addr;
    uint16_t byte_count;
    uint16_t end_of_table;
} __attribute__((packed)) tape_prd_t;

/* ========================================================================
 * ATA/ATAPI functions
 * ======================================================================== */
int      tape_ata_init(void);
int      tape_ata_detect(uint8_t port, uint8_t slave, tape_drive_t *drive);
int      tape_ata_read_sectors(tape_drive_t *drive, uint32_t lba, uint32_t count, void *buf);
int      tape_ata_write_sectors(tape_drive_t *drive, uint32_t lba, uint32_t count, const void *buf);
int      tape_ata_read_sectors_lba48(tape_drive_t *drive, uint64_t lba, uint32_t count, void *buf);
int      tape_ata_write_sectors_lba48(tape_drive_t *drive, uint64_t lba, uint32_t count, const void *buf);
int      tape_ata_identify(tape_drive_t *drive, uint16_t *out);
int      tape_ata_packet(tape_drive_t *drive, const uint16_t *cmd, uint32_t cmd_len,
                         void *buf, uint32_t buf_len, int read);

/* ATAPI specific */
int      tape_atapi_read(tape_drive_t *drive, uint32_t lba, uint32_t count, void *buf, uint32_t buf_len);

/* ========================================================================
 * Floppy functions
 * ======================================================================== */
int      tape_floppy_init(uint8_t drive_num);
int      tape_floppy_read_sector(uint8_t drive_num, uint8_t cylinder, uint8_t head,
                                 uint8_t sector, void *buf);
int      tape_floppy_write_sector(uint8_t drive_num, uint8_t cylinder, uint8_t head,
                                  uint8_t sector, const void *buf);
int      tape_floppy_seek(uint8_t drive_num, uint8_t cylinder);
int      tape_floppy_recalibrate(uint8_t drive_num);
void     tape_floppy_motor_on(uint8_t drive_num);
void     tape_floppy_motor_off(uint8_t drive_num);

/* ========================================================================
 * Unified disk API
 * ======================================================================== */
int      tape_read(uint8_t drive_type, uint8_t drive_num, uint64_t lba,
                   uint32_t count, void *buf);
int      tape_write(uint8_t drive_type, uint8_t drive_num, uint64_t lba,
                    uint32_t count, const void *buf);
int      tape_get_drive_count(void);
tape_drive_t *tape_get_drive(uint8_t index);

#endif /* CHICAGO_TAPE_H */
