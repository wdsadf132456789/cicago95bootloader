[bits 16]

EXTERN stage2_entry

; Stage2 signature for stage1 to verify
dw 0x7A3B

entry_rm:
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00

    ; Save boot drive from DL
    mov [boot_drive_save], dl

    ; Build minimal GDT at 0x8000
    mov di, 0x8000

    ; Null descriptor (selector 0x00)
    xor eax, eax
    stosd
    stosd

    ; Code segment descriptor (selector 0x08): base=0, limit=4GB, DPL=0, 32-bit, code
    mov eax, 0x0000FFFF
    stosd
    mov eax, 0x00CF9A00
    stosd

    ; Data segment descriptor (selector 0x10): base=0, limit=4GB, DPL=0, 32-bit, data RW
    mov eax, 0x0000FFFF
    stosd
    mov eax, 0x00CF9200
    stosd

    ; GDT pointer at 0x7E00
    mov word [0x7E00], 23     ; GDT byte limit - 1
    mov dword [0x7E02], 0x8000 ; GDT linear base
    lgdt [0x7E00]

    ; Enable protected mode (CR0.PE = 1)
    mov eax, cr0
    or al, 1
    mov cr0, eax

    ; Far jump to 32-bit code: load CS with selector 0x08
    db 0x66
    db 0xEA
    dd pm_entry
    dw 0x08

boot_drive_save: db 0

[bits 32]
pm_entry:
    ; Set up segment registers for 32-bit flat mode
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; Pass boot drive as first argument
    xor eax, eax
    mov al, [boot_drive_save]
    push eax
    call stage2_entry
    add esp, 4

    ; Should never return
    cli
    hlt
