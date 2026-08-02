#include <stdint.h>
#include "console.h"
#include "elf.h"
#include "disk.h"
#include "hash.h"
#include "e820.h"
#include "acpi.h"
#include "bootinfo.h"
#include "cpu.h"
#include "splash.h"
#include "cmos.h"
#include "smbios.h"
#include "kbd.h"
#include "bootprompt.h"

static uint64_t rdtsc64(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static void format_dec(char *buf, uint32_t val) {
    int i = 11;
    buf[i] = 0;
    if (val == 0) buf[--i] = '0';
    else while (val > 0) { buf[--i] = '0' + (val % 10); val /= 10; }
}

void stage3_entry(void) {
    cons_init();
    splash_banner();

    uint64_t start_ticks = rdtsc64();

    cpu_info_t cpu;
    cpu_detect(&cpu);

    smbios_info_t smbios;
    int have_smbios = smbios_scan(&smbios);

    splash_dash_item(10, "Kernel", "scanning memory...", COL_WARN);
    uint8_t *elf_base = elf_find_in_memory();
    if (!elf_base) {
        splash_dash_item(10, "Kernel", "NOT FOUND", COL_ERR);
        __asm__ volatile("cli\n1: hlt\njmp 1b");
    }
    if ((uint64_t)elf_base == 0x200000) {
        splash_dash_item(10, "Kernel", "found at 0x200000", COL_OK);
    } else {
        splash_dash_item(10, "Kernel", "found in memory", COL_OK);
    }
    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)elf_base;

    splash_dash_item(11, "CRC32", "computing...", COL_WARN);
    uint32_t kernel_crc = hash_crc32(elf_base, 512);
    uint8_t integ_ok = (kernel_crc != 0 && kernel_crc != 0xFFFFFFFF);

    char crc_buf[20];
    for (int i = 0; i < 8; i++) {
        uint8_t nib = (kernel_crc >> (28 - i*4)) & 0xF;
        crc_buf[i] = nib < 10 ? '0' + nib : 'A' + nib - 10;
    }
    crc_buf[8] = 0;
    splash_dash_item(11, "CRC32", crc_buf, integ_ok ? COL_OK : COL_WARN);

    splash_dash_item(12, "Segments", "loading...", COL_WARN);
    uint32_t segs = elf_load_all(elf_base);
    char segs_buf[12];
    format_dec(segs_buf, segs);
    splash_dash_item(12, "Segments", segs_buf, COL_OK);

    splash_dash_item(13, "Memory Map", "reading...", COL_WARN);
    e820_report();
    splash_dash_item(13, "Memory Map", "ok", COL_OK);

    splash_dash_item(14, "Modules", "scanning disk...", COL_WARN);
    module_dir_t mod_dir;
    uint32_t modules_loaded = 0;
    if (disk_ata_read(MODULE_DIR_LBA, 1, &mod_dir) &&
        mod_dir.magic == MODULE_DIR_MAGIC) {
        modules_loaded = disk_load_modules(&mod_dir);
    }
    char mod_buf[12];
    format_dec(mod_buf, modules_loaded);
    splash_dash_item(14, "Modules", mod_buf, modules_loaded ? COL_OK : COL_DIM);

    splash_dash_item(15, "ACPI", "scanning RSDP...", COL_WARN);
    acpi_info_t acpi;
    acpi_scan(&acpi);
    splash_dash_item(15, "ACPI", "done", COL_OK);

    splash_dash_item(16, "Boot Info", "building...", COL_WARN);
    boot_info_t *bih = (boot_info_t *)BOOT_INFO_ADDR;
    bootinfo_build(bih, ehdr->e_entry, kernel_crc, modules_loaded,
                   start_ticks, &acpi);
    bootinfo_stamp_msr(kernel_crc, modules_loaded);
    splash_dash_item(16, "Boot Info", "0x7000", COL_OK);

    splash_dash_sep(18);

    cpu_print(&cpu);

    if (have_smbios) {
        smbios_print(&smbios);
    }

    rtc_time_t rtc;
    cmos_read_rtc(&rtc);
    cons_color("\n  Boot time: ", COL_LABEL);
    cmos_print(&rtc);
    cons_puts("\n");

    cons_color("  Total time: ", COL_LABEL);
    cons_dec32((uint32_t)((rdtsc64() - start_ticks) / 1000000));
    cons_color(" us  ", COL_OK);
    cons_color("RSDP: 0x", COL_DIM);
    cons_hex64(acpi.rsdp_addr);
    cons_color("  Kernel: 0x", COL_DIM);
    cons_hex64(ehdr->e_entry);
    cons_puts("\n");

    splash_dash_sep(23);

    boot_prompt();

    cons_color("\n", COL_DEFAULT);
    cons_set_cursor(24, 2);
    cons_color("Jumping to kernel with boot info at 0x7000\n", COL_HDR);

    __asm__ volatile("mov $0x90000, %%rsp\n" : : : "memory");
    __asm__ volatile(
        "xor %%rbp, %%rbp\n"
        "mov %0, %%rdi\n"
        "jmp *%1\n"
        : : "r"((uint64_t)BOOT_INFO_ADDR), "r"(ehdr->e_entry)
        : "rbp", "rdi"
    );

    while (1) __asm__ volatile("hlt");
}
