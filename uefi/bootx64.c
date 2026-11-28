/*
 * Minimal Chicago-95 UEFI loader.
 *
 * This is a freestanding PE32+ EFI application.  It intentionally defines the
 * small subset of UEFI types it needs so the build has no gnu-efi dependency.
 * It loads KERNEL.ELF and optional MODULEn.BIN files from the EFI system
 * partition, builds the shared boot_info_t ABI, installs identity/high-half
 * page tables, exits boot services, and jumps to the kernel entry point.
 *
 * The companion BIOS path uses stage1.bin/stage2.bin with the raw disk image.
 */

#include <stdint.h>
#include "boot/bootinfo.h"

typedef unsigned long efi_uintn;
typedef uint64_t efi_status;

#define EFI_SUCCESS            0ULL
#define EFI_LOAD_ERROR         1ULL
#define EFI_INVALID_PARAMETER  2ULL

#define EFI_ALLOCATE_ADDRESS   0U
#define EFI_LOADER_DATA        2U

#define EFI_FILE_MODE_READ     1ULL

#define E820_MAP_PHYS          0x8000ULL
#define UEFI_LOAD_ADDR         0x700000ULL
#define UEFI_LOAD_STRIDE       0x2000ULL
#define PML4_PHYS              0x90000ULL
#define PDPT_PHYS              0x91000ULL
#define PD_PHYS_BASE           0x92000ULL
#define UEFI_STACK_TOP         0x180000ULL
#define UEFI_PAGE_TABLE_PAGES  6U
#define UEFI_MODULE_PAGES      2U

typedef struct {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} efi_table_header_t;

typedef struct {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} efi_guid_t;

typedef struct {
    efi_table_header_t header;
    uint16_t *firmware_vendor;
    uint32_t firmware_revision;
    uint32_t pad0;
    void *console_in_handle;
    void *con_in;
    void *console_out_handle;
    void *con_out;
    void *stderr_handle;
    void *stderr;
    void *runtime_services;
    void *boot_services;
    efi_uintn number_of_table_entries;
    void *configuration_table;
} efi_system_table_t;

/* Entries through ExitBootServices are the only ones this loader needs. */
typedef struct {
    efi_table_header_t header;
    void *raise_tpl;
    void *restore_tpl;
    efi_status (*allocate_pages)(uint32_t, uint32_t, efi_uintn, uint64_t *);
    efi_status (*free_pages)(uint64_t, efi_uintn);
    efi_status (*get_memory_map)(efi_uintn *, void *, efi_uintn *,
                                 efi_uintn *, uint32_t *);
    efi_status (*allocate_pool)(uint32_t, efi_uintn, void **);
    void (*free_pool)(void *);
    void *create_event;
    void *set_timer;
    void *wait_for_event;
    void *signal_event;
    void *close_event;
    void *check_event;
    void *install_protocol_interface;
    void *reinstall_protocol_interface;
    void *uninstall_protocol_interface;
    efi_status (*handle_protocol)(void *, efi_guid_t *, void **);
    void *reserved;
    void *register_protocol_notify;
    void *locate_handle;
    void *locate_device_path;
    void *install_configuration_table;
    void *load_image;
    void *start_image;
    void *exit;
    void *unload_image;
    efi_status (*exit_boot_services)(void *, efi_uintn);
} efi_boot_services_t;

typedef struct {
    uint32_t revision;
    uint32_t pad0;
    void *parent_handle;
    void *system_table;
    void *device_handle;
    void *file_path;
    void *reserved;
    uint32_t load_options_size;
    uint32_t pad1;
    void *load_options;
    void *image_base;
    uint64_t image_size;
    uint32_t image_code_type;
    uint32_t image_data_type;
} efi_loaded_image_t;

typedef struct efi_file_protocol efi_file_protocol_t;

typedef struct {
    uint64_t revision;
    efi_status (*open_volume)(void *, efi_file_protocol_t **);
} efi_simple_fs_t;

