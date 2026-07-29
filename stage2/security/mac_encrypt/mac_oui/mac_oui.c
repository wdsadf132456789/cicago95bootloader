/**
 * Chicago-95 MAC Encrypter #5: MAC OUI Spoofer
 * Replaces the 3-byte OUI (Organizationally Unique Identifier) prefix
 * while preserving the 3-byte NIC-specific suffix
 * Includes a database of valid OUI prefixes for stealth
 */

#include "boot/security.h"

#define OUI_SPOOF_DB_SIZE  64
#define OUI_LEN            3
#define NIC_LEN            3

typedef struct {
    uint8_t  oui[OUI_LEN];
    uint8_t  valid;       /* 0=unoccupied, 1=valid entry */
} oui_entry_t;

typedef struct {
    uint8_t  original_mac[6];
    uint8_t  spoofed_mac[6];
    oui_entry_t oui_db[OUI_SPOOF_DB_SIZE];
    uint32_t oui_count;
    uint32_t selected_oui_idx;
    uint8_t  randomize_nic;   /* Also randomize NIC portion */
    uint8_t  preserve_universal; /* Preserve universal/local bit */
    uint32_t spoof_count;
    uint8_t  initialized;
    sec_stats_t stats;
} oui_spoof_state_t;

static oui_spoof_state_t oui_spoof;

/* Well-known OUI prefixes (publicly registered, non-traceable to user) */
static const uint8_t common_ouis[OUI_SPOOF_DB_SIZE][OUI_LEN] = {
    /* Dell */
    {0x00, 0x14, 0x22}, {0x00, 0x15, 0xC5}, {0x00, 0x18, 0x8B},
    {0x00, 0x1A, 0xA0}, {0x00, 0x1B, 0xB9},
    /* Intel */
    {0x00, 0x11, 0x22}, {0x00, 0x13, 0x02}, {0x00, 0x16, 0xD3},
    {0x00, 0x1B, 0x21}, {0x00, 0x1E, 0x65},
    /* HP */
    {0x00, 0x08, 0x74}, {0x00, 0x0C, 0x29}, {0x00, 0x0E, 0x7F},
    {0x00, 0x10, 0x83}, {0x00, 0x12, 0x79},
    /* Realtek */
    {0x00, 0xE0, 0x4C}, {0x52, 0x54, 0x00}, {0x08, 0x00, 0x27},
    /* Broadcom */
    {0x00, 0x10, 0x18}, {0x00, 0x14, 0xA5}, {0x00, 0x17, 0xF2},
    /* Qualcomm / Atheros */
    {0x00, 0x03, 0x7F}, {0x00, 0x13, 0x74}, {0x00, 0x1D, 0x4F},
    /* Marvell */
    {0x00, 0x50, 0x43}, {0x00, 0x1A, 0x2B}, {0x00, 0x23, 0x17},
    /* Cisco */
    {0x00, 0x00, 0x0C}, {0x00, 0x01, 0x42}, {0x00, 0x01, 0x43},
    /* Apple */
    {0x00, 0x03, 0x93}, {0x00, 0x05, 0x02}, {0x00, 0x0A, 0x95},
    {0x00, 0x14, 0xBF}, {0x00, 0x16, 0xCB},
    /* Microsoft */
    {0x00, 0x0D, 0x3A}, {0x00, 0x12, 0x5F}, {0x00, 0x15, 0x5D},
    /* Generic / common virtual */
    {0x00, 0x50, 0x56}, {0x00, 0x0C, 0x29}, {0x00, 0x05, 0x69},
    {0x00, 0x0F, 0x4B}, {0x00, 0x1C, 0x14},
    /* Additional random OUIs */
    {0x3C, 0x22, 0xFB}, {0x44, 0x07, 0x0B}, {0x48, 0x5D, 0x36},
    {0x54, 0x9F, 0x13}, {0x60, 0x38, 0xE0}, {0x68, 0x05, 0xCA},
    {0x70, 0x85, 0xC2}, {0x74, 0xAC, 0xB9}, {0x78, 0x8A, 0x20},
    {0x7C, 0x83, 0x34}, {0x80, 0xCE, 0x62}, {0x84, 0x7B, 0xEB},
    {0x88, 0xDC, 0xF6}, {0x8C, 0xEC, 0x4B}, {0x90, 0x72, 0x40},
    {0x94, 0x10, 0x3E}, {0x98, 0xFA, 0x3B}, {0x9C, 0x32, 0xCE},
    {0xA0, 0x63, 0x91}, {0xA4, 0x34, 0xD9}, {0xA8, 0x5E, 0x1C},
    {0xAC, 0x22, 0x05}, {0xB0, 0x83, 0xFE}, {0xB4, 0x2E, 0x99},
    {0xB8, 0x27, 0xEB}, {0xBC, 0x5F, 0xAF}, {0xC0, 0x4A, 0x00},
    {0xC4, 0x3D, 0xC7}, {0xC8, 0x5B, 0x76}, {0xCC, 0x46, 0xD6},
    {0xD0, 0x21, 0xF9}, {0xD4, 0x6D, 0x6D}, {0xD8, 0xF7, 0x10},
    {0xDC, 0xA6, 0x32}, {0xE0, 0x63, 0xDA}, {0xE4, 0x5E, 0x37},
    {0xE8, 0x6A, 0x35}, {0xEC, 0x08, 0x6B}, {0xF0, 0x9F, 0xE2},
    {0xF4, 0xEC, 0x38}, {0xF8, 0x28, 0x19}, {0xFC, 0x15, 0xB4},
};

