#ifndef STAGE3_ELF_H
#define STAGE3_ELF_H

#include <stdint.h>

#define ELF_MAGIC   0x464C457F
#define ELF64_CLASS 2
#define ELF64_ENDIAN 1
#define KERNEL_BASE 0xFFFFFFFF80000000ULL

typedef struct {
    uint32_t magic;
    uint8_t  ei_class;
    uint8_t  ei_data;
    uint8_t  ei_version;
    uint8_t  ei_osabi;
    uint8_t  ei_abiversion;
    uint8_t  ei_pad[7];
    uint16_t e_type;
    uint16_t e_machine;
    uint32_t e_version;
    uint64_t e_entry;
    uint64_t e_phoff;
    uint64_t e_shoff;
    uint32_t e_flags;
    uint16_t e_ehsize;
    uint16_t e_phentsize;
    uint16_t e_phnum;
    uint16_t e_shentsize;
    uint16_t e_shnum;
    uint16_t e_shstrndx;
} __attribute__((packed)) elf64_ehdr_t;

typedef struct {
    uint32_t p_type;
    uint32_t p_flags;
    uint64_t p_offset;
    uint64_t p_vaddr;
    uint64_t p_paddr;
    uint64_t p_filesz;
    uint64_t p_memsz;
    uint64_t p_align;
} __attribute__((packed)) elf64_phdr_t;

#define PT_LOAD 1

int  elf_verify(const elf64_ehdr_t *hdr);
int  elf_load_segment(const elf64_phdr_t *phdr, const uint8_t *elf_base);
uint8_t *elf_find_in_memory(void);
uint32_t elf_load_all(const uint8_t *elf_base);

#endif