[BITS 64]

section .text

extern isr_handler
extern syscall_handler

global gdt_flush
global tss_flush
global isr_stub_table
global syscall_entry

%macro ISR_NOERRCODE 1
global isr%1
isr%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERRCODE 1
global isr%1
isr%1:
    push qword %1
    jmp isr_common
%endmacro

ISR_NOERRCODE 0
ISR_NOERRCODE 1
ISR_NOERRCODE 2
ISR_NOERRCODE 3
ISR_NOERRCODE 4
ISR_NOERRCODE 5
ISR_NOERRCODE 6
ISR_NOERRCODE 7
ISR_ERRCODE 8
ISR_NOERRCODE 9
ISR_ERRCODE 10
ISR_ERRCODE 11
ISR_ERRCODE 12
ISR_ERRCODE 13
ISR_ERRCODE 14
ISR_NOERRCODE 15
ISR_NOERRCODE 16
ISR_ERRCODE 17
ISR_NOERRCODE 18
ISR_NOERRCODE 19
ISR_NOERRCODE 20
ISR_NOERRCODE 21
ISR_NOERRCODE 22
ISR_NOERRCODE 23
ISR_NOERRCODE 24
ISR_NOERRCODE 25
ISR_NOERRCODE 26
ISR_NOERRCODE 27
ISR_NOERRCODE 28
ISR_ERRCODE 29
ISR_ERRCODE 30
ISR_NOERRCODE 31

%assign i 32
%rep 224
ISR_NOERRCODE i
%assign i i+1
%endrep

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov ax, 0x10
    mov ds, ax
    mov es, ax

    mov rdi, rsp
    call isr_handler

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

gdt_flush:
    lgdt [rdi]
    push qword 0x08
    lea rax, [rel .gdt_flush_ret]
    push rax
    retfq
.gdt_flush_ret:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret

tss_flush:
    mov ax, 0x28
    ltr ax
    ret

global idt_load
idt_load:
    lidt [rdi]
    ret

; ==========================================================
; SYSCALL entry point (loaded via MSR_LSTAR)
;
; On SYSCALL instruction:
;   RCX <- user RIP (return address)
;   R11 <- user RFLAGS
;   RAX <- syscall number (user-set before SYSCALL)
;   RDI, RSI, RDX, R8, R9, R10 <- user args
;   RSP unchanged (still user RSP)
;   CS/SS loaded from STAR MSR
;   RFLAGS.IF cleared per SFMASK
;
; syscall_frame_t layout (each field is 8 bytes):
;   [frame+0]   r15    [frame+8]   r14    [frame+16]  r13
;   [frame+24]  r12    [frame+32]  r11    [frame+40]  r10
;   [frame+48]  r9     [frame+56]  r8     [frame+64]  rbp
;   [frame+72]  rdi    [frame+80]  rsi    [frame+88]  rdx
;   [frame+96]  rcx    [frame+104] rbx    [frame+112] rax
;   [frame+120] rip    [frame+128] cs     [frame+136] rflags
;   [frame+144] rsp    [frame+152] ss
; ==========================================================

section .bss
align 16
_syscall_kstack:
    resb 0x4000
_syscall_kstack_top:

section .data
_syscall_user_rsp_save: dq 0

