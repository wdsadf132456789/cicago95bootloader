#include <stdint.h>
#include "bootinfo.h"
#include "e820.h"

void bootinfo_build(boot_info_t *bih, uint64_t entry, uint32_t crc,
                    uint32_t mod_count, uint64_t boot_ticks,
                    const acpi_info_t *acpi) {
    bih->magic = 0x46494243;
    bih->flags = (crc != 0 && crc != 0xFFFFFFFF) ? 0x01 : 0;
    if (mod_count > 0) bih->flags |= 0x02;
    if (acpi->rsdp_addr) bih->flags |= 0x04;
    bih->e820_addr = E820_MAP_ADDR;
    bih->e820_count = e820_get_count();
    bih->kernel_entry = entry;
    bih->kernel_crc = crc;
    bih->module_count = mod_count;
    bih->module_list = (mod_count > 0) ? (uint64_t)0x200000 : 0;
    bih->framebuffer = 0;
    bih->fb_width = 0;
    bih->fb_height = 0;
    bih->fb_bpp = 0;
    bih->boot_ticks = boot_ticks;
    for (int i = 0; i < 20; i++) bih->measured_hash[i] = 0;
    for (int i = 0; i < 4 && i < 20; i++)
        bih->measured_hash[i] = ((uint8_t *)&crc)[i];
    {
        const char *id = "Chicago-95/Stage3 v1.0";
        int i;
        for (i = 0; id[i] && i < 31; i++)
            bih->boot_loader[i] = id[i];
        bih->boot_loader[i] = 0;
    }
    bih->rsdp_addr = acpi->rsdp_addr;
    bih->lapic_addr = acpi->lapic_addr;
    bih->lapic_count = acpi->lapic_count;
    bih->ioapic_count = acpi->ioapic_count;
}

void bootinfo_stamp_msr(uint32_t kernel_crc, uint32_t modules_loaded) {
    __asm__ volatile(
        "movb $'M', 0x700\n"
        "movb $'S', 0x701\n"
        "movb $'R', 0x702\n"
        "movb $'D', 0x703\n"
        "movl %0, 0x704\n"
        "movl %1, 0x708\n"
        "movb %2, 0x70C\n"
        : : "r"(kernel_crc), "r"((uint32_t)0x20250728),
            "r"((uint8_t)modules_loaded) : "memory"
    );
}
