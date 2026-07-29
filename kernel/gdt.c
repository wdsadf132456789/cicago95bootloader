#include "gdt.h"
#include "kernel.h"

gdt_entry_t gdt[7];
gdt_ptr_t gdt_ptr;
tss_t tss;

static void gdt_set_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low = base & 0xFFFF;
    gdt[num].base_mid = (base >> 16) & 0xFF;
    gdt[num].base_high = (base >> 24) & 0xFF;
    gdt[num].limit_low = limit & 0xFFFF;
    gdt[num].granularity = ((limit >> 16) & 0x0F);
    gdt[num].granularity |= (gran & 0xF0);
    gdt[num].access = access;
}

void gdt_init(void) {
    gdt_ptr.limit = sizeof(gdt_entry_t) * 7 - 1;
    gdt_ptr.base = (uint64_t)&gdt;

    gdt_set_gate(0, 0, 0, 0, 0);
    gdt_set_gate(1, 0, 0xFFFFF, 0x9A, 0xA0);
    gdt_set_gate(2, 0, 0xFFFFF, 0x92, 0xC0);
    gdt_set_gate(3, 0, 0xFFFFF, 0xFA, 0xA0);
    gdt_set_gate(4, 0, 0xFFFFF, 0xF2, 0xC0);

    __builtin_memset(&tss, 0, sizeof(tss_t));
    tss.iopb_offset = sizeof(tss_t);

    uint8_t *tss_desc = (uint8_t *)&gdt[5];
    __builtin_memset(tss_desc, 0, 16);
    uint64_t base = (uint64_t)&tss;
    uint32_t limit = sizeof(tss_t) - 1;

    tss_desc[0] = limit & 0xFF;
    tss_desc[1] = (limit >> 8) & 0xFF;
    tss_desc[2] = base & 0xFF;
    tss_desc[3] = (base >> 8) & 0xFF;
    tss_desc[4] = (base >> 16) & 0xFF;
    tss_desc[5] = 0x89;
    tss_desc[6] = ((limit >> 16) & 0x0F);
    tss_desc[7] = (base >> 24) & 0xFF;
    tss_desc[8] = (base >> 32) & 0xFF;
    tss_desc[9] = (base >> 40) & 0xFF;
    tss_desc[10] = (base >> 48) & 0xFF;

    gdt_flush();
    tss_flush();
}

void tss_set_rsp0(uint64_t rsp0) {
    tss.rsp0 = rsp0;
}
