[bits 16]
[org 0]

; NOTE: assembled with [org 0] so every label is a file offset. The boot
; sector is loaded by the BIOS at 0x0000:0x7C00, but the loader is relocated
; to LOADER_SEG and runs there with DS=CS=LOADER_SEG, so label-based
; references resolve to LOADER_SEG:file_offset == the relocated copy.

; ============================================================================
; Chicago-95 stage1 boot sector
;
; Loads the full chain in real mode, then hands off to stage2 at 0x0600:
;   stage2 (LBA 1,      1023 sectors) -> 0x0600..0x80400
;   stage3 (LBA 0x0400,   34 sectors) -> 0x100000..0x104400
;   kernel (LBA 0x1000,  148 sectors) -> 0x10000..0x22800
;
; The stage2 region (0x0600..0x80400) covers the boot sector at 0x7C00, so the
; whole 512-byte loader is copied to the scratch area 0x9F00:0x0000 first and
; continues there, outside every load target.
; ============================================================================

LOADER_SEG         equ 0x9F00          ; segment for relocated loader (0x9F000)
STACK_SEG          equ 0x9F80          ; stack segment (0x9F800)
STACK_TOP          equ 0x0400          ; sp -> stack spans 0x9F800..0x9FC00

STAGE2_LBA         equ 1
STAGE2_SECTORS     equ 1023
STAGE2_DEST        equ 0x0600

STAGE3_LBA         equ 0x0400
STAGE3_SECTORS     equ 34
STAGE3_SEG         equ 0xF000          ; segment base 0xF0000
STAGE3_OFF         equ 0x1000          ; + offset -> physical 0x100000

KERNEL_LBA         equ 0x1000
KERNEL_SECTORS     equ 148
    KERNEL_SEG         equ 0x20000          ; segment base 0x200000
    KERNEL_OFF         equ 0x0000


STAGE2_SIGNATURE   equ 0x7A3B

MAX_BATCH          equ 62              ; sectors per int 13h call (keeps the
                                       ; buffer inside one 64K segment window)

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
    ; Boot drive (DL) survives the relocation below (rep movsw uses AX/SI/DI/CX
    ; only) and is saved to bp_drive after we are running from the relocated
    ; copy, where label-based stores resolve to LOADER_SEG.
    cli
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ax, STACK_SEG
    mov ss, ax
    mov sp, STACK_TOP
    sti

    ; Relocate the whole 512-byte loader to LOADER_SEG so it survives the
    ; stage2 load that otherwise overwrites 0x7C00..0x7E00.
    mov si, 0x7C00
    mov ax, LOADER_SEG
    mov es, ax
    xor di, di
    mov cx, 256
    cld
    rep movsw
    jmp LOADER_SEG:relocated_start

relocated_start:
    ; Run from the relocated copy: DS must point at it so all label-based
    ; references (messages, DAP) resolve to the relocated data.
    mov ax, LOADER_SEG
    mov ds, ax
    mov es, ax

    mov [bp_drive], dl

    mov si, msg_boot
    call print_string_16

    call enable_a20

    ; Load stage2 (LBA 1, 1023 sectors) to 0x0600.
    mov dl, [bp_drive]
    xor ax, ax
    mov es, ax              ; es = 0
    mov eax, STAGE2_LBA
    mov cx, STAGE2_SECTORS
    mov di, STAGE2_DEST
    call load_sectors
    jc .load_fail

    ; Verify stage2 signature at 0x0600.
    push ds
    xor ax, ax
    mov ds, ax
    cmp word [STAGE2_DEST], STAGE2_SIGNATURE
    pop ds
    jne .load_fail
    mov si, msg_s2
    call print_string_16

    ; Load stage3 (LBA 0x400, 34 sectors) to 0x100000.
    mov ax, STAGE3_SEG
    mov es, ax
    mov eax, STAGE3_LBA
    mov cx, STAGE3_SECTORS
    mov di, STAGE3_OFF
    call load_sectors
    jc .load_fail

    ; Load kernel (LBA 0x1000, 148 sectors) to 0x200000.
    mov ax, KERNEL_SEG
    mov es, ax
    mov eax, KERNEL_LBA
    mov cx, KERNEL_SECTORS
    mov di, KERNEL_OFF
    call load_sectors
    jc .load_fail

    mov si, msg_ok
    call print_string_16

    ; Hand off to stage2 (its own copy of boot_drive is in DL).
    mov dl, [bp_drive]
    jmp 0x0000:STAGE2_DEST

.load_fail:
    mov si, msg_fail
    call print_string_16
    jmp $

; ============================================================================
; load_sectors -- int 13h extended read of `cx` sectors from LBA `eax` into
; ES:DI, advancing ES by one 64K window per MAX_BATCH sectors.
; Returns: CF=0 ok, CF=1 error.
; ============================================================================
load_sectors:
    pusha
    ; int 13h may clobber EAX, so the running LBA lives in memory (cur_lba),
    ; never in EAX across the call.
    mov dword [cur_lba], eax
.s_loop:
    mov word [dap_count], MAX_BATCH
    cmp cx, MAX_BATCH
    jae .go
    mov word [dap_count], cx
.go:
    mov word [dap_offset], di
    mov word [dap_segment], es
    mov eax, [cur_lba]
    mov dword [dap_lba], eax
    mov dword [dap_lba + 4], 0

    mov si, dap
    mov ah, 0x42
    int 0x13
    jc .done

    movzx ebx, word [dap_count]   ; sectors actually read
    sub cx, bx
    jz .done

    ; lba += count, es += count * 32 (512-byte sectors / 16-byte paragraphs)
    add dword [cur_lba], ebx
    shl ebx, 5
    mov ax, es
    add ax, bx
    mov es, ax
    jmp .s_loop
.done:
    popa
    ret

; ============================================================================
; A20 Line Enable
; ============================================================================
enable_a20:
    mov ax, 0x2401
    int 0x15
    jnc .a20_done
    in al, 0x92
    or al, 2
    out 0x92, al
.a20_done:
    ret

; ============================================================================
; Print String (null-terminated, 16-bit real mode)
; ============================================================================
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

; ============================================================================
; Data
; ============================================================================
dap:
    db 0x10, 0
dap_count:      dw 0
dap_offset:     dw 0
dap_segment:    dw 0
dap_lba:        dd 0, 0

cur_lba:        dd 0

bp_drive:       db 0

msg_boot:       db 'C95', 0
msg_s2:         db 'L2', 0
msg_ok:         db 'RDY', 0
msg_fail:       db '?', 0

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