/* ---- Init ---- */
int oui_spoof_init(const uint8_t original_mac[6]) {
    if (!original_mac) return SEC_ERR_BAD_PARAM;

    uint8_t *d = (uint8_t*)&oui_spoof;
    for (uint32_t i = 0; i < sizeof(oui_spoof_state_t); i++) d[i] = 0;

    for (uint32_t i = 0; i < 6; i++) oui_spoof.original_mac[i] = original_mac[i];

    /* Load OUI database */
    oui_spoof.oui_count = OUI_SPOOF_DB_SIZE;
    for (uint32_t i = 0; i < OUI_SPOOF_DB_SIZE; i++) {
        for (uint32_t j = 0; j < OUI_LEN; j++)
            oui_spoof.oui_db[i].oui[j] = common_ouis[i][j];
        oui_spoof.oui_db[i].valid = 1;
    }

    oui_spoof.randomize_nic = 0;
    oui_spoof.preserve_universal = 1;
    oui_spoof.initialized = 1;

    return SEC_OK;
}

/* ---- Select OUI by index ---- */
int oui_spoof_select(uint32_t oui_idx) {
    if (oui_idx >= oui_spoof.oui_count) return SEC_ERR_BAD_PARAM;
    if (!oui_spoof.initialized) return SEC_ERR_NOT_INIT;

    oui_spoof.selected_oui_idx = oui_idx;

    for (uint32_t i = 0; i < OUI_LEN; i++)
        oui_spoof.spoofed_mac[i] = oui_spoof.oui_db[oui_idx].oui[i];

    /* NIC portion */
    if (oui_spoof.randomize_nic) {
        uint8_t nic[NIC_LEN];
        sec_random_bytes(nic, NIC_LEN);
        nic[0] &= 0xFE;  /* Clear multicast bit */
        for (uint32_t i = 0; i < NIC_LEN; i++)
            oui_spoof.spoofed_mac[OUI_LEN + i] = nic[i];
    } else {
        for (uint32_t i = 0; i < NIC_LEN; i++)
            oui_spoof.spoofed_mac[OUI_LEN + i] = oui_spoof.original_mac[OUI_LEN + i];
    }

    /* Preserve universal/local bit from original */
    if (oui_spoof.preserve_universal) {
        if (oui_spoof.original_mac[0] & 0x02)
            oui_spoof.spoofed_mac[0] |= 0x02;
        else
            oui_spoof.spoofed_mac[0] &= ~0x02;
    }

    return SEC_OK;
}

