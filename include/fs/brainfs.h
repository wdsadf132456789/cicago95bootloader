/**
 * Chicago-95 BrainFS - Bootable FAT Filesystem
 * Supports FAT widths from 1-bit to 128-bit
 *
 * FAT1:   1 bit/cluster  (bitmap, 2 states: free/allocated)
 * FAT2:   2 bits/cluster (4 states)
 * FAT4:   4 bits/cluster (16 states)
 * FAT8:   8 bits/cluster (256 states, like FAT8)
 * FAT12: 12 bits/cluster (standard FAT12)
 * FAT16: 16 bits/cluster (standard FAT16)
 * FAT32: 32 bits/cluster (standard FAT32)
 * FAT64: 64 bits/cluster (extended)
 * FAT128: 128 bits/cluster (massive address space)
 *
 * On-disk layout:
 *   [Boot Sector] [FAT Table] [Root Dir] [Data Clusters]
 */

#ifndef BRAIN_FS_H
#define BRAIN_FS_H

#include <stdint.h>

/* ---- BrainFS Constants ---- */
#define BRAINFS_MAGIC           0x42524149  /* "BRAI" */
#define BRAINFS_VERSION         0x00010000  /* 1.0 */
#define BRAINFS_MAX_PATH        256
#define BRAINFS_MAX_FNAME       11
#define BRAINFS_MAX_FNAME_LONG  255
#define BRAINFS_SECTOR_SIZE     512
#define BRAINFS_CLUSTER_SIZE    4096
#define BRAINFS_MAX_OPEN_FILES  32
#define BRAINFS_MAX_MOUNTS      4

/* FAT width definitions */
#define BRAINFS_FAT1            1
#define BRAINFS_FAT2            2
#define BRAINFS_FAT4            4
#define BRAINFS_FAT8            8
#define BRAINFS_FAT12           12
#define BRAINFS_FAT16           16
#define BRAINFS_FAT32           32
#define BRAINFS_FAT64           64
#define BRAINFS_FAT128          128

/* FAT entry special values */
#define FAT1_FREE               0
#define FAT1_USED               1

#define FAT2_FREE               0
#define FAT2_EOC                3      /* End of chain */
#define FAT2_BAD                2      /* Bad cluster */

#define FAT4_FREE               0
#define FAT4_EOC                15
#define FAT4_BAD                14

#define FAT8_FREE               0x00
#define FAT8_EOC                0xFF
#define FAT8_BAD                0xFF
#define FAT8_RESERVED           0xF0

#define FAT12_FREE              0x000
#define FAT12_EOC               0xFF8
#define FAT12_BAD               0xFF7
#define FAT12_RESERVED          0xFF0

#define FAT16_FREE              0x0000
#define FAT16_EOC               0xFFF8
#define FAT16_BAD               0xFFF7

#define FAT32_FREE              0x00000000
#define FAT32_EOC               0x0FFFFFF8
#define FAT32_BAD               0x0FFFFFF7
#define FAT32_RESERVED          0x0FFFFFF0

#define FAT64_FREE              0x0000000000000000ULL
#define FAT64_EOC               0xFFFFFFFFFFFFFFF8ULL
#define FAT64_BAD               0xFFFFFFFFFFFFFFF0ULL

#define FAT128_FREE             0
#define FAT128_EOC              1
#define FAT128_BAD              2

/* File attributes */
#define BRAINFS_ATTR_READONLY   0x01
#define BRAINFS_ATTR_HIDDEN     0x02
#define BRAINFS_ATTR_SYSTEM     0x04
#define BRAINFS_ATTR_VOLUME     0x08
#define BRAINFS_ATTR_DIR        0x10
#define BRAINFS_ATTR_ARCHIVE    0x20
#define BRAINFS_ATTR_LFN        0x0F

/* ---- On-disk structures ---- */