struct efi_file_protocol {
    uint64_t revision;
    efi_status (*open)(efi_file_protocol_t *, efi_file_protocol_t **,
                       uint16_t *, uint64_t, uint64_t);
    efi_status (*close)(efi_file_protocol_t *);
    void *delete;
    efi_status (*read)(efi_file_protocol_t *, uint64_t *, void *);
    void *write;
    void *get_position;
    void *set_position;
    void *get_info;
    void *set_info;
    void *flush;
};

typedef struct {
    void *reset;
    efi_status (*output_string)(void *, uint16_t *);
} efi_text_out_t;

typedef struct {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t number_of_pages;
    uint64_t attribute;
} efi_memory_descriptor_t;

typedef struct {
    uint64_t base;
    uint64_t len;
    uint32_t type;
    uint32_t acpi_ext;
} __attribute__((packed)) efi_e820_entry_t;

typedef struct {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
} __attribute__((packed)) efi_elf64_ehdr_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
} __attribute__((packed)) efi_elf64_phdr_t;

_Static_assert(sizeof(efi_table_header_t) == 24, "UEFI table header ABI changed");
_Static_assert(sizeof(efi_system_table_t) == 120, "UEFI system table ABI changed");
_Static_assert(sizeof(efi_loaded_image_t) == 88, "UEFI loaded image ABI changed");
_Static_assert(sizeof(efi_simple_fs_t) == 16, "UEFI simple FS ABI changed");
_Static_assert(sizeof(efi_file_protocol_t) == 88, "UEFI file protocol ABI changed");
_Static_assert(sizeof(efi_memory_descriptor_t) == 40, "UEFI memory descriptor ABI changed");
_Static_assert(sizeof(efi_e820_entry_t) == 24, "E820 entry ABI changed");

static const efi_guid_t loaded_image_guid = {
    0x5B1B31A1U, 0x9562U, 0x11D2U,
    {0x8E, 0x3F, 0x00, 0xA0, 0xC9, 0x69, 0x72, 0x3B}
};

static const efi_guid_t simple_fs_guid = {
    0x0964D5A9U, 0x6A2FU, 0x11D3U,
    {0x98, 0xA9, 0x00, 0x08, 0xC7, 0xB3, 0xA5, 0xB6}
};

static void efi_memset(void *dst, uint8_t value, efi_uintn len) {
    uint8_t *p = (uint8_t *)dst;
    for (efi_uintn i = 0; i < len; i++) p[i] = value;
}

static void efi_memcpy(void *dst, const void *src, efi_uintn len) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (efi_uintn i = 0; i < len; i++) d[i] = s[i];
}

static void efi_puts(efi_text_out_t *con, const uint16_t *text) {
    if (con && con->output_string) con->output_string(con, (uint16_t *)text);
}

static uint64_t efi_rdtsc(void) {
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static uint32_t efi_crc32(const uint8_t *data, efi_uintn len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (efi_uintn i = 0; i < len; i++) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xEDB88320U & (uint32_t)(-(int32_t)(crc & 1)));
    }
    return ~crc;
}

static uint32_t cmd_append(char *dst, uint32_t pos, const char *src) {
    while (*src && pos + 1 < BOOT_CMDLINE_LEN) dst[pos++] = *src++;
    dst[pos] = 0;
    return pos;
}

static uint32_t cmd_append_u64(char *dst, uint32_t pos, uint64_t value) {
    char tmp[21];
    uint32_t n = 0;
    if (value == 0) {
        tmp[n++] = '0';
    } else {
        while (value && n < 20) {
            tmp[n++] = '0' + (uint32_t)(value % 10);
            value /= 10;
        }
    }
    for (uint32_t i = 0; i < n && pos + 1 < BOOT_CMDLINE_LEN; i++)
        dst[pos++] = tmp[n - 1 - i];
    dst[pos] = 0;
    return pos;
}

