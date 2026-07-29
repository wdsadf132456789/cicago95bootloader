/**
 * Chicago-95 BrainFS - FAT Table Operations
 * Low-level read/write for FAT widths 1 through 128 bits
 *
 * Each FAT width has its own get/set functions that pack entries
 * into the sector buffer at the correct bit offset.
 */

#include "fs/brainfs.h"

/* ========================================================================
 * FAT1 - 1 bit per cluster (bitmap allocation only)
 * Cluster states: 0=free, 1=allocated
 * ======================================================================== */

uint64_t brainfs_fat1_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster / 8;
    uint32_t bit_idx  = cluster % 8;
    return (sector[byte_idx] >> bit_idx) & 1;
}

void brainfs_fat1_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster / 8;
    uint32_t bit_idx  = cluster % 8;
    if (value & 1)
        sector[byte_idx] |= (1 << bit_idx);
    else
        sector[byte_idx] &= ~(1 << bit_idx);
}

/* ========================================================================
 * FAT2 - 2 bits per cluster
 * Cluster states: 0=free, 2=bad, 3=eoc (end of chain)
 * ======================================================================== */

uint64_t brainfs_fat2_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster / 4;
    uint32_t bit_idx  = (cluster % 4) * 2;
    return (sector[byte_idx] >> bit_idx) & 3;
}

void brainfs_fat2_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster / 4;
    uint32_t bit_idx  = (cluster % 4) * 2;
    sector[byte_idx] = (sector[byte_idx] & ~(3 << bit_idx)) | ((value & 3) << bit_idx);
}

/* ========================================================================
 * FAT4 - 4 bits per cluster (nibble)
 * Cluster states: 0-13=next, 14=bad, 15=eoc
 * ======================================================================== */

uint64_t brainfs_fat4_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster / 2;
    uint32_t nibble   = cluster % 2;
    if (nibble == 0)
        return sector[byte_idx] & 0x0F;
    else
        return (sector[byte_idx] >> 4) & 0x0F;
}

void brainfs_fat4_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster / 2;
    uint32_t nibble   = cluster % 2;
    if (nibble == 0)
        sector[byte_idx] = (sector[byte_idx] & 0xF0) | (value & 0x0F);
    else
        sector[byte_idx] = (sector[byte_idx] & 0x0F) | ((value & 0x0F) << 4);
}

/* ========================================================================
 * FAT8 - 8 bits per cluster (1 byte)
 * 0x00=free, 0xFF=eoc, 0xFE=bad (for non-standard)
 * ======================================================================== */

uint64_t brainfs_fat8_get(const uint8_t *sector, uint32_t cluster) {
    return sector[cluster];
}

void brainfs_fat8_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    sector[cluster] = (uint8_t)(value & 0xFF);
}

/* ========================================================================
 * FAT12 - 12 bits per cluster (1.5 bytes, little-endian packed)
 * 0x000=free, 0xFF8+=eoc, 0xFF7=bad
 * ======================================================================== */

uint64_t brainfs_fat12_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster + (cluster / 2); /* cluster * 1.5 */
    uint16_t val;

    if (cluster & 1) {
        /* Odd cluster: high nibble of first byte + full second byte */
        val = ((uint16_t)(sector[byte_idx] >> 4)) |
              ((uint16_t)(sector[byte_idx + 1]) << 4);
    } else {
        /* Even cluster: full first byte + low nibble of second byte */
        val = ((uint16_t)(sector[byte_idx])) |
              ((uint16_t)(sector[byte_idx + 1] & 0x0F) << 8);
    }

    return val & 0xFFF;
}

void brainfs_fat12_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster + (cluster / 2);
    uint16_t val = (uint16_t)(value & 0xFFF);

    if (cluster & 1) {
        sector[byte_idx]     = (sector[byte_idx] & 0x0F) | ((val & 0x0F) << 4);
        sector[byte_idx + 1] = (val >> 4) & 0xFF;
    } else {
        sector[byte_idx]     = val & 0xFF;
        sector[byte_idx + 1] = (sector[byte_idx + 1] & 0xF0) | ((val >> 8) & 0x0F);
    }
}