/* Boot Sector (Sector 0) */
typedef struct {
    uint8_t  jmp_boot[3];          /* 0xEB 0x?? 0x90 */
    char     oem_name[8];          /* "BRAINFS " */
    uint16_t bytes_per_sector;     /* 512 */
    uint8_t  sectors_per_cluster;  /* 1,2,4,8... */
    uint16_t reserved_sectors;     /* 1 (boot sector) */
    uint8_t  num_fats;             /* 1 or 2 */
    uint16_t root_dir_entries;     /* FAT12/16 only */
    uint16_t total_sectors_16;     /* 0 if > 65535 */
    uint8_t  media_type;          /* 0xF8 = fixed disk */
    uint16_t fat_size_16;          /* Sectors per FAT (FAT12/16) */
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;     /* Total sectors */
    /* FAT32 extended */
    uint32_t fat_size_32;          /* Sectors per FAT (FAT32+) */
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;         /* FAT32+ root dir cluster */
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved_nt;
    uint8_t  boot_signature;       /* 0x29 */
    uint32_t volume_serial;
    char     volume_label[11];
    char     filesystem_type[8];   /* "FAT12  " "FAT16  " "FAT32  " "BRAINFS" */
    /* BrainFS extended header (after standard FAT BPB) */
    uint32_t brainfs_magic;        /* 0x42524149 */
    uint32_t brainfs_version;
    uint16_t fat_width;            /* 1,2,4,8,12,16,32,64,128 */
    uint32_t total_clusters;
    uint32_t free_clusters;
    uint64_t fat_entry_bits;       /* Bitmask of supported widths */
    uint8_t  reserved_bfs[32];
} __attribute__((packed)) brainfs_boot_sector_t;

/* Directory Entry (8.3 short name) */
typedef struct {
    char     name[8];
    char     ext[3];
    uint8_t  attr;
    uint8_t  nt_reserved;
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t access_date;
    uint16_t first_cluster_hi;     /* FAT32+ */
    uint16_t modify_time;
    uint16_t modify_date;
    uint16_t first_cluster_lo;
    uint32_t file_size;
} __attribute__((packed)) brainfs_dir_entry_t;

/* Long File Name entry (LFN) */
typedef struct {
    uint8_t  order;
    uint16_t name1[5];             /* Chars 1-5 (UTF-16LE) */
    uint8_t  attr;                 /* 0x0F */
    uint8_t  type;
    uint8_t  checksum;
    uint16_t name2[6];             /* Chars 6-11 */
    uint16_t zero;
    uint16_t name3[2];             /* Chars 12-13 */
} __attribute__((packed)) brainfs_lfn_entry_t;

/* ---- FAT entry storage ---- */
typedef struct {
    uint8_t  width;                /* FAT width: 1,2,4,8,12,16,32,64,128 */
    uint32_t total_clusters;
    uint32_t entries_per_sector;   /* How many FAT entries fit in one sector */
    uint32_t fat_sectors;          /* Total sectors for FAT table */
    uint64_t *table;               /* In-memory FAT table (if loaded) */
    uint8_t  dirty;                /* Needs flush to disk */
} brainfs_fat_t;

/* ---- File handle ---- */
typedef struct {
    char     path[BRAINFS_MAX_PATH];
    uint32_t first_cluster;
    uint32_t current_cluster;
    uint32_t file_size;
    uint32_t position;             /* Current read/write offset */
    uint8_t  mode;                 /* 0=read, 1=write, 2=read+write */
    uint8_t  open;
    uint32_t dir_cluster;          /* Parent directory cluster */
    uint16_t dir_index;            /* Index within parent dir */
} brainfs_file_t;

/* ---- Mounted filesystem ---- */
typedef struct {
    uint8_t  drive;                /* Drive letter (0=A, 1=B...) */
    uint8_t  fat_width;            /* FAT width */
    brainfs_boot_sector_t bs;
    brainfs_fat_t fat;
    uint32_t data_start;           /* First data sector */
    uint32_t root_cluster;
    brainfs_file_t open_files[BRAINFS_MAX_OPEN_FILES];
    uint8_t  mounted;
} brainfs_mount_t;

/* ---- Filesystem state ---- */
typedef struct {
    brainfs_mount_t mounts[BRAINFS_MAX_MOUNTS];
    uint32_t mount_count;
    uint8_t  initialized;
} brainfs_state_t;

/* ========================================================================
 * BrainFS API
 * ======================================================================== */

/* Init / Format */
int  brainfs_init(void);
int  brainfs_format(uint8_t drive, uint8_t fat_width, uint32_t total_sectors);
int  brainfs_mount(uint8_t drive, uint8_t fat_width);
int  brainfs_umount(uint8_t drive);

