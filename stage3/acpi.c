#include <stdint.h>
#include "console.h"
#include "acpi.h"

static uint8_t acpi_checksum(const uint8_t *data, uint32_t len) {
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

static int acpi_find_rsdp(acpi_rsdp_t *out) {
    uint8_t *scan = (uint8_t *)ACPI_RSDP_ADDR_LEGACY;
    while ((uint64_t)scan < ACPI_RSDP_ADDR_END) {
        int found = 1;
        for (int i = 0; i < 8; i++)
            if (scan[i] != RSDP_SIG[i]) { found = 0; break; }
        if (found) {
            for (int i = 0; i < 8; i++) ((uint8_t *)out)[i] = scan[i];
            out->v1.checksum = scan[8];
            for (int i = 0; i < 6; i++) out->v1.oemid[i] = scan[9 + i];
            out->v1.rev = scan[15];
            out->v1.rsdt_addr = *(uint32_t *)(scan + 16);
            if (acpi_checksum(scan, 20)) { scan += 16; continue; }
            if (out->v1.rev >= 2) {
                out->length = *(uint32_t *)(scan + 20);
                out->xsdt_addr = *(uint64_t *)(scan + 24);
                out->ext_checksum = scan[32];
                if (acpi_checksum(scan, out->length)) { scan += 16; continue; }
            }
            return 1;
        }
        scan += 16;
    }
    return 0;
}

static void acpi_scan_madt(acpi_info_t *info, const acpi_sdt_header_t *hdr) {
    const acpi_madt_t *madt = (const acpi_madt_t *)hdr;
    info->lapic_addr = madt->lapic_addr;
    const uint8_t *ptr = (const uint8_t *)madt + sizeof(acpi_madt_t);
    const uint8_t *end = (const uint8_t *)madt + madt->hdr.length;
    while (ptr < end) {
        uint8_t type = ptr[0];
        uint8_t len = ptr[1];
        if (len < 2) break;
        if (type == 0) info->lapic_count++;
        if (type == 1) info->ioapic_count++;
        ptr += len;
    }
}

int acpi_scan(acpi_info_t *info) {
    cons_color("  Scanning ACPI tables...\n", COL_DEFAULT);
    acpi_rsdp_t rsdp;
    if (!acpi_find_rsdp(&rsdp)) {
        cons_color("    RSDP not found\n", COL_WARN);
        return 0;
    }
    info->rsdp_addr = (uint64_t)ACPI_RSDP_ADDR_LEGACY;
    info->oemid[0] = 0;
    info->lapic_count = 0;
    info->ioapic_count = 0;
    info->lapic_addr = 0;

    if (rsdp.v1.rev >= 2 && rsdp.xsdt_addr) {
        info->has_xsdt = 1;
        info->has_rsdt = 0;
        const acpi_xsdt_t *xsdt = (const acpi_xsdt_t *)(uint64_t)rsdp.xsdt_addr;
        info->table_count = (xsdt->hdr.length - sizeof(acpi_sdt_header_t)) / 8;
        for (int i = 0; i < 6; i++) info->oemid[i] = xsdt->hdr.oemid[i];
        info->oemid[6] = 0;
        cons_color("    XSDT at 0x", COL_OK);
        cons_hex64(rsdp.xsdt_addr);
        cons_color(" (", COL_OK);
        cons_dec32(info->table_count);
        cons_color(" tables)\n", COL_OK);
        for (uint32_t i = 0; i < info->table_count; i++) {
            const acpi_sdt_header_t *tbl = (const acpi_sdt_header_t *)(uint64_t)xsdt->entries[i];
            if (tbl->sig[0] == 'A' && tbl->sig[1] == 'P' &&
                tbl->sig[2] == 'I' && tbl->sig[3] == 'C')
                acpi_scan_madt(info, tbl);
        }
    } else if (rsdp.v1.rsdt_addr) {
        info->has_xsdt = 0;
        info->has_rsdt = 1;
        const acpi_rsdt_t *rsdt = (const acpi_rsdt_t *)(uint64_t)rsdp.v1.rsdt_addr;
        info->table_count = (rsdt->hdr.length - sizeof(acpi_sdt_header_t)) / 4;
        for (int i = 0; i < 6; i++) info->oemid[i] = rsdt->hdr.oemid[i];
        info->oemid[6] = 0;
        cons_color("    RSDT at 0x", COL_OK);
        cons_hex32(rsdp.v1.rsdt_addr);
        cons_color(" (", COL_OK);
        cons_dec32(info->table_count);
        cons_color(" tables)\n", COL_OK);
        for (uint32_t i = 0; i < info->table_count; i++) {
            const acpi_sdt_header_t *tbl = (const acpi_sdt_header_t *)(uint64_t)rsdt->entries[i];
            if (tbl->sig[0] == 'A' && tbl->sig[1] == 'P' &&
                tbl->sig[2] == 'I' && tbl->sig[3] == 'C')
                acpi_scan_madt(info, tbl);
        }
    }

    if (info->lapic_count > 0) {
        cons_color("    MADT: ", COL_DEFAULT);
        cons_dec32(info->lapic_count);
        cons_color(" LAPICs, ", COL_DEFAULT);
        cons_dec32(info->ioapic_count);
        cons_color(" I/O APICs, LAPIC addr 0x", COL_DEFAULT);
        cons_hex32(info->lapic_addr);
        cons_puts("\n");
    }

    cons_color("    OEM: ", COL_DEFAULT);
    cons_color(info->oemid, COL_OK);
    cons_puts("\n");
    return 1;
}
