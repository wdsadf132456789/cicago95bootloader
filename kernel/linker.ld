/**
 * Chicago-95 Kernel Linker Script
 * Linked at 0xFFFFFFFF80000000 (higher-half)
 * Physical load at 0x200000 via Stage 2, mapped by VMM: KERNEL_BASE -> phys 0
 */

ENTRY(_start)
SECTIONS
{
    . = 0xFFFFFFFF80000000 + 0x200000;

    .text : AT(ADDR(.text) - 0xFFFFFFFF80000000) {
        *(.text)
        *(.text.*)
    }

    .rodata : AT(ADDR(.rodata) - 0xFFFFFFFF80000000) {
        *(.rodata)
        *(.rodata.*)
    }

    .data : AT(ADDR(.data) - 0xFFFFFFFF80000000) {
        *(.data)
        *(.data.*)
    }

    .bss : AT(ADDR(.bss) - 0xFFFFFFFF80000000) {
        _bss_start = .;
        *(.bss)
        *(COMMON)
        *(.bss.*)
        _bss_end = .;
    }

    _kernel_start = . - 0xFFFFFFFF80000000;
    _kernel_end = . - 0xFFFFFFFF80000000;

    /DISCARD/ : {
        *(.comment)
        *(.note.*)
        *(.eh_frame)
    }
}
