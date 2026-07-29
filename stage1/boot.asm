[bits 16]
[org 0x7C00]

STAGE2_OFFSET        equ 0x0600
STAGE2_SECTOR_COUNT  equ 64
STAGE2_LOAD_OFF      equ 0x0600
E820_MAP_OFFSET      equ 0x8000
E820_MAP_ENTRIES     equ 0x8002
DAP_OFFSET           equ 0x9000
PART_TABLE_OFFSET    equ 0x9100
STAGE2_SIGNATURE     equ 0x7A3B

; BPB for FAT12/16 BIOS compatibility
    jmp short start
    nop
bpb_oem_label:           db 'CHICAGO'
bpb_bytes_per_sector:    dw 512
bpb_sectors_per_cluster: db 1
bpb_reserved_sectors:    dw 1
bpb_num_fats:            db 2
bpb_root_dir_entries:    dw 224
bpb_total_sectors_16:    dw 2880
bpb_media_type:          db 0xF0
bpb_sectors_per_fat_16:  dw 9
bpb_sectors_per_track:   dw 18
bpb_num_heads:           dw 2
bpb_hidden_sectors:      dd 0
bpb_total_sectors_32:    dd 0
bpb_drive_number:        db 0x80
bpb_reserved:            db 0
bpb_signature:           db 0x29
bpb_volume_serial:       dd 0xDEADBEEF
bpb_volume_label:        db 'CHICAGO-95'
bpb_filesystem_type:     db 'FAT12   '

start:
    mov [bp_drive], dl
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7C00
    sti

    mov si, 0x7C00
    mov di, STAGE2_OFFSET
    mov cx, 256
    cld
    rep movsw
    jmp 0x0000:relocated_start

relocated_start:
    xor ax, ax
    mov ds, ax
    mov es, ax

    mov si, msg_boot
    call print_string_16

    call detect_memory_e820
    call enable_a20
    call read_mbr
    call load_stage2

    jmp 0x0000:STAGE2_LOAD_OFF

; E820 Memory Detection
detect_memory_e820:
    push es
    push di
    push bp
    mov di, E820_MAP_OFFSET
    xor bp, bp
    mov edx, 0x534D4150
    xor ebx, ebx
.e820_loop:
    mov eax, 0xE820
    mov ecx, 24
    int 0x15
    jc .e820_done
    cmp eax, 0x534D4150
    jne .e820_done
    cmp dword [di], 0
    jne .e820_valid
    cmp dword [di + 4], 0
    je .e820_next
.e820_valid:
    inc bp
    add di, 24
.e820_next:
    cmp ebx, 0
    je .e820_done
    jmp .e820_loop
.e820_done:
    mov [E820_MAP_ENTRIES], bp
    pop bp
    pop di
    pop es
    ret

; A20 Line Enable
enable_a20:
    mov ax, 0x2401
    int 0x15
    jnc .a20_done
    in al, 0x92
    or al, 2
    out 0x92, al
.a20_done:
    ret

; Read MBR Partition Table
read_mbr:
    mov si, 0x7C00 + 0x1BE
    mov cx, 4
    xor bx, bx
.parse:
    cmp byte [si], 0x80
    je .found
    cmp byte [si + 4], 0
    je .next
    ; Copy first non-empty non-active partition we find (if no active found)
    cmp byte [part_active], 1
    je .next
    mov di, PART_TABLE_OFFSET
    mov cx, 16
    push si
    rep movsb
    pop si
    jmp .next2
.found:
    mov byte [part_active], 1
    mov di, PART_TABLE_OFFSET
    mov cx, 16
    push si
    rep movsb
    pop si
.next2:
    mov cx, 4
.next:
    add si, 16
    loop .parse
    cmp byte [part_active], 1
    je .ok
    cmp word [PART_TABLE_OFFSET + 4], 0
    je .fail
.ok:
    mov si, msg_pt
    call print_string_16
    ret
.fail:
    mov si, msg_err
    call print_string_16
    jmp $

; Load Stage 2 from disk
load_stage2:
    mov si, PART_TABLE_OFFSET
    mov eax, [si + 8]
    xor edx, edx
    div word [bpb_sectors_per_track]
    inc dx
    mov cl, dl
    xor dx, dx
    div word [bpb_num_heads]
    mov ch, al
    shl ah, 6
    or cl, ah
    mov dh, dl
    mov ah, 0x02
    mov al, STAGE2_SECTOR_COUNT
    mov dl, [bp_drive]
    mov bx, STAGE2_LOAD_OFF
    int 0x13
    jc .fail
    cmp word [STAGE2_LOAD_OFF + 0], STAGE2_SIGNATURE
    jne .fail
    mov si, msg_ok
    call print_string_16
    ret
.fail:
    mov si, msg_fail
    call print_string_16
    jmp $

; Print String (null-terminated, 16-bit real mode)
print_string_16:
    pusha
.loop:
    lodsb
    or al, al
    jz .done
    mov ah, 0x0E
    int 0x10
    jmp .loop
.done:
    popa
    ret

; Data
bp_drive:     db 0
part_active:  db 0

msg_boot:     db 'C95', 0
msg_pt:       db 'OK', 0
msg_ok:       db 'RDY', 0
msg_err:      db '!', 0
msg_fail:     db '?', 0

; Pad to 446 bytes
times 446-($-$$) db 0

; MBR Partition Table (4 entries x 16 bytes)
partition_entry_1:
    db 0x80, 0x00, 0x01, 0x00
    db 0x83, 0xFE, 0xFF, 0xFF
    dd 1
    dd 2047
partition_entry_2:
    times 16 db 0
partition_entry_3:
    times 16 db 0
partition_entry_4:
    times 16 db 0

dw 0xAA55
