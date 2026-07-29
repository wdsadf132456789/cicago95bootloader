#include <stdint.h>
#include "console.h"
#include "elf.h"

int elf_verify(const elf64_ehdr_t *hdr) {
    if (hdr->magic != ELF_MAGIC) return 0;
    if (hdr->ei_class != ELF64_CLASS) return 0;
    if (hdr->ei_data != ELF64_ENDIAN) return 0;
    if (hdr->e_type != 2) return 0;
    if (hdr->e_machine != 0x3E) return 0;
    return 1;
}

int elf_load_segment(const elf64_phdr_t *phdr, const uint8_t *elf_base) {
    if (phdr->p_type != PT_LOAD) return 1;
    const uint8_t *src = elf_base + phdr->p_offset;
    uint64_t phys_addr;
    if (phdr->p_paddr != 0)
        phys_addr = phdr->p_paddr;
    else if (phdr->p_vaddr >= KERNEL_BASE)
        phys_addr = phdr->p_vaddr - KERNEL_BASE;
    else
        phys_addr = phdr->p_vaddr;
    uint8_t *dst = (uint8_t *)phys_addr;
    for (uint64_t i = 0; i < phdr->p_filesz; i++)
        dst[i] = src[i];
    for (uint64_t i = phdr->p_filesz; i < phdr->p_memsz; i++)
        dst[i] = 0;
    return 0;
}

uint8_t *elf_find_in_memory(void) {
    cons_color("  Scanning for ELF...\n", COL_DEFAULT);
    uint8_t *bases[] = {
        (uint8_t *)0x10000, (uint8_t *)0x100000,
        (uint8_t *)0x80000, (uint8_t *)0x20000
    };
    for (int i = 0; i < 4; i++) {
        if (elf_verify((elf64_ehdr_t *)bases[i])) {
            cons_color("  ELF64 found at ", COL_OK);
            cons_hex32((uint32_t)(uint64_t)bases[i]);
            cons_puts("\n");
            return bases[i];
        }
    }
    return (uint8_t *)0;
}

uint32_t elf_load_all(const uint8_t *elf_base) {
    elf64_ehdr_t *ehdr = (elf64_ehdr_t *)elf_base;
    elf64_phdr_t *phdr = (elf64_phdr_t *)(elf_base + ehdr->e_phoff);
    uint32_t count = 0;
    for (uint16_t i = 0; i < ehdr->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            cons_color("    Segment ", COL_DEFAULT);
            cons_dec32(i);
            cons_color(": vaddr=0x", COL_DEFAULT);
            cons_hex64(phdr[i].p_vaddr);
            cons_color(" size=0x", COL_DEFAULT);
            cons_hex64(phdr[i].p_memsz);
            cons_color(" -> paddr=0x", COL_DEFAULT);
            elf_load_segment(&phdr[i], elf_base);
            uint64_t pa = phdr[i].p_paddr ? phdr[i].p_paddr :
                (phdr[i].p_vaddr >= KERNEL_BASE ?
                 phdr[i].p_vaddr - KERNEL_BASE : phdr[i].p_vaddr);
            cons_hex64(pa);
            cons_puts("\n");
            count++;
        }
    }
    return count;
}
