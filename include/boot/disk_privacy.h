/**
 * Chicago-95 Bootloader Disk Privacy Subsystem
 * 3 UUID Encrypters + 7 Disk Identity Maskers
 *
 * All 10 modules run at boot-time before the kernel loads.
 * They encrypt/obfuscate disk identifiers so lsblk/blkid/udevadm
 * cannot discover real disk data, UUIDs, serial numbers, or labels.
 */

#ifndef BOOT_DISK_PRIVACY_H
#define BOOT_DISK_PRIVACY_H

#include <stdint.h>

/* ========================================================================
 * Common constants
 * ======================================================================== */
#define DP_OK                   0
#define DP_ERR_NOMEM           -1
#define DP_ERR_BAD_PARAM       -2
#define DP_ERR_NOT_FOUND       -3
#define DP_ERR_NO_SPACE        -4

#define DP_MAX_DISKS           32
#define DP_MAX_PARTITIONS      128
#define DP_UUID_LEN            16
#define DP_GPT_UUID_LEN        16
#define DP_SERIAL_LEN          64
#define DP_MODEL_LEN           64
#define DP_LABEL_LEN           64
#define DP_MAX_MAP             256
#define DP_KEY_LEN             32

/* ========================================================================
 * UUID-1: UUID Encrypter
 * ======================================================================== */
typedef struct {
    uint8_t  real_uuid[DP_UUID_LEN];
    uint8_t  enc_uuid[DP_UUID_LEN];
    uint8_t  sector;        /* disk sector index */
    uint8_t  part_idx;      /* partition index */
} dp_uuid_map_t;

typedef struct {
    dp_uuid_map_t map[DP_MAX_MAP];
    uint32_t      count;
    uint8_t       key[DP_KEY_LEN];
    uint8_t       armed;
} dp_uuid_encrypt_state_t;

int  dp_uuid_encrypt_init(void);
int  dp_uuid_encrypt_add(const uint8_t real_uuid[DP_UUID_LEN],
                         uint8_t sector, uint8_t part_idx);
int  dp_uuid_encrypt_get_enc(const uint8_t real_uuid[DP_UUID_LEN],
                             uint8_t out_enc[DP_UUID_LEN]);
int  dp_uuid_encrypt_get_real(const uint8_t enc_uuid[DP_UUID_LEN],
                              uint8_t out_real[DP_UUID_LEN]);
int  dp_uuid_encrypt_count(void);
void dp_uuid_encrypt_reset(void);

/* ========================================================================
 * UUID-2: UUID Rotator
 * ======================================================================== */
typedef struct {
    uint8_t  current_uuid[DP_UUID_LEN];
    uint8_t  prev_uuid[DP_UUID_LEN];
    uint64_t last_rotate_tsc;
    uint64_t interval_tsc;    /* rotation interval in TSC ticks */
    uint32_t rotate_count;
    uint8_t  armed;
} dp_uuid_rot_state_t;

int  dp_uuid_rot_init(void);
int  dp_uuid_rot_set_interval(uint64_t interval_tsc);
int  dp_uuid_rot_set_uuid(const uint8_t uuid[DP_UUID_LEN]);
int  dp_uuid_rot_rotate(void);
int  dp_uuid_rot_get_current(uint8_t out_uuid[DP_UUID_LEN]);
int  dp_uuid_rot_get_prev(uint8_t out_uuid[DP_UUID_LEN]);
int  dp_uuid_rot_get_count(void);

/* ========================================================================
 * UUID-3: UUID Cloner
 * ======================================================================== */
typedef struct {
    uint8_t  src_disk;
    uint8_t  dst_disk;
    uint8_t  cloned_uuid[DP_UUID_LEN];
    uint8_t  active;
} dp_uuid_clone_map_t;

typedef struct {
    dp_uuid_clone_map_t map[DP_MAX_DISKS];
    uint32_t            count;
    uint8_t             key[DP_KEY_LEN];
    uint8_t             armed;
} dp_uuid_clone_state_t;

int  dp_uuid_clone_init(void);
int  dp_uuid_clone_add(uint8_t src_disk, uint8_t dst_disk,
                       const uint8_t cloned_uuid[DP_UUID_LEN]);