static uint32_t cmd_append_hex32(char *dst, uint32_t pos, uint32_t value) {
    static const char hex[] = "0123456789ABCDEF";
    char tmp[9];
    for (uint32_t i = 0; i < 8; i++)
        tmp[i] = hex[(value >> (28 - i * 4)) & 0xF];
    tmp[8] = 0;
    return cmd_append(dst, pos, tmp);
}

static efi_status read_file(efi_file_protocol_t *root, const uint16_t *name,
                            void *buffer, uint64_t capacity,
                            uint64_t *size_out) {
    efi_file_protocol_t *file = 0;
    uint64_t size = capacity;
    efi_status status;

    status = root->open(root, &file, (uint16_t *)name, EFI_FILE_MODE_READ, 0);
    if (status != EFI_SUCCESS || !file) return status;

    status = file->read(file, &size, buffer);
    file->close(file);
    if (status != EFI_SUCCESS) return status;
    if (size == 0 || size > capacity) return EFI_LOAD_ERROR;

    *size_out = size;
    return EFI_SUCCESS;
}

static uint8_t *load_pool_file(efi_boot_services_t *bs,
                               efi_file_protocol_t *root,
                               const uint16_t *name, uint64_t capacity,
                               uint64_t *size_out) {
    void *buffer = 0;
    uint64_t size = 0;

    if (bs->allocate_pool(EFI_LOADER_DATA, capacity, &buffer) != EFI_SUCCESS)
        return 0;
    if (read_file(root, name, buffer, capacity, &size) != EFI_SUCCESS) {
        bs->free_pool(buffer);
        return 0;
    }
    *size_out = size;
    return (uint8_t *)buffer;
}

static int load_kernel_elf(const uint8_t *elf, uint64_t elf_size,
                           uint64_t *entry_out, uint32_t *crc_out) {
    if (elf_size < sizeof(efi_elf64_ehdr_t)) return -1;
    if (elf[0] != 0x7F || elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F') return -1;
    if (elf[4] != 2 || elf[5] != 1) return -1;

    efi_elf64_ehdr_t ehdr;
    efi_memcpy(&ehdr, elf, sizeof(ehdr));
    if (ehdr.type != 2 || ehdr.machine != 0x3E || ehdr.phnum == 0) return -1;
    if (ehdr.phoff + (uint64_t)ehdr.phnum * ehdr.phentsize > elf_size) return -1;

    for (uint16_t i = 0; i < ehdr.phnum; i++) {
        const uint8_t *raw = elf + ehdr.phoff + (uint64_t)i * ehdr.phentsize;
        efi_elf64_phdr_t phdr;
        efi_memcpy(&phdr, raw, sizeof(phdr));
        if (phdr.type != 1) continue;
        if (phdr.offset + phdr.filesz > elf_size) return -1;

        uint64_t destination = phdr.paddr;
        if (destination == 0) {
            destination = phdr.vaddr >= 0xFFFFFFFF80000000ULL
                ? phdr.vaddr - 0xFFFFFFFF80000000ULL
                : phdr.vaddr;
        }
        if (destination + phdr.memsz > 0x660000ULL) return -1;

        efi_memcpy((void *)(unsigned long)destination, elf + phdr.offset,
                   (efi_uintn)phdr.filesz);
        if (phdr.memsz > phdr.filesz)
            efi_memset((void *)(unsigned long)(destination + phdr.filesz), 0,
                       (efi_uintn)(phdr.memsz - phdr.filesz));
    }

    *entry_out = ehdr.entry;
    *crc_out = efi_crc32(elf, 512);
    return 0;
}

static uint32_t load_modules(efi_boot_services_t *bs,
                             efi_file_protocol_t *root, boot_info_t *bi) {
    static const uint16_t module_names[BOOT_MODULE_MAX][14] = {
        u"MODULE0.BIN",  u"MODULE1.BIN",  u"MODULE2.BIN",  u"MODULE3.BIN",
        u"MODULE4.BIN",  u"MODULE5.BIN",  u"MODULE6.BIN",  u"MODULE7.BIN",
        u"MODULE8.BIN",  u"MODULE9.BIN",  u"MODULE10.BIN", u"MODULE11.BIN",
        u"MODULE12.BIN", u"MODULE13.BIN", u"MODULE14.BIN", u"MODULE15.BIN"
    };
    uint32_t count = 0;

    for (uint32_t index = 0; index < BOOT_MODULE_MAX; index++) {
        uint64_t address = UEFI_LOAD_ADDR + index * UEFI_LOAD_STRIDE;
        uint64_t size = 0;
        uint8_t *payload;
        uint32_t magic, version;
        boot_module_t *desc;

        if (bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
                               UEFI_MODULE_PAGES, &address) != EFI_SUCCESS)
            continue;

        if (read_file(root, module_names[index],
                      (void *)(unsigned long)address,
                      UEFI_MODULE_PAGES * 4096ULL, &size) != EFI_SUCCESS ||
            size < 56) {
            bs->free_pages(address, UEFI_MODULE_PAGES);
            continue;
        }

        payload = (uint8_t *)(unsigned long)address;
        magic = (uint32_t)payload[0] | ((uint32_t)payload[1] << 8) |
                ((uint32_t)payload[2] << 16) | ((uint32_t)payload[3] << 24);
        version = (uint32_t)payload[4] | ((uint32_t)payload[5] << 8) |
                  ((uint32_t)payload[6] << 16) | ((uint32_t)payload[7] << 24);
        if (magic != MODULE_IMAGE_MAGIC || version != MODULE_IMAGE_VERSION) {
            bs->free_pages(address, UEFI_MODULE_PAGES);
            continue;
        }

        desc = &bi->modules[count];
        for (uint32_t i = 0; i < BOOT_MODULE_NAME_LEN - 1; i++) {
            desc->name[i] = (char)payload[8 + i];
            if (!desc->name[i]) break;
        }
        desc->name[BOOT_MODULE_NAME_LEN - 1] = 0;
        desc->address = address;
        desc->size = size;
        desc->entry = 0;
        desc->flags = (uint32_t)payload[48] | ((uint32_t)payload[49] << 8) |
                      ((uint32_t)payload[50] << 16) | ((uint32_t)payload[51] << 24);
        desc->reserved = 0;
        count++;
    }

    return count;
}