section .text
global syscall_entry
syscall_entry:
    mov [rel _syscall_user_rsp_save], rsp
    mov rsp, _syscall_kstack_top

    ; Build syscall_frame_t at [rsp]
    ; We use mov to build the frame without push (cleaner for SYSRET exit)
    ; Allocate 160 bytes (20 qwords)
    sub rsp, 160

    mov qword [rsp + 152], 0       ; ss = 0
    mov r15, [rel _syscall_user_rsp_save]
    mov [rsp + 144], r15           ; rsp = user RSP
    mov [rsp + 136], r11           ; rflags (captured by SYSCALL)
    mov qword [rsp + 128], 0x08    ; cs = ring0 code segment
    mov [rsp + 120], rcx           ; rip (captured by SYSCALL)
    mov [rsp + 112], rax           ; rax = syscall number
    mov [rsp + 104], rbx
    mov qword [rsp + 96], 0        ; rcx (clobbered by SYSCALL, unavailable)
    mov [rsp + 88], rdx
    mov [rsp + 80], rsi
    mov [rsp + 72], rdi
    mov [rsp + 64], rbp
    mov [rsp + 56], r8
    mov [rsp + 48], r9
    mov [rsp + 40], r10
    mov qword [rsp + 32], 0        ; r11 (clobbered by SYSCALL to RFLAGS)
    mov [rsp + 24], r12
    mov [rsp + 16], r13
    mov [rsp + 8], r14
    mov [rsp + 0], r15

    mov rdi, rsp                    ; arg0 = &syscall_frame_t
    call syscall_handler

    ; Read restored frame and return via SYSRETQ
    mov rcx, [rsp + 120]           ; RCX = user RIP (SYSRET return addr)
    mov r11, [rsp + 136]           ; R11 = user RFLAGS (SYSRET restores)

    ; Restore all GPRs from the frame (SYSRET won't touch these)
    mov r15, [rsp + 0]
    mov r14, [rsp + 8]
    mov r13, [rsp + 16]
    mov r12, [rsp + 24]
    ; skip [rsp+32] (r11 slot)
    mov r10, [rsp + 40]
    mov r9,  [rsp + 48]
    mov r8,  [rsp + 56]
    mov rbp, [rsp + 64]
    mov rdi, [rsp + 72]
    mov rsi, [rsp + 80]
    mov rdx, [rsp + 88]
    ; skip [rsp+96] (rcx slot)
    mov rbx, [rsp + 104]
    mov rax, [rsp + 112]           ; return value

    ; Load user RSP last (overwrites rsp)
    mov rsp, [rsp + 144]

    db 0x48, 0x0F, 0x07           ; sysretq (REX.W + SYSRET)

; ==========================================================
; context_switch: save callee-saved regs of old, restore of new
;
; void context_switch(uint64_t *old_rsp, uint64_t new_rsp);
;   rdi = pointer to old RSP storage
;   rsi = new RSP value (kernel stack top for target process)
;
; Also loads CR3 from the target process's context if needed.
; ==========================================================

global context_switch
context_switch:
    ; Save callee-saved registers onto current stack
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save current RSP into *old_rsp
    mov [rdi], rsp

    ; Load new RSP
    mov rsp, rsi

    ; Restore callee-saved registers from new stack
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp

    ret

; ==========================================================
; switch_to_user_mode: transition from ring0 to ring3 via IRETQ
;
; void switch_to_user_mode(uint64_t rip, uint64_t rsp, uint64_t rflags);
;   rdi = user RIP
;   rsi = user RSP
;   rdx = user RFLAGS
; ==========================================================

global switch_to_user_mode
switch_to_user_mode:
    mov ax, 0x23       ; USER_DS | RPL3
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    push qword 0x23    ; user SS (USER_DS | RPL3)
    push rsi            ; user RSP
    push rdx            ; user RFLAGS
    push qword 0x1B    ; user CS (USER_CS | RPL3)
    push rdi            ; user RIP

    iretq

; ==========================================================
; read_rip: return the address of the caller (for debugging)
; ==========================================================

global read_rip
read_rip:
    pop rax
    push rax
    ret

section .data

isr_stub_table:
    dq isr0, isr1, isr2, isr3, isr4, isr5, isr6, isr7
    dq isr8, isr9, isr10, isr11, isr12, isr13, isr14, isr15
    dq isr16, isr17, isr18, isr19, isr20, isr21, isr22, isr23
    dq isr24, isr25, isr26, isr27, isr28, isr29, isr30, isr31
    dq isr32, isr33, isr34, isr35, isr36, isr37, isr38, isr39
    dq isr40, isr41, isr42, isr43, isr44, isr45, isr46, isr47
    dq isr48, isr49, isr50, isr51, isr52, isr53, isr54, isr55
    dq isr56, isr57, isr58, isr59, isr60, isr61, isr62, isr63
    dq isr64, isr65, isr66, isr67, isr68, isr69, isr70, isr71
    dq isr72, isr73, isr74, isr75, isr76, isr77, isr78, isr79
    dq isr80, isr81, isr82, isr83, isr84, isr85, isr86, isr87
    dq isr88, isr89, isr90, isr91, isr92, isr93, isr94, isr95
    dq isr96, isr97, isr98, isr99, isr100, isr101, isr102, isr103
    dq isr104, isr105, isr106, isr107, isr108, isr109, isr110, isr111
    dq isr112, isr113, isr114, isr115, isr116, isr117, isr118, isr119
    dq isr120, isr121, isr122, isr123, isr124, isr125, isr126, isr127
    dq isr128, isr129, isr130, isr131, isr132, isr133, isr134, isr135
    dq isr136, isr137, isr138, isr139, isr140, isr141, isr142, isr143
    dq isr144, isr145, isr146, isr147, isr148, isr149, isr150, isr151
    dq isr152, isr153, isr154, isr155, isr156, isr157, isr158, isr159
    dq isr160, isr161, isr162, isr163, isr164, isr165, isr166, isr167
    dq isr168, isr169, isr170, isr171, isr172, isr173, isr174, isr175
    dq isr176, isr177, isr178, isr179, isr180, isr181, isr182, isr183
    dq isr184, isr185, isr186, isr187, isr188, isr189, isr190, isr191
    dq isr192, isr193, isr194, isr195, isr196, isr197, isr198, isr199
    dq isr200, isr201, isr202, isr203, isr204, isr205, isr206, isr207
    dq isr208, isr209, isr210, isr211, isr212, isr213, isr214, isr215
    dq isr216, isr217, isr218, isr219, isr220, isr221, isr222, isr223
    dq isr224, isr225, isr226, isr227, isr228, isr229, isr230, isr231
    dq isr232, isr233, isr234, isr235, isr236, isr237, isr238, isr239
    dq isr240, isr241, isr242, isr243, isr244, isr245, isr246, isr247
    dq isr248, isr249, isr250, isr251, isr252, isr253, isr254, isr255
