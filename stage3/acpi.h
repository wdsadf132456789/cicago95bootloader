#ifndef STAGE3_ACPI_H
#define STAGE3_ACPI_H

#include <stdint.h>

#define RSDP_SIG "RSD PTR "
#define ACPI_RSDP_ADDR_LEGACY 0xE0000
#define ACPI_RSDP_ADDR_END    0xFFFFF

typedef struct {
    char     sig[8];
    uint8_t  checksum;
    char     oemid[6];
    uint8_t  rev;
    uint32_t rsdt_addr;
} __attribute__((packed)) acpi_rsdp_v1_t;

typedef struct {
    acpi_rsdp_v1_t v1;
    uint32_t length;
    uint64_t xsdt_addr;
    uint8_t  ext_checksum;
    uint8_t  _rsv[3];
} __attribute__((packed)) acpi_rsdp_t;

typedef struct {
    char     sig[4];
    uint32_t length;
    uint8_t  rev;
    uint8_t  checksum;
    char     oemid[6];
    char     oem_table_id[8];
    uint32_t oem_rev;
    uint32_t creator_id;
    uint32_t creator_rev;
} __attribute__((packed)) acpi_sdt_header_t;

typedef struct {
    acpi_sdt_header_t hdr;
    uint32_t entries[];
} __attribute__((packed)) acpi_rsdt_t;

typedef struct {
    acpi_sdt_header_t hdr;
    uint64_t entries[];
} __attribute__((packed)) acpi_xsdt_t;

typedef struct {
    uint8_t  addr_space;
    uint8_t  bit_width;
    uint8_t  bit_offset;
    uint8_t  access_size;
    uint64_t address;
} __attribute__((packed)) acpi_gas_t;

typedef struct {
    acpi_sdt_header_t hdr;
    uint32_t local_ic_type;
    uint8_t  length;
    uint8_t  madt_rev;
    uint32_t lapic_addr;
    uint64_t flags;
} __attribute__((packed)) acpi_madt_t;

typedef struct {
    uint8_t  type;
    uint8_t  length;
    uint8_t  acpi_proc_id;
    uint8_t  apic_id;
    uint32_t flags;
} __attribute__((packed)) acpi_madt_lapic_t;

typedef struct {
    uint8_t  type;
    uint8_t  length;
    uint8_t  ioapic_id;
    uint8_t  _rsv;
    uint32_t ioapic_addr;
    uint32_t gsi_base;
} __attribute__((packed)) acpi_madt_ioapic_t;

typedef struct {
    uint64_t rsdp_addr;
    int      has_xsdt;
    int      has_rsdt;
    uint32_t table_count;
    char     oemid[7];
    uint32_t lapic_count;
    uint32_t ioapic_count;
    uint32_t lapic_addr;
} acpi_info_t;

int acpi_scan(acpi_info_t *info);

#endif