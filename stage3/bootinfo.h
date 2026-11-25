#ifndef STAGE3_BOOTINFO_H
#define STAGE3_BOOTINFO_H

#include "boot/bootinfo.h"
#include "acpi.h"

void bootinfo_build(boot_info_t *bih, uint64_t entry, uint32_t crc,
                    const boot_module_t *modules, uint32_t mod_count,
                    uint64_t boot_ticks, const acpi_info_t *acpi);

void bootinfo_stamp_msr(uint32_t kernel_crc, uint32_t modules_loaded);

#endif