/* ---- Select random OUI ---- */
int oui_spoof_select_random(void) {
    if (!oui_spoof.initialized) return SEC_ERR_NOT_INIT;

    uint32_t idx = sec_random_u32() % oui_spoof.oui_count;
    return oui_spoof_select(idx);
}

/* ---- Select OUI by vendor name (match prefix) ---- */
int oui_spoof_select_vendor(const char *vendor) {
    if (!vendor) return SEC_ERR_BAD_PARAM;

    /* Simple prefix matching */
    for (uint32_t i = 0; i < oui_spoof.oui_count; i++) {
        /* Check first 2 bytes of OUI against common vendor patterns */
        const uint8_t *oui = oui_spoof.oui_db[i].oui;

        if (vendor[0] == 'D' && vendor[1] == 'e' && oui[0] == 0x00 && oui[1] == 0x14)
            return oui_spoof_select(i);
        if (vendor[0] == 'I' && vendor[1] == 'n' && oui[0] == 0x00 && oui[1] == 0x11)
            return oui_spoof_select(i);
        if (vendor[0] == 'H' && vendor[1] == 'P' && oui[0] == 0x00 && oui[1] == 0x08)
            return oui_spoof_select(i);
        if (vendor[0] == 'C' && vendor[1] == 'i' && oui[0] == 0x00 && oui[1] == 0x00)
            return oui_spoof_select(i);
        if (vendor[0] == 'A' && vendor[1] == 'p' && oui[0] == 0x00 && oui[1] == 0x03)
            return oui_spoof_select(i);
        if (vendor[0] == 'M' && vendor[1] == 'i' && oui[0] == 0x00 && oui[1] == 0x0D)
            return oui_spoof_select(i);
    }

    /* Default to random */
    return oui_spoof_select_random();
}

/* ---- Get spoofed MAC ---- */
int oui_spoof_get(uint8_t out[6]) {
    if (!out) return SEC_ERR_BAD_PARAM;
    if (!oui_spoof.initialized) return SEC_ERR_NOT_INIT;

    for (uint32_t i = 0; i < 6; i++) out[i] = oui_spoof.spoofed_mac[i];
    return SEC_OK;
}

/* ---- Apply to NIC ---- */
int oui_spoof_apply(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, oui_spoof.spoofed_mac);
    oui_spoof.spoof_count++;
    oui_spoof.stats.connections_opened++;
    return SEC_OK;
}

/* ---- Restore original ---- */
int oui_spoof_restore(boot_nic_t *nic) {
    if (!nic) return SEC_ERR_BAD_PARAM;

    boot_nic_set_mac(nic, oui_spoof.original_mac);
    return SEC_OK;
}

/* ---- Get current OUI (3 bytes) ---- */
void oui_spoof_get_oui(uint8_t out_oui[3]) {
    if (out_oui) {
        for (uint32_t i = 0; i < 3; i++)
            out_oui[i] = oui_spoof.spoofed_mac[i];
    }
}

void oui_spoof_set_randomize_nic(int enable) {
    oui_spoof.randomize_nic = enable ? 1 : 0;
}

void oui_spoof_set_preserve_universal(int enable) {
    oui_spoof.preserve_universal = enable ? 1 : 0;
}

void oui_spoof_get_stats(sec_stats_t *stats) {
    if (stats) {
        uint8_t *d = (uint8_t*)stats;
        const uint8_t *s = (const uint8_t*)&oui_spoof.stats;
        for (uint32_t i = 0; i < sizeof(sec_stats_t); i++) d[i] = s[i];
    }
}
