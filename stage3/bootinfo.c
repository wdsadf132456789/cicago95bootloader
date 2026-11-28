#include <stdint.h>
#include "bootinfo.h"
#include "e820.h"

static uint32_t cmdline_append(char *dst, uint32_t pos, const char *src) {
    while (*src && pos + 1 < BOOT_CMDLINE_LEN) dst[pos++] = *src++;
    dst[pos] = 0;
    return pos;
}

static uint32_t cmdline_append_module_name(char *dst, uint32_t pos, const char *name) {
    for (uint32_t i = 0; i < BOOT_MODULE_NAME_LEN && name[i] &&
                         pos + 1 < BOOT_CMDLINE_LEN; i++) {
        dst[pos++] = name[i];
    }
    dst[pos] = 0;
    return pos;
}

static uint32_t cmdline_append_u64(char *dst, uint32_t pos, uint64_t value) {
    char tmp[21];
    uint32_t n = 0;

    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value && n < 20) {
            tmp[n++] = '0' + (value % 10);
            value /= 10;
        }
    }

    for (uint32_t i = 0; i < n; i++) {
        if (pos + 1 >= BOOT_CMDLINE_LEN) break;
        dst[pos++] = tmp[n - 1 - i];
    }
    dst[pos] = 0;
    return pos;
}

static uint32_t cmdline_append_hex32(char *dst, uint32_t pos, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[9];

    for (uint32_t i = 0; i < 8; i++)
        tmp[i] = hex[(value >> (28 - i * 4)) & 0xF];
    tmp[8] = 0;
    return cmdline_append(dst, pos, tmp);
}

static void bootinfo_build_cmdline(boot_info_t *bih, uint32_t crc,
                                   const boot_module_t *modules,
                                   uint32_t mod_count, uint64_t boot_ticks) {
    char *cmd = bih->kernel_cmdline;
    uint32_t pos = 0;

    pos = cmdline_append(cmd, pos, "loader=Chicago95/Stage3");
    pos = cmdline_append(cmd, pos, " modules=");
    pos = cmdline_append_u64(cmd, pos, mod_count);
    pos = cmdline_append(cmd, pos, " crc=0x");
    pos = cmdline_append_hex32(cmd, pos, crc);
    pos = cmdline_append(cmd, pos, " ticks=");
    pos = cmdline_append_u64(cmd, pos, boot_ticks);
    pos = cmdline_append(cmd, pos, " names=");

    for (uint32_t i = 0; i < mod_count; i++) {
        if (i) pos = cmdline_append(cmd, pos, ",");
        pos = cmdline_append_module_name(cmd, pos, modules[i].name);
    }

    cmd[pos] = 0;
}

void bootinfo_build(boot_info_t *bih, uint64_t entry, uint32_t crc,
                    const boot_module_t *modules, uint32_t mod_count,
                    uint64_t boot_ticks, const acpi_info_t *acpi) {
    uint8_t *raw = (uint8_t *)bih;
    for (uint32_t i = 0; i < sizeof(*bih); i++) raw[i] = 0;

    if (mod_count > BOOT_MODULE_MAX) mod_count = BOOT_MODULE_MAX;

    bih->magic = BOOT_INFO_MAGIC;
    bih->version = BOOT_INFO_VERSION;
    bih->flags = (crc != 0 && crc != 0xFFFFFFFF) ? BOOT_FLAG_KERNEL_VALID : 0;
    if (mod_count > 0) bih->flags |= BOOT_FLAG_MODULES;
    if (acpi->rsdp_addr) bih->flags |= BOOT_FLAG_ACPI;
    bih->flags |= BOOT_FLAG_E820;

    bih->e820_addr = E820_MAP_ADDR;
    bih->e820_count = e820_get_count();
    bih->kernel_entry = entry;
    bih->kernel_crc = crc;
    bih->module_count = mod_count;
    bih->module_list = (uint64_t)bih->modules;
    for (uint32_t i = 0; i < mod_count; i++) {
        bih->modules[i] = modules[i];
    }

    bih->framebuffer = 0;
    bih->fb_width = 0;
    bih->fb_height = 0;
    bih->fb_bpp = 0;
    bih->boot_ticks = boot_ticks;

    for (uint32_t i = 0; i < 20; i++) bih->measured_hash[i] = 0;
    for (uint32_t i = 0; i < 4 && i < 20; i++)
        bih->measured_hash[i] = ((uint8_t *)&crc)[i];

    {
        const char *id = "Chicago-95/Stage3 v2.0";
        uint32_t i;
        for (i = 0; id[i] && i < sizeof(bih->boot_loader) - 1; i++)
            bih->boot_loader[i] = id[i];
        bih->boot_loader[i] = 0;
    }

    bootinfo_build_cmdline(bih, crc, modules, mod_count, boot_ticks);

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
