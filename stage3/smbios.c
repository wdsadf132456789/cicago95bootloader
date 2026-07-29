#include <stdint.h>
#include "smbios.h"
#include "console.h"

typedef struct {
    char signature[4];
    uint8_t checksum;
    uint8_t len;
    uint8_t major;
    uint8_t minor;
    uint16_t max_size;
    uint8_t revision;
    uint8_t reserved[5];
    char dmi_signature[5];
    uint8_t dmi_checksum;
    uint16_t table_len;
    uint32_t table_addr;
    uint16_t num_structs;
    uint8_t bcd_rev;
} __attribute__((packed)) smbios_entry_t;

typedef struct {
    uint8_t type;
    uint8_t len;
    uint16_t handle;
} __attribute__((packed)) smbios_header_t;

static uint8_t checksum_range(const uint8_t *data, int len) {
    uint8_t sum = 0;
    for (int i = 0; i < len; i++) sum += data[i];
    return sum;
}

static void read_string(const uint8_t *strtab, int idx, char *out, int max) {
    if (idx == 0) { out[0] = 0; return; }
    int cur = 1;
    for (int i = 0; i < idx; i++) {
        int len = 0;
        while (strtab[cur + len]) len++;
        cur += len + 1;
    }
    int i = 0;
    while (strtab[cur] && i < max - 1)
        out[i++] = strtab[cur++];
    out[i] = 0;
}

static void scan_entry_point(smbios_entry_t *ep, smbios_info_t *info) {
    uint8_t *base = (uint8_t *)(uintptr_t)ep->table_addr;
    int offset = 0;
    for (int s = 0; s < ep->num_structs && offset < ep->table_len; s++) {
        smbios_header_t *hdr = (smbios_header_t *)(base + offset);
        uint8_t *strings = base + offset + hdr->len;

        if (hdr->type == 0) {
            read_string(strings, base[offset + 0x04], info->bios_vendor, sizeof(info->bios_vendor));
            read_string(strings, base[offset + 0x05], info->bios_version, sizeof(info->bios_version));
            read_string(strings, base[offset + 0x08], info->bios_date, sizeof(info->bios_date));
        }
        if (hdr->type == 1) {
            read_string(strings, base[offset + 0x04], info->manufacturer, sizeof(info->manufacturer));
            read_string(strings, base[offset + 0x05], info->product, sizeof(info->product));
            if (hdr->len >= 0x08) {
                int pos = 0;
                for (int i = 0; i < 8 && pos < 36; i++) {
                    if (i == 4 || i == 6) { info->uuid[pos++] = '-'; }
                    uint8_t b = base[offset + 0x07 - i];
                    if (b < 16) { info->uuid[pos++] = '0'; }
                    info->uuid[pos++] = "0123456789ABCDEF"[b >> 4];
                    info->uuid[pos++] = "0123456789ABCDEF"[b & 0xF];
                    if (i == 3 || i == 5) { info->uuid[pos++] = '-'; }
                }
                info->uuid[36] = 0;
            } else {
                info->uuid[0] = 0;
            }
        }

        offset += hdr->len;
        while (offset < ep->table_len) {
            if (base[offset] == 0 && base[offset + 1] == 0) {
                offset += 2;
                break;
            }
            offset++;
        }
    }
}

int smbios_scan(smbios_info_t *info) {
    for (int i = 0; i < sizeof(info->manufacturer); i++) info->manufacturer[i] = 0;
    for (int i = 0; i < sizeof(info->product); i++) info->product[i] = 0;
    for (int i = 0; i < sizeof(info->bios_vendor); i++) info->bios_vendor[i] = 0;
    for (int i = 0; i < sizeof(info->bios_version); i++) info->bios_version[i] = 0;
    for (int i = 0; i < sizeof(info->bios_date); i++) info->bios_date[i] = 0;
    info->uuid[0] = 0;

    uint8_t *search = (uint8_t *)0xE0000;
    uint8_t *end    = (uint8_t *)0xFFFFF;

    while ((uintptr_t)search < (uintptr_t)end) {
        if (search[0] == '_' && search[1] == 'S' && search[2] == 'M' && search[3] == '_') {
            smbios_entry_t *ep = (smbios_entry_t *)search;
            if (ep->len >= 0x1F && checksum_range(search, ep->len) == 0) {
                scan_entry_point(ep, info);
                return 1;
            }
            search += 16;
            continue;
        }
        if (search[0] == '_' && search[1] == 'D' && search[2] == 'M' && search[3] == 'I' && search[4] == '_') {
            if (checksum_range(search, 0x0F) == 0) {
                smbios_entry_t *ep = (smbios_entry_t *)(search - 5);
                if (ep->signature[0] == '_' && ep->signature[1] == 'S' && ep->signature[2] == 'M' && ep->signature[3] == '_') {
                    scan_entry_point(ep, info);
                    return 1;
                }
            }
        }
        search += 16;
    }
    return 0;
}

void smbios_print(const smbios_info_t *info) {
    if (info->manufacturer[0]) {
        cons_color("  System: ", COL_LABEL);
        cons_color(info->manufacturer, COL_OK);
        cons_puts(" ");
        cons_color(info->product, COL_OK);
        cons_puts("\n");
    }
    if (info->bios_vendor[0]) {
        cons_color("  BIOS:   ", COL_LABEL);
        cons_color(info->bios_vendor, COL_DEFAULT);
        cons_puts(" ");
        cons_color(info->bios_version, COL_DEFAULT);
        cons_puts(" (");
        cons_color(info->bios_date, COL_DIM);
        cons_puts(")\n");
    }
    if (info->uuid[0]) {
        cons_color("  UUID:   ", COL_LABEL);
        cons_color(info->uuid, COL_DIM);
        cons_puts("\n");
    }
}