/* ========================================================================
 * FAT16 - 16 bits per cluster (2 bytes, little-endian)
 * 0x0000=free, 0xFFF8+=eoc, 0xFFF7=bad
 * ======================================================================== */

uint64_t brainfs_fat16_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster * 2;
    return (uint64_t)(sector[byte_idx] | (sector[byte_idx + 1] << 8));
}

void brainfs_fat16_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster * 2;
    sector[byte_idx]     = value & 0xFF;
    sector[byte_idx + 1] = (value >> 8) & 0xFF;
}

/* ========================================================================
 * FAT32 - 32 bits per cluster (4 bytes, little-endian)
 * 0x00000000=free, 0x0FFFFFF8+=eoc, 0x0FFFFFF7=bad
 * ======================================================================== */

uint64_t brainfs_fat32_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster * 4;
    uint32_t val = (uint32_t)(sector[byte_idx] |
                              (sector[byte_idx + 1] << 8) |
                              (sector[byte_idx + 2] << 16) |
                              (sector[byte_idx + 3] << 24));
    return val & 0x0FFFFFFF; /* Only 28 bits used */
}

void brainfs_fat32_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster * 4;
    uint32_t v = (uint32_t)(value & 0x0FFFFFFF);
    sector[byte_idx]     = v & 0xFF;
    sector[byte_idx + 1] = (v >> 8) & 0xFF;
    sector[byte_idx + 2] = (v >> 16) & 0xFF;
    sector[byte_idx + 3] = (v >> 24) & 0xFF;
}

/* ========================================================================
 * FAT64 - 64 bits per cluster (8 bytes, little-endian)
 * Full 64-bit cluster addressing
 * ======================================================================== */

uint64_t brainfs_fat64_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster * 8;
    uint64_t val = 0;
    for (uint32_t i = 0; i < 8; i++)
        val |= ((uint64_t)sector[byte_idx + i]) << (i * 8);
    return val;
}

void brainfs_fat64_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster * 8;
    for (uint32_t i = 0; i < 8; i++)
        sector[byte_idx + i] = (value >> (i * 8)) & 0xFF;
}

/* ========================================================================
 * FAT128 - 128 bits per cluster (16 bytes, little-endian)
 * For massive address spaces; stored as two 64-bit values
 * ======================================================================== */

uint64_t brainfs_fat128_get(const uint8_t *sector, uint32_t cluster) {
    uint32_t byte_idx = cluster * 16;
    /* Return low 64 bits (high 64 bits for cluster chaining extension) */
    uint64_t val = 0;
    for (uint32_t i = 0; i < 8; i++)
        val |= ((uint64_t)sector[byte_idx + i]) << (i * 8);
    return val;
}

void brainfs_fat128_set(uint8_t *sector, uint32_t cluster, uint64_t value) {
    uint32_t byte_idx = cluster * 16;
    for (uint32_t i = 0; i < 8; i++)
        sector[byte_idx + i] = (value >> (i * 8)) & 0xFF;
    /* High 8 bytes: store epoch/timestamp for extended metadata */
    for (uint32_t i = 8; i < 16; i++)
        sector[byte_idx + i] = 0;
}

/* ========================================================================
 * Width-dispatched FAT operations
 * ======================================================================== */

/* Get the number of entries per 512-byte sector for a given width */
uint32_t brainfs_fat_entries_per_sector(uint8_t width) {
    switch (width) {
        case 1:   return 512 * 8;       /* 4096 entries/sector */
        case 2:   return 512 * 4;       /* 2048 entries/sector */
        case 4:   return 512 * 2;       /* 1024 entries/sector */
        case 8:   return 512;           /* 512 entries/sector */
        case 12:  return 512 * 2 / 3;   /* 341 entries/sector */
        case 16:  return 512 / 2;       /* 256 entries/sector */
        case 32:  return 512 / 4;       /* 128 entries/sector */
        case 64:  return 512 / 8;       /* 64 entries/sector */
        case 128: return 512 / 16;      /* 32 entries/sector */
        default:  return 256;
    }
}

/* Get number of sectors needed for the FAT table */
uint32_t brainfs_fat_sectors_needed(uint8_t width, uint32_t total_clusters) {
    uint32_t entries_per_sector = brainfs_fat_entries_per_sector(width);
    return (total_clusters + entries_per_sector - 1) / entries_per_sector;
}