int  dp_uuid_clone_get(uint8_t disk, uint8_t out_uuid[DP_UUID_LEN]);
int  dp_uuid_clone_resolve(uint8_t disk, uint8_t *out_real_disk,
                           uint8_t out_uuid[DP_UUID_LEN]);
int  dp_uuid_clone_count(void);
void dp_uuid_clone_reset(void);

/* ========================================================================
 * DISK-1: Serial Number Masker
 * ======================================================================== */
typedef struct {
    uint8_t  disk_idx;
    char     real_serial[DP_SERIAL_LEN];
    char     masked_serial[DP_SERIAL_LEN];
    uint8_t  active;
} dp_serial_map_t;

typedef struct {
    dp_serial_map_t map[DP_MAX_DISKS];
    uint32_t        count;
    uint8_t         key[DP_KEY_LEN];
    uint8_t         armed;
} dp_serial_mask_state_t;

int  dp_serial_mask_init(void);
int  dp_serial_mask_add(uint8_t disk_idx, const char *real_serial);
int  dp_serial_mask_get_masked(uint8_t disk_idx, char *out_serial);
int  dp_serial_mask_get_real(const char *masked_serial, char *out_real);
int  dp_serial_mask_count(void);
void dp_serial_mask_reset(void);

/* ========================================================================
 * DISK-2: Model String Spoofer
 * ======================================================================== */
typedef struct {
    uint8_t  disk_idx;
    char     real_model[DP_MODEL_LEN];
    char     spoofed_model[DP_MODEL_LEN];
    uint8_t  active;
} dp_model_map_t;

typedef struct {
    dp_model_map_t map[DP_MAX_DISKS];
    uint32_t       count;
    uint8_t        key[DP_KEY_LEN];
    uint8_t        armed;
} dp_model_spoof_state_t;

int  dp_model_spoof_init(void);
int  dp_model_spoof_add(uint8_t disk_idx, const char *real_model);
int  dp_model_spoof_spoof(uint8_t disk_idx, const char *spoofed_model);
int  dp_model_spoof_get(uint8_t disk_idx, char *out_model);
int  dp_model_spoof_get_real(const char *spoofed, char *out_real);
int  dp_model_spoof_count(void);
void dp_model_spoof_reset(void);

/* ========================================================================
 * DISK-3: Volume Label Encrypter
 * ======================================================================== */
typedef struct {
    uint8_t  disk_idx;
    uint8_t  part_idx;
    char     real_label[DP_LABEL_LEN];
    char     enc_label[DP_LABEL_LEN];
    uint8_t  active;
} dp_label_map_t;

typedef struct {
    dp_label_map_t map[DP_MAX_PARTITIONS];
    uint32_t       count;
    uint8_t        key[DP_KEY_LEN];
    uint8_t        armed;
} dp_label_enc_state_t;

int  dp_label_enc_init(void);
int  dp_label_enc_add(uint8_t disk_idx, uint8_t part_idx,
                      const char *real_label);
int  dp_label_enc_get_enc(uint8_t disk_idx, uint8_t part_idx,
                          char *out_label);
int  dp_label_enc_get_real(const char *enc_label, char *out_real);
int  dp_label_enc_count(void);
void dp_label_enc_reset(void);

/* ========================================================================
 * DISK-4: MBR/GPT Signature Masker
 * ======================================================================== */
typedef struct {
    uint8_t   disk_idx;
    uint32_t  real_mbr_sig;       /* 0x1B8 MBR disk signature */
    uint32_t  masked_mbr_sig;
    uint8_t   real_gpt_guid[DP_GPT_UUID_LEN];
    uint8_t   masked_gpt_guid[DP_GPT_UUID_LEN];
    uint8_t   active;
} dp_mbr_gpt_map_t;

typedef struct {
    dp_mbr_gpt_map_t map[DP_MAX_DISKS];
    uint32_t         count;
    uint8_t          key[DP_KEY_LEN];
    uint8_t          armed;
} dp_mbr_gpt_mask_state_t;

int  dp_mbr_gpt_mask_init(void);
int  dp_mbr_gpt_mask_add(uint8_t disk_idx,
                         uint32_t real_mbr_sig,
                         const uint8_t real_gpt_guid[DP_GPT_UUID_LEN]);
int  dp_mbr_gpt_mask_get(uint8_t disk_idx,
                         uint32_t *out_mbr_sig,
                         uint8_t out_gpt_guid[DP_GPT_UUID_LEN]);