/* FAT operations */
int  brainfs_fat_read_entry(brainfs_mount_t *mnt, uint32_t cluster, uint64_t *out);
int  brainfs_fat_write_entry(brainfs_mount_t *mnt, uint32_t cluster, uint64_t value);
int  brainfs_fat_alloc_cluster(brainfs_mount_t *mnt, uint32_t *out_cluster);
int  brainfs_fat_free_chain(brainfs_mount_t *mnt, uint32_t start_cluster);
int  brainfs_fat_next_cluster(brainfs_mount_t *mnt, uint32_t cluster, uint32_t *next);
int  brainfs_fat_is_free(brainfs_mount_t *mnt, uint32_t cluster);
int  brainfs_fat_is_eoc(brainfs_mount_t *mnt, uint64_t entry);
int  brainfs_fat_is_bad(brainfs_mount_t *mnt, uint64_t entry);
int  brainfs_fat_is_eoc_value(uint8_t width, uint64_t value);
int  brainfs_fat_is_bad_value(uint8_t width, uint64_t value);
uint32_t brainfs_fat_count_free(brainfs_mount_t *mnt);

/* Directory operations */
int  brainfs_dir_read(brainfs_mount_t *mnt, uint32_t dir_cluster,
                      brainfs_dir_entry_t *entries, uint32_t max_entries);
int  brainfs_dir_find(brainfs_mount_t *mnt, uint32_t dir_cluster,
                      const char *name, brainfs_dir_entry_t *out);
int  brainfs_dir_create(brainfs_mount_t *mnt, uint32_t parent_cluster,
                        const char *name, uint8_t attr);
int  brainfs_dir_delete(brainfs_mount_t *mnt, uint32_t parent_cluster,
                        const char *name);

/* File operations */
int  brainfs_file_open(brainfs_mount_t *mnt, const char *path, uint8_t mode,
                       brainfs_file_t **out);
int  brainfs_file_close(brainfs_file_t *file);
int  brainfs_file_read(brainfs_file_t *file, void *buf, uint32_t len, uint32_t *out_len);
int  brainfs_file_write(brainfs_file_t *file, const void *buf, uint32_t len);
int  brainfs_file_seek(brainfs_file_t *file, uint32_t offset);
int  brainfs_file_tell(brainfs_file_t *file, uint32_t *offset);
int  brainfs_file_truncate(brainfs_file_t *file, uint32_t size);

/* File creation/deletion */
int  brainfs_file_create(brainfs_mount_t *mnt, const char *path, uint8_t attr);
int  brainfs_file_delete(brainfs_mount_t *mnt, const char *path);
int  brainfs_file_exists(brainfs_mount_t *mnt, const char *path);

/* Cluster I/O */
int  brainfs_cluster_read(brainfs_mount_t *mnt, uint32_t cluster,
                          void *buf, uint32_t offset, uint32_t len);
int  brainfs_cluster_write(brainfs_mount_t *mnt, uint32_t cluster,
                           const void *buf, uint32_t offset, uint32_t len);

/* Flush */
int  brainfs_flush(brainfs_mount_t *mnt);

/* Utility */
uint32_t brainfs_cluster_to_sector(brainfs_mount_t *mnt, uint32_t cluster);
uint32_t brainfs_sector_to_cluster(brainfs_mount_t *mnt, uint32_t sector);
const char *brainfs_fat_width_name(uint8_t width);
uint32_t brainfs_fat_entries_per_sector(uint8_t width);

/* ========================================================================
 * BrainFS - Internal helpers (fat_table.c)
 * ======================================================================== */

/* Low-level FAT entry packing/unpacking for each width */
uint64_t brainfs_fat1_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat1_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat2_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat2_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat4_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat4_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat8_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat8_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat12_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat12_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat16_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat16_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat32_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat32_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat64_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat64_set(uint8_t *sector, uint32_t cluster, uint64_t value);

uint64_t brainfs_fat128_get(const uint8_t *sector, uint32_t cluster);
void     brainfs_fat128_set(uint8_t *sector, uint32_t cluster, uint64_t value);

#endif /* BRAIN_FS_H */
