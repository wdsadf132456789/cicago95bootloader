#ifndef CHICAGO95_BOOTINFO_H
#define CHICAGO95_BOOTINFO_H

#include <stdint.h>

#define BOOT_INFO_ADDR  0x7000ULL
#define MSR_ADDR        0x700

#define BOOT_INFO_MAGIC   0x46494243U  /* "CBIF" */
#define BOOT_INFO_VERSION 2U

/* boot_info_t.flags */
#define BOOT_FLAG_KERNEL_VALID 0x01U
#define BOOT_FLAG_MODULES      0x02U
#define BOOT_FLAG_ACPI         0x04U
#define BOOT_FLAG_E820         0x08U

#define BOOT_MODULE_NAME_LEN 32
#define BOOT_MODULE_MAX      16

/* boot_module_t.flags */
#define BOOT_MODULE_FLAG_EXECUTABLE 0x01U
#define BOOT_MODULE_FLAG_DATA      0x02U

/*
 * Minimal on-disk/in-memory module image header.  tools/gen_modules.py emits
 * one of these at the start of every module payload; stage 3 validates it and
 * copies flags/entry into boot_module_t, and the kernel init system validates
 * it again before accepting the module.
 */
#define MODULE_IMAGE_MAGIC   0x31444F4DU  /* "MOD1" */
#define MODULE_IMAGE_VERSION 1U

typedef struct {
    uint32_t magic;
    uint32_t version;
    char     name[BOOT_MODULE_NAME_LEN];
    uint64_t entry;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed)) module_image_header_t;

/*
 * A boot module descriptor. Stage 3 fills one per module it loads from the
 * module directory, the boot-manager stages display them, and the kernel init
 * system consumes them during boot.
 */
typedef struct {
    char     name[BOOT_MODULE_NAME_LEN];
    uint64_t address;
    uint64_t size;
    uint64_t entry;
    uint32_t flags;
    uint32_t reserved;
} __attribute__((packed)) boot_module_t;

/*
 * Single ABI shared by stage 3, stages 5-100, and the kernel.
 * The structure lives at BOOT_INFO_ADDR and is passed to the kernel in RDI.
 */
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t flags;
    uint32_t reserved0;

    uint64_t e820_addr;
    uint32_t e820_count;
    uint32_t reserved1;

    uint64_t kernel_entry;
    uint32_t kernel_crc;
    uint32_t module_count;
    uint64_t module_list;
    boot_module_t modules[BOOT_MODULE_MAX];

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

_Static_assert(sizeof(boot_module_t) == 64, "boot_module_t ABI changed");
_Static_assert(sizeof(module_image_header_t) == 56, "module image header ABI changed");
_Static_assert(sizeof(boot_info_t) == 1184, "boot_info_t ABI changed");

#endif