static uint32_t build_e820_map(efi_boot_services_t *bs) {
    efi_uintn map_size = 0;
    efi_uintn map_key = 0;
    efi_uintn desc_size = 0;
    uint32_t desc_version = 0;
    void *map = 0;
    efi_e820_entry_t *out = (efi_e820_entry_t *)(unsigned long)E820_MAP_PHYS;
    uint32_t count = 0;

    if (bs->get_memory_map(&map_size, 0, &map_key, &desc_size, &desc_version) != EFI_SUCCESS)
        return 0;
    if (bs->allocate_pool(EFI_LOADER_DATA, map_size + 256, &map) != EFI_SUCCESS)
        return 0;
    if (bs->get_memory_map(&map_size, map, &map_key, &desc_size, &desc_version) != EFI_SUCCESS) {
        bs->free_pool(map);
        return 0;
    }
    if (desc_size == 0) desc_size = sizeof(efi_memory_descriptor_t);

    efi_memset(out, 0, 256 * sizeof(*out));
    for (efi_uintn offset = 0; offset + desc_size <= map_size && count < 256;
         offset += desc_size) {
        efi_memory_descriptor_t *entry =
            (efi_memory_descriptor_t *)((uint8_t *)map + offset);
        if (entry->number_of_pages == 0) continue;

        out[count].base = entry->physical_start;
        out[count].len = entry->number_of_pages * 4096ULL;
        out[count].type = entry->type == 7 ? 1U : 2U;
        out[count].acpi_ext = 0;
        count++;
    }
    bs->free_pool(map);
    return count;
}

/*
 * Return a map key for ExitBootServices.  The map buffer is deliberately left
 * allocated: freeing it would change the map and invalidate the key.
 */
