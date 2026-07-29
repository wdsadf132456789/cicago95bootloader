/**
 * Chicago-95 Bootloader — DISK-5 LUKS Header Camouflage
 * Hides LUKS volume headers by replacing the LUKS magic bytes
 * with fake filesystem signatures. When tools look at sector 0,
 * they see ext4/FAT/etc instead of LUKS.
 */

#include "boot/security.h"
#include <stdint.h>

/* LUKS magic: "LUKS\xba\xbe" at offset 0 */
static const uint8_t LUKS_MAGIC[6] = { 'L','U','K','S',0xBA,0xBE };
/* LUKS version 2 magic at offset 0: "$6$\0" for phdr */
static const uint8_t LUKS2_MAGIC[4] = { '$','6','$',0x00 };

/* Fake filesystem signatures to plant over LUKS magic */
static const uint8_t FAKE_EXT4[2]  = { 0x53, 0xEF }; /* ext4 superblock magic */
static const uint8_t FAKE_FAT32[8] = { 'F','A','T','3','2',' ',' ',' ' };
static const uint8_t FAKE_XFS[4]   = { 0x58, 0x46, 0x53, 0x42 }; /* XFSB */
static const uint8_t FAKE_BTRFS[8] = { '_','B','H','R','f','S','_','M' };

#define NUM_FAKE_SIGS  4

static struct {
    uint8_t  key[32];
    uint8_t  original_magic[6];   /* what was at LUKS offset 0 */
    uint8_t  original_version;    /* LUKS1=0x31, LUKS2=0x32, 0=not LUKS */
    uint8_t  active_fake;         /* which fake sig is planted */
    uint32_t hidden;
    uint32_t revealed;
} luks_state;

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

int disk_luks_camouflage_init(void) {
    sec_random_bytes(luks_state.key, 32);
    luks_state.original_version = 0;
    luks_state.active_fake = 0;
    luks_state.hidden = 0;
    luks_state.revealed = 0;
    sec_memzero(luks_state.original_magic, 6);
    return 0;
}

int disk_luks_hide_header(uint8_t *sector0, uint32_t sector_size) {
    if (!sector0 || sector_size < 512) return -1;

    /* Check if sector0 contains LUKS1 magic */
    int is_luks = 1;
    for (uint32_t i = 0; i < 6; i++) {
        if (sector0[i] != LUKS_MAGIC[i]) { is_luks = 0; break; }
    }

    /* Check LUKS2 magic at offset 512 if not LUKS1 */
    if (!is_luks && sector_size >= 1024) {
        for (uint32_t i = 0; i < 4; i++) {
            if (sector0[512 + i] != LUKS2_MAGIC[i]) { is_luks = 0; break; }
        }
        if (is_luks) {
            /* LUKS2 — remember and replace */
            luks_state.original_version = 0x32;
            for (uint32_t i = 0; i < 4; i++)
                luks_state.original_magic[i] = sector0[512 + i];
            /* Pick a fake sig */
            uint8_t pick;
            sec_random_bytes(&pick, 1);
            pick = pick % NUM_FAKE_SIGS;
            luks_state.active_fake = pick;

            switch (pick) {
                case 0: /* ext4 */
                    sector0[512] = FAKE_EXT4[0];
                    sector0[513] = FAKE_EXT4[1];
                    break;
                case 1: /* XFS */
                    for (uint32_t i = 0; i < 4; i++)
                        sector0[512 + i] = FAKE_XFS[i];
                    break;
                case 2: /* btrfs */
                    for (uint32_t i = 0; i < 8; i++)
                        sector0[512 + i] = FAKE_BTRFS[i];
                    break;
                case 3: /* FAT32-like */
                    for (uint32_t i = 0; i < 8; i++)
                        sector0[512 + i] = FAKE_FAT32[i];
                    break;
            }
            luks_state.hidden++;
            return 0;
        }
        return -2; /* not LUKS */
    }

    if (is_luks) return -2;

    /* LUKS1 at offset 0 */
    luks_state.original_version = 0x31;
    for (uint32_t i = 0; i < 6; i++)
        luks_state.original_magic[i] = sector0[i];

    uint8_t pick;
    sec_random_bytes(&pick, 1);
    pick = pick % NUM_FAKE_SIGS;
    luks_state.active_fake = pick;

    switch (pick) {
        case 0:
            sector0[0] = FAKE_EXT4[0];
            sector0[1] = FAKE_EXT4[1];
            break;
        case 1:
            for (uint32_t i = 0; i < 4; i++)
                sector0[i] = FAKE_XFS[i];
            break;
        case 2:
            for (uint32_t i = 0; i < 8; i++)
                sector0[i] = FAKE_BTRFS[i];
            break;
        case 3:
            for (uint32_t i = 0; i < 8; i++)
                sector0[i] = FAKE_FAT32[i];
            break;
    }

    luks_state.hidden++;
    return 0;
}

int disk_luks_reveal_header(uint8_t *sector0, uint32_t sector_size) {
    if (!sector0 || sector_size < 512) return -1;
    if (luks_state.original_version == 0) return -2;

    if (luks_state.original_version == 0x31) {
        for (uint32_t i = 0; i < 6; i++)
            sector0[i] = luks_state.original_magic[i];
    } else if (luks_state.original_version == 0x32 && sector_size >= 1024) {
        for (uint32_t i = 0; i < 4; i++)
            sector0[512 + i] = luks_state.original_magic[i];
    }

    luks_state.revealed++;
    return 0;
}

int disk_luks_mask_magic(uint8_t *sector0, uint32_t sector_size) {
    return disk_luks_hide_header(sector0, sector_size);
}

int disk_luks_unmask_magic(uint8_t *sector0, uint32_t sector_size) {
    return disk_luks_reveal_header(sector0, sector_size);
}

void disk_luks_get_stats(sec_stats_t *stats) {
    if (!stats) return;
    sec_memzero(stats, sizeof(sec_stats_t));
    stats->packets_handled = luks_state.hidden;
    stats->connections_active = luks_state.revealed;
    stats->drops = luks_state.original_version;
}
