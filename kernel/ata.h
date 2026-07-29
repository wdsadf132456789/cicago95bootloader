#ifndef ATA_H
#define ATA_H

#include <stdint.h>

#define ATA_PRIMARY_IO    0x1F0
#define ATA_PRIMARY_CTRL  0x3F6
#define ATA_SECONDARY_IO  0x170
#define ATA_SECONDARY_CTRL 0x376

#define ATA_CMD_READ   0x20
#define ATA_CMD_WRITE  0x30
#define ATA_CMD_IDENTIFY 0xEC

/* LBA48 commands */
#define ATA_CMD_READ_EXT   0x24
#define ATA_CMD_WRITE_EXT  0x34

#define ATA_DRIVE_MASTER 0
#define ATA_DRIVE_SLAVE  1

typedef struct {
    uint8_t  present;
    uint8_t  lba48;       /* 1 if supports LBA48 */
    uint32_t max_lba;     /* Max LBA (28-bit) */
    uint64_t max_lba48;   /* Max LBA (48-bit) */
    uint32_t sector_size; /* Usually 512 */
    char     model[41];
} ata_info_t;

void ata_init(void);
int  ata_identify(uint8_t drive, uint16_t *buffer);
int  ata_read_sectors(uint8_t drive, uint32_t lba, uint8_t count, uint16_t *buffer);
int  ata_write_sectors(uint8_t drive, uint32_t lba, uint8_t count, const uint16_t *buffer);
int  ata_read_sectors_lba48(uint8_t drive, uint64_t lba, uint16_t count, void *buffer);
int  ata_write_sectors_lba48(uint8_t drive, uint64_t lba, uint16_t count, const void *buffer);
int  ata_detect_drive(uint8_t drive, ata_info_t *info);
int  ata_get_detected_count(void);
ata_info_t *ata_get_info(uint8_t drive);

#endif
