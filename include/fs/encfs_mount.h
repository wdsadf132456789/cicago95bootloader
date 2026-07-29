/**
 * Chicago-95 Encrypted BrainFS Mount
 */

#ifndef ENCFS_MOUNT_H
#define ENCFS_MOUNT_H

#include <stdint.h>

/* Initialize encryption keys and state */
int encfs_init(void);

/* Mount encrypted BrainFS on drive with given FAT width */
int encfs_mount(uint8_t drive, uint8_t fat_width);

/* Read sectors with decryption */
int encfs_read_sectors(uint8_t drive, uint32_t lba, uint32_t count, uint8_t *buf);

/* Write sectors with encryption */
int encfs_write_sectors(uint8_t drive, uint32_t lba, uint32_t count, const uint8_t *buf);

/* Status */
uint8_t encfs_is_mounted(void);
const uint8_t *encfs_get_volume_key(void);

#endif
