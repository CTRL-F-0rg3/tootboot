; disk.asm — disk_read trampoline: 64-bit -> real mode -> INT 13h -> 64-bit
; Called from C as: int disk_read(uint8_t drive, uint64_t lba, uint16_t sectors, void *buf)
; System V AMD64 ABI: rdi=drive, rsi=lba, rdx=sectors, rcx=buf

[BITS 64]
[SECTION .text]

; Saved 64-bit state
saved_rsp:  dq 0
saved_cr3:  dq 0

; GDT for dropping back to protected/real mode
rm_gdt_start:
    dq 0x0000000000000000   ; null
    dq 0x00CF9A000000FFFF   ; code32
    dq 0x00CF92000000FFFF   ; data32
    dq 0x00AF9A000000FFFF   ; code64
    dq 0x00AF92000000FFFF   ; data64
rm_gdt_end:

rm_gdt_ptr:
    dw (rm_gdt_end - rm_gdt_start - 1)
    dq rm_gdt_start

; 16-bit GDT for real mode transition
rm16_gdt_start:
    dq 0x0000000000000000   ; null
    ; 16-bit code: base=0x7C00, limit=0xFFFF
    dq 0x00009A007C00FFFF
    ; 16-bit data: base=0, limit=0xFFFF
    dq 0x000092000000FFFF
rm16_gdt_end:

rm16_gdt_ptr:
    dw (rm16_gdt_end - rm16_gdt_start - 1)
    dd rm16_gdt_start

; DAP stored here, filled at runtime
disk_dap:
    db 0x10     ; size
    db 0x00     ; reserved
    dw 0        ; sector count (filled at call)
    dw 0        ; buffer offset (filled at call)
    dw 0        ; buffer segment (filled at call)
    dq 0        ; LBA (filled at call)

disk_drive: db 0x80

global disk_read
disk_read:
    ; rdi = drive, rsi = lba, rdx = sectors, rcx = buf
    push rbp
    mov  rbp, rsp
    push rbx
    push r12
    push r13
    push r14
    push r15

    ; Save parameters
    mov r12b, dil           ; drive
    mov r13,  rsi           ; lba
    mov r14w, dx            ; sectors
    mov r15,  rcx           ; buf (must be < 0x10000 for real mode)

    ; Fill DAP
    mov byte  [disk_drive],       r12b
    mov word  [disk_dap + 2],     r14w
    ; buf offset (low 16 bits)
    mov ax,   r15w
    mov word  [disk_dap + 4],     ax
    ; buf segment = 0
    mov word  [disk_dap + 6],     0
    ; LBA
    mov qword [disk_dap + 8],     r13

    ; Save 64-bit stack and CR3
    mov [saved_rsp], rsp
    mov rax, cr3
    mov [saved_cr3], rax

    ; -----------------------------------------------------------------------
    ; Step 1: Switch to 32-bit protected mode (disable paging)
    ; -----------------------------------------------------------------------
    mov rax, cr0
    and eax, 0x7FFFFFFF     ; clear PG
    mov cr0, rax

    ; Flush TLB
    xor rax, rax
    mov cr3, rax

    ; Far jump to 32-bit code segment
    mov eax, pm32_entry
    mov [.jmp_target], eax
    mov word [.jmp_target+4], 0x08
    jmp far [.jmp_target]
.jmp_target: dq 0

[BITS 32]
pm32_entry:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; -----------------------------------------------------------------------
    ; Step 2: Switch to Real Mode — clear PE bit
    ; -----------------------------------------------------------------------
    mov eax, cr0
    and eax, 0xFFFFFFFE     ; clear PE
    mov cr0, eax

    jmp 0x0000:rm16_entry

[BITS 16]
rm16_entry:
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov sp, 0x7000          ; temporary 16-bit stack

    sti

    ; -----------------------------------------------------------------------
    ; Step 3: INT 13h extended read
    ; -----------------------------------------------------------------------
    mov ah, 0x42
    mov dl, [disk_drive]
    mov si, disk_dap
    int 0x13
    jc  .error
    xor bx, bx              ; success = 0
    jmp .done
.error:
    mov bx, 1               ; error = 1

.done:
    cli

    ; -----------------------------------------------------------------------
    ; Step 4: Back to Protected Mode
    ; -----------------------------------------------------------------------
    lgdt [rm_gdt_ptr]
    mov eax, cr0
    or  eax, 1
    mov cr0, eax
    jmp 0x08:back_to_pm32

[BITS 32]
back_to_pm32:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Re-enable Long Mode
    ; Restore CR3 (page tables)
    mov eax, [saved_cr3]    ; low 32 bits only (tables below 4GB)
    mov cr3, eax

    ; Set PAE + LME + PG
    mov eax, cr4
    or  eax, (1 << 5)       ; PAE
    mov cr4, eax

    mov ecx, 0xC0000080     ; EFER
    rdmsr
    or  eax, (1 << 8)       ; LME
    wrmsr

    mov eax, cr0
    or  eax, (1 << 31)      ; PG
    mov cr0, eax

    jmp 0x18:back_to_lm64

[BITS 64]
back_to_lm64:
    mov ax, 0x20
    mov ds, ax
    mov es, ax
    mov ss, ax

    ; Restore stack
    mov rsp, [saved_rsp]

    ; Return value: bx was set (0=ok, 1=error) — zero extend
    movzx eax, bx

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
