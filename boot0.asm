; boot0.asm — TootBoot stage 0
; x86_64 | NASM flat binary | Real Mode -> Long Mode
; Multiboot2 + TootBoot ABI

[BITS 16]
[ORG 0x7C00]

CR0_PE          equ (1 << 0)
CR0_PG          equ (1 << 31)
CR4_PAE         equ (1 << 5)
EFER_MSR        equ 0xC0000080
EFER_LME        equ (1 << 8)
EFER_NXE        equ (1 << 11)
GDT_CODE64_SEL  equ 0x18
GDT_DATA64_SEL  equ 0x20
PML4_ADDRESS    equ 0x1000
PDPT_ADDRESS    equ 0x2000
PD_ADDRESS      equ 0x3000
BOOT1_ADDRESS   equ 0x7E00
BOOT1_SECTORS   equ 16

MULTIBOOT2_MAGIC equ 0xE85250D6
MULTIBOOT2_ARCH  equ 0
TOOTBOOT_MAGIC   equ 0x544F4F54

; ---------------------------------------------------------------------------
; Multiboot2 header
; ---------------------------------------------------------------------------
mb2_start:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH
    dd (mb2_end - mb2_start)
    dd -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + (mb2_end - mb2_start))
    align 8
    dw 3, 1
    dd 12
    dd _start
    align 8
    dw 0, 0
    dd 8
mb2_end:

; ---------------------------------------------------------------------------
; TootBoot ABI header
; ---------------------------------------------------------------------------
align 8
tootabi:
    dd TOOTBOOT_MAGIC
    dd 0x00010000
    dd _start
    dd BOOT1_ADDRESS
    dd 0
    dq 0

; ---------------------------------------------------------------------------
; Entry point
; ---------------------------------------------------------------------------
_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov sp, 0x7C00

    mov [boot_drive], dl

    ; -------------------------------------------------------------------------
    ; Try INT 13h extended read (LBA) first
    ; -------------------------------------------------------------------------
    mov ah, 0x41             ; check extensions present
    mov bx, 0x55AA
    int 0x13
    jc  .use_chs             ; no extensions -> fall back to CHS
    cmp bx, 0xAA55
    jne .use_chs

    ; LBA read
    mov si, dap
    mov ah, 0x42
    mov dl, [boot_drive]
    int 0x13
    jnc .load_ok
    jmp disk_error

    ; -------------------------------------------------------------------------
    ; CHS fallback (sector 2, head 0, cylinder 0)
    ; -------------------------------------------------------------------------
.use_chs:
    mov ah, 0x02             ; read sectors
    mov al, BOOT1_SECTORS    ; count
    mov ch, 0                ; cylinder 0
    mov cl, 2                ; sector 2 (1-based)
    mov dh, 0                ; head 0
    mov dl, [boot_drive]
    mov bx, BOOT1_ADDRESS    ; ES:BX destination (ES=0)
    int 0x13
    jc  disk_error

.load_ok:
    ; -------------------------------------------------------------------------
    ; Enable PAE + build page tables (2MB identity map)
    ; -------------------------------------------------------------------------
    lgdt [gdt_ptr]

    mov eax, cr4
    or  eax, CR4_PAE
    mov cr4, eax

    mov edi, PML4_ADDRESS
    xor eax, eax
    mov ecx, 3 * 4096 / 4
    rep stosd

    mov dword [PML4_ADDRESS],     (PDPT_ADDRESS | 0x03)
    mov dword [PML4_ADDRESS + 4], 0
    mov dword [PDPT_ADDRESS],     (PD_ADDRESS | 0x03)
    mov dword [PDPT_ADDRESS + 4], 0
    mov dword [PD_ADDRESS],       0x00000083
    mov dword [PD_ADDRESS + 4],   0

    mov eax, PML4_ADDRESS
    mov cr3, eax

    ; -------------------------------------------------------------------------
    ; Activate Long Mode
    ; -------------------------------------------------------------------------
    mov ecx, EFER_MSR
    rdmsr
    or  eax, (EFER_LME | EFER_NXE)
    wrmsr

    mov eax, cr0
    or  eax, (CR0_PE | CR0_PG)
    mov cr0, eax

    jmp GDT_CODE64_SEL:long_mode_entry

; ---------------------------------------------------------------------------
; Disk error: red 'E' on screen then halt
; ---------------------------------------------------------------------------
disk_error:
    mov ax, 0xB800
    mov es, ax
    mov word [es:0], 0x4F45
    cli
    hlt

; ---------------------------------------------------------------------------
; DAP for INT 13h LBA read
; ---------------------------------------------------------------------------
align 4
dap:
    db  0x10
    db  0x00
    dw  BOOT1_SECTORS
    dw  BOOT1_ADDRESS
    dw  0x0000
    dq  1

boot_drive: db 0x80

; ---------------------------------------------------------------------------
; Long Mode
; ---------------------------------------------------------------------------
[BITS 64]
long_mode_entry:
    mov ax, GDT_DATA64_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000

    mov rdi, tootabi
    mov rsi, TOOTBOOT_MAGIC
    mov rdx, PML4_ADDRESS

    call BOOT1_ADDRESS

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; GDT
; ---------------------------------------------------------------------------
align 8
gdt_start:
    dq 0x0000000000000000
    dq 0x00CF9A000000FFFF   ; code32
    dq 0x00CF92000000FFFF   ; data32
    dq 0x00AF9A000000FFFF   ; code64
    dq 0x00AF92000000FFFF   ; data64
gdt_end:

gdt_ptr:
    dw (gdt_end - gdt_start - 1)
    dq gdt_start

times (510 - ($ - $$)) db 0
dw 0xAA55