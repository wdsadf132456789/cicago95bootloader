#include "elf.h"
#include "kernel.h"

int elf_validate(elf64_header_t *header) {
    if (header->e_ident[0] != 0x7F) return 0;
    if (header->e_ident[1] != 'E') return 0;
    if (header->e_ident[2] != 'L') return 0;
    if (header->e_ident[3] != 'F') return 0;
    if (header->e_ident[4] != 2) return 0;
    if (header->e_ident[5] != 1) return 0;
    if (header->e_type != 2) return 0;
    if (header->e_machine != 0x3E) return 0;
    return 1;
}

uint64_t elf_load(elf64_header_t *header) {
    elf64_phdr_t *phdr = (elf64_phdr_t *)((uint8_t *)header + header->e_phoff);

    for (int i = 0; i < header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {
            uint64_t dest = phdr[i].p_vaddr;
            uint64_t src_offset = phdr[i].p_offset;
            uint64_t filesz = phdr[i].p_filesz;
            uint64_t memsz = phdr[i].p_memsz;

            uint8_t *src = (uint8_t *)header + src_offset;
            uint8_t *dst = (uint8_t *)dest;

            for (uint64_t j = 0; j < filesz; j++) {
                dst[j] = src[j];
            }
            for (uint64_t j = filesz; j < memsz; j++) {
                dst[j] = 0;
            }
        }
    }

    return header->e_entry;
}