int  dp_mbr_gpt_mask_resolve(uint32_t masked_mbr_sig,
                             uint8_t *out_disk,
                             uint32_t *out_real);
int  dp_mbr_gpt_mask_count(void);
void dp_mbr_gpt_mask_reset(void);

/* ========================================================================
 * DISK-5: LBA Address Scrambler
 * ======================================================================== */
typedef struct {
    uint8_t  disk_idx;
    uint64_t real_lba;
    uint64_t scrambled_lba;
    uint8_t  active;
} dp_lba_map_t;

typedef struct {
    dp_lba_map_t map[DP_MAX_MAP];
    uint32_t     count;
    uint8_t      key[DP_KEY_LEN];
    uint8_t      armed;
} dp_lba_scramble_state_t;

int  dp_lba_scramble_init(void);
int  dp_lba_scramble_add(uint8_t disk_idx, uint64_t real_lba);
int  dp_lba_scramble_get(uint8_t disk_idx, uint64_t real_lba,
                         uint64_t *out_scrambled);
int  dp_lba_scramble_resolve(uint8_t disk_idx, uint64_t scrambled_lba,
                             uint64_t *out_real);
int  dp_lba_scramble_count(void);
void dp_lba_scramble_reset(void);

/* ========================================================================
 * DISK-6: Disk Size Obfuscator
 * ======================================================================== */
typedef struct {
    uint8_t  disk_idx;
    uint64_t real_sectors;
    uint64_t reported_sectors;
    uint32_t real_sector_size;
    uint32_t reported_sector_size;
    uint8_t  active;
} dp_size_map_t;

typedef struct {
    dp_size_map_t map[DP_MAX_DISKS];
    uint32_t      count;
    uint8_t       key[DP_KEY_LEN];
    uint8_t       armed;
} dp_size_obfs_state_t;

int  dp_size_obfs_init(void);
int  dp_size_obfs_add(uint8_t disk_idx, uint64_t real_sectors,
                      uint32_t real_sector_size);
int  dp_size_obfs_get(uint8_t disk_idx, uint64_t *out_sectors,
                      uint32_t *out_sector_size);
int  dp_size_obfs_resolve(uint8_t disk_idx, uint64_t reported_sectors,
                          uint64_t *out_real);
int  dp_size_obfs_count(void);
void dp_size_obfs_reset(void);

/* ========================================================================
 * DISK-7: Filesystem Type Hider
 * ======================================================================== */
typedef struct {
    uint8_t  disk_idx;
    uint8_t  part_idx;
    char     real_fstype[32];
    char     hidden_fstype[32];
    uint8_t  active;
} dp_fstype_map_t;

typedef struct {
    dp_fstype_map_t map[DP_MAX_PARTITIONS];
    uint32_t        count;
    uint8_t         key[DP_KEY_LEN];
    uint8_t         armed;
} dp_fs_hide_state_t;

int  dp_fs_hide_init(void);
int  dp_fs_hide_add(uint8_t disk_idx, uint8_t part_idx,
                    const char *real_fstype);
int  dp_fs_hide_spoof(uint8_t disk_idx, uint8_t part_idx,
                      const char *hidden_fstype);
int  dp_fs_hide_get(uint8_t disk_idx, uint8_t part_idx,
                    char *out_fstype);
int  dp_fs_hide_get_real(const char *hidden_fstype, char *out_real);
int  dp_fs_hide_count(void);
void dp_fs_hide_reset(void);

/* ========================================================================
 * Master disk privacy init
 * ======================================================================== */
typedef struct {
    uint8_t  uuid_encrypt_enabled;
    uint8_t  uuid_rot_enabled;
    uint8_t  uuid_clone_enabled;
    uint8_t  serial_mask_enabled;
    uint8_t  model_spoof_enabled;
    uint8_t  label_enc_enabled;
    uint8_t  mbr_gpt_mask_enabled;
    uint8_t  lba_scramble_enabled;
    uint8_t  size_obfs_enabled;
    uint8_t  fs_hide_enabled;
    uint8_t  module_count;
    uint64_t init_time_us;
} dp_master_state_t;

int  dp_master_init(void);
void dp_master_get_state(dp_master_state_t *out);

#endif /* BOOT_DISK_PRIVACY_H */
