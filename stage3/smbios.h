#ifndef STAGE3_SMBIOS_H
#define STAGE3_SMBIOS_H

#include <stdint.h>

typedef struct {
    char manufacturer[64];
    char product[64];
    char bios_vendor[64];
    char bios_version[64];
    char bios_date[32];
    char uuid[37];
} smbios_info_t;

int smbios_scan(smbios_info_t *info);
void smbios_print(const smbios_info_t *info);

#endif