/* Dispatched get: reads a FAT entry from a raw sector buffer */
uint64_t brainfs_fat_get_entry(uint8_t width, const uint8_t *sector, uint32_t cluster) {
    switch (width) {
        case 1:   return brainfs_fat1_get(sector, cluster);
        case 2:   return brainfs_fat2_get(sector, cluster);
        case 4:   return brainfs_fat4_get(sector, cluster);
        case 8:   return brainfs_fat8_get(sector, cluster);
        case 12:  return brainfs_fat12_get(sector, cluster);
        case 16:  return brainfs_fat16_get(sector, cluster);
        case 32:  return brainfs_fat32_get(sector, cluster);
        case 64:  return brainfs_fat64_get(sector, cluster);
        case 128: return brainfs_fat128_get(sector, cluster);
        default:  return 0;
    }
}

/* Dispatched set: writes a FAT entry to a raw sector buffer */
void brainfs_fat_set_entry(uint8_t width, uint8_t *sector, uint32_t cluster, uint64_t value) {
    switch (width) {
        case 1:   brainfs_fat1_set(sector, cluster, value); break;
        case 2:   brainfs_fat2_set(sector, cluster, value); break;
        case 4:   brainfs_fat4_set(sector, cluster, value); break;
        case 8:   brainfs_fat8_set(sector, cluster, value); break;
        case 12:  brainfs_fat12_set(sector, cluster, value); break;
        case 16:  brainfs_fat16_set(sector, cluster, value); break;
        case 32:  brainfs_fat32_set(sector, cluster, value); break;
        case 64:  brainfs_fat64_set(sector, cluster, value); break;
        case 128: brainfs_fat128_set(sector, cluster, value); break;
        default:  break;
    }
}

/* Check if an entry represents end-of-chain for a given width */
int brainfs_fat_is_eoc_value(uint8_t width, uint64_t value) {
    switch (width) {
        case 1:   return value == 1;        /* FAT1: no chaining, just allocated */
        case 2:   return value == 3;
        case 4:   return value == 15;
        case 8:   return value >= 0xFF;
        case 12:  return value >= 0xFF8;
        case 16:  return value >= 0xFFF8;
        case 32:  return value >= 0x0FFFFFF8;
        case 64:  return value >= 0xFFFFFFFFFFFFFFF8ULL;
        case 128: return value == 1;
        default:  return 0;
    }
}

/* Check if an entry represents a bad cluster */
int brainfs_fat_is_bad_value(uint8_t width, uint64_t value) {
    switch (width) {
        case 1:   return 0;  /* FAT1 has no bad cluster concept */
        case 2:   return value == 2;
        case 4:   return value == 14;
        case 8:   return value == 0xFF;
        case 12:  return value == 0xFF7;
        case 16:  return value == 0xFFF7;
        case 32:  return value == 0x0FFFFFF7;
        case 64:  return value == 0xFFFFFFFFFFFFFFF0ULL;
        case 128: return value == 2;
        default:  return 0;
    }
}

/* Get the free value for a given width */
uint64_t brainfs_fat_free_value(uint8_t width) {
    return 0;  /* All widths use 0 for free */
}

/* Get the max allocatable cluster value */
uint64_t brainfs_fat_max_cluster_value(uint8_t width) {
    switch (width) {
        case 1:   return 1;
        case 2:   return 3;
        case 4:   return 13;
        case 8:   return 254;
        case 12:  return 0xFF7;
        case 16:  return 0xFFF7;
        case 32:  return 0x0FFFFFF7;
        case 64:  return 0xFFFFFFFFFFFFFFEULL;
        case 128: return 0xFFFFFFFFFFFFFFFFULL;
        default:  return 0;
    }
}

/* Get name string for FAT width */
const char *brainfs_fat_width_name(uint8_t width) {
    switch (width) {
        case 1:   return "FAT1";
        case 2:   return "FAT2";
        case 4:   return "FAT4";
        case 8:   return "FAT8";
        case 12:  return "FAT12";
        case 16:  return "FAT16";
        case 32:  return "FAT32";
        case 64:  return "FAT64";
        case 128: return "FAT128";
        default:  return "UNKNOWN";
    }
}
