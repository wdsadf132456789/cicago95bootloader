#ifndef STAGE3_BOOTINFO_H
#define STAGE3_BOOTINFO_H

#include <stdint.h>
#include "acpi.h"

#define BOOT_INFO_ADDR  0x7000
#define MSR_ADDR        0x700

typedef struct {
    uint32_t magic;
    uint32_t flags;
    uint64_t e820_addr;
    uint32_t e820_count;
    uint64_t kernel_entry;
    uint32_t kernel_crc;
    uint32_t module_count;
    uint64_t module_list;
    uint64_t framebuffer;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_bpp;
    uint64_t boot_ticks;
    uint8_t  measured_hash[20];
    char     boot_loader[32];
    uint64_t rsdp_addr;
    uint32_t lapic_addr;
    uint32_t lapic_count;
    uint32_t ioapic_count;
    uint8_t  _pad[4];
} __attribute__((packed)) boot_info_t;

void bootinfo_build(boot_info_t *bih, uint64_t entry, uint32_t crc,
                    uint32_t mod_count, uint64_t boot_ticks,
                    const acpi_info_t *acpi);

void bootinfo_stamp_msr(uint32_t kernel_crc, uint32_t modules_loaded);

#endif