static efi_uintn final_memory_map_key(efi_boot_services_t *bs) {
    efi_uintn map_size = 0;
    efi_uintn map_key = 0;
    efi_uintn desc_size = 0;
    uint32_t desc_version = 0;
    void *map = 0;

    if (bs->get_memory_map(&map_size, 0, &map_key, &desc_size, &desc_version) != EFI_SUCCESS)
        return map_key;
    if (bs->allocate_pool(EFI_LOADER_DATA, map_size + 256, &map) != EFI_SUCCESS)
        return map_key;
    bs->get_memory_map(&map_size, map, &map_key, &desc_size, &desc_version);
    return map_key;
}

static void setup_page_tables(void) {
    uint64_t *pml4 = (uint64_t *)(unsigned long)PML4_PHYS;
    uint64_t *pdpt = (uint64_t *)(unsigned long)PDPT_PHYS;

    efi_memset((void *)(unsigned long)PML4_PHYS, 0,
               UEFI_PAGE_TABLE_PAGES * 4096U);

    pml4[0] = PDPT_PHYS | 3U;     /* identity map low 4 GiB */
    pml4[511] = PDPT_PHYS | 3U;   /* higher-half map to the same memory */

    for (uint64_t i = 0; i < 4; i++) {
        uint64_t *pd = (uint64_t *)(unsigned long)(PD_PHYS_BASE + i * 4096);
        uint64_t entry = (PD_PHYS_BASE + i * 4096) | 3U;

        /* Identity PDPT slots 0-3 and higher-half PDPT slots 510-513 both
         * point at the same page directories, so 0xffffffff80000000+phys
         * aliases physical memory exactly like the BIOS stage2 map. */
        pdpt[i] = entry;
        pdpt[510 + i] = entry;

        for (uint64_t j = 0; j < 512; j++)
            pd[j] = ((i << 30) | (j << 21)) | 0x83U;
    }
}

efi_status efi_main(void *image_handle, void *system_table_ptr) {
    efi_system_table_t *system_table = (efi_system_table_t *)system_table_ptr;
    efi_boot_services_t *bs;
    efi_text_out_t *con;
    efi_loaded_image_t *loaded_image = 0;
    efi_simple_fs_t *fs = 0;
    efi_file_protocol_t *root = 0;
    uint8_t *elf = 0;
    uint64_t elf_size = 0;
    uint64_t kernel_entry = 0;
    uint32_t kernel_crc = 0;
    uint64_t boot_info_phys = BOOT_INFO_ADDR;
    uint64_t page_tables_phys = PML4_PHYS;
    boot_info_t *bi;
    uint32_t module_count;
    uint32_t e820_count;
    efi_uintn map_key;
    uint64_t boot_ticks;
    uint32_t pos = 0;

    if (!system_table) return EFI_INVALID_PARAMETER;
    bs = (efi_boot_services_t *)system_table->boot_services;
    con = (efi_text_out_t *)system_table->con_out;
    if (!bs) return EFI_INVALID_PARAMETER;

    boot_ticks = efi_rdtsc();
    efi_puts(con, u"Chicago-95 UEFI loader\r\n");

    if (bs->handle_protocol(image_handle, (efi_guid_t *)&loaded_image_guid,
                            (void **)&loaded_image) != EFI_SUCCESS ||
        !loaded_image || !loaded_image->device_handle) {
        efi_puts(con, u"Loaded image protocol not found\r\n");
        return EFI_LOAD_ERROR;
    }
    if (bs->handle_protocol(loaded_image->device_handle,
                            (efi_guid_t *)&simple_fs_guid,
                            (void **)&fs) != EFI_SUCCESS || !fs) {
        efi_puts(con, u"Simple file system not found\r\n");
        return EFI_LOAD_ERROR;
    }
    if (fs->open_volume(fs, &root) != EFI_SUCCESS || !root) {
        efi_puts(con, u"Cannot open EFI root directory\r\n");
        return EFI_LOAD_ERROR;
    }

    elf = load_pool_file(bs, root, u"KERNEL.ELF", 1024 * 1024, &elf_size);
    if (!elf) {
        efi_puts(con, u"KERNEL.ELF not found\r\n");
        return EFI_LOAD_ERROR;
    }
    if (load_kernel_elf(elf, elf_size, &kernel_entry, &kernel_crc) != 0) {
        efi_puts(con, u"KERNEL.ELF is not a valid ELF64 image\r\n");
        bs->free_pool(elf);
        return EFI_LOAD_ERROR;
    }
    bs->free_pool(elf);

    if (bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA, 1,
                           &boot_info_phys) != EFI_SUCCESS) {
        efi_puts(con, u"Cannot reserve boot info page\r\n");
        return EFI_LOAD_ERROR;
    }
    if (bs->allocate_pages(EFI_ALLOCATE_ADDRESS, EFI_LOADER_DATA,
                           UEFI_PAGE_TABLE_PAGES,
                           &page_tables_phys) != EFI_SUCCESS) {
        efi_puts(con, u"Cannot reserve UEFI page tables\r\n");
        return EFI_LOAD_ERROR;
    }

    bi = (boot_info_t *)(unsigned long)boot_info_phys;
    efi_memset(bi, 0, sizeof(*bi));

    module_count = load_modules(bs, root, bi);
    e820_count = build_e820_map(bs);

    bi->magic = BOOT_INFO_MAGIC;
    bi->version = BOOT_INFO_VERSION;
    bi->flags = BOOT_FLAG_KERNEL_VALID | BOOT_FLAG_E820;
    if (module_count) bi->flags |= BOOT_FLAG_MODULES;
    bi->e820_addr = E820_MAP_PHYS;
    bi->e820_count = e820_count;
    bi->kernel_entry = kernel_entry;
    bi->kernel_crc = kernel_crc;
    bi->module_count = module_count;
    bi->module_list = (uint64_t)(unsigned long)&bi->modules;
    bi->boot_ticks = efi_rdtsc() - boot_ticks;

    {
        const char id[] = "Chicago-95/UEFI v1.0";
        for (uint32_t i = 0; i < sizeof(id) - 1; i++) bi->boot_loader[i] = id[i];
    }

    pos = cmd_append(bi->kernel_cmdline, pos, "loader=Chicago95/UEFI");
    pos = cmd_append(bi->kernel_cmdline, pos, " modules=");
    pos = cmd_append_u64(bi->kernel_cmdline, pos, module_count);
    pos = cmd_append(bi->kernel_cmdline, pos, " crc=0x");
    pos = cmd_append_hex32(bi->kernel_cmdline, pos, kernel_crc);
    pos = cmd_append(bi->kernel_cmdline, pos, " ticks=");
    pos = cmd_append_u64(bi->kernel_cmdline, pos, boot_ticks);
    pos = cmd_append(bi->kernel_cmdline, pos, " names=");
    for (uint32_t i = 0; i < module_count; i++) {
        if (i) pos = cmd_append(bi->kernel_cmdline, pos, ",");
        pos = cmd_append(bi->kernel_cmdline, pos, bi->modules[i].name);
    }

    efi_puts(con, u"Starting Chicago-95 kernel...\r\n");
    map_key = final_memory_map_key(bs);
    if (bs->exit_boot_services(image_handle, map_key) != EFI_SUCCESS)
        return EFI_LOAD_ERROR;

    setup_page_tables();

    __asm__ volatile(
        "cli\n"
        "mov %0, %%cr3\n"
        "mov %1, %%rsp\n"
        "xor %%rbp, %%rbp\n"
        "mov %2, %%rdi\n"
        "jmp *%3\n"
        :
        : "r"(page_tables_phys), "r"((uint64_t)UEFI_STACK_TOP),
          "r"((uint64_t)boot_info_phys), "r"(kernel_entry)
        : "rbp", "rdi", "memory"
    );

    return EFI_LOAD_ERROR;
}
