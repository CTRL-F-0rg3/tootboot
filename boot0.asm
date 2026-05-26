; boot0.asm — TootBoot stage 0
; x86_64 | NASM | Real Mode -> Long Mode
; Multiboot2 compatible + TootBoot ABI

; ---------------------------------------------------------------------------
; Constants
; ---------------------------------------------------------------------------
MULTIBOOT2_MAGIC equ 0xE85250D6
MULTIBOOT2_ARCH  equ 0
TOOTBOOT_MAGIC   equ 0x544F4F54

CR0_PE   equ (1 << 0)
CR0_PG   equ (1 << 31)
CR4_PAE  equ (1 << 5)

EFER_MSR equ 0xC0000080
EFER_LME equ (1 << 8)
EFER_NXE equ (1 << 11)

GDT_CODE64_SEL equ 0x18
GDT_DATA64_SEL equ 0x20

PML4_ADDRESS equ 0x1000
PDPT_ADDRESS equ 0x2000
PD_ADDRESS   equ 0x3000

; ---------------------------------------------------------------------------
; Multiboot2 header
; ---------------------------------------------------------------------------
section .multiboot2 progbits alloc noexec nowrite align=8

multiboot2_header_start:
    dd MULTIBOOT2_MAGIC
    dd MULTIBOOT2_ARCH
    dd (multiboot2_header_end - multiboot2_header_start)
    dd -(MULTIBOOT2_MAGIC + MULTIBOOT2_ARCH + \
         (multiboot2_header_end - multiboot2_header_start))
    align 8
    .tag_entry:
        dw 3, 1
        dd 12
        dd _start
    align 8
    .tag_end:
        dw 0, 0
        dd 8
multiboot2_header_end:

; ---------------------------------------------------------------------------
; TootBoot ABI header
; ---------------------------------------------------------------------------
section .tootabi progbits alloc noexec nowrite align=8

tootboot_abi_header:
    dd TOOTBOOT_MAGIC
    dd 0x00010000       ; version 1.0.0
    dd _start
    dd boot1_entry_point
    dd 0                ; reserved
    dq 0                ; signature (future use)

; ---------------------------------------------------------------------------
; Boot sequence
; ---------------------------------------------------------------------------
section .text
bits 16

global _start
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

    lgdt [gdt_ptr]

    mov eax, cr4
    or  eax, CR4_PAE
    mov cr4, eax

    ; Identity map first 2MB via PML4 -> PDPT -> PD
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

    mov ecx, EFER_MSR
    rdmsr
    or  eax, (EFER_LME | EFER_NXE)
    wrmsr

    mov eax, cr0
    or  eax, (CR0_PE | CR0_PG)
    mov cr0, eax

    jmp GDT_CODE64_SEL:long_mode_entry

bits 64
long_mode_entry:
    mov ax, GDT_DATA64_SEL
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    mov rsp, 0x90000

    ; rdi = ABI header, rsi = magic, rdx = PML4
    mov rdi, tootboot_abi_header
    mov rsi, TOOTBOOT_MAGIC
    mov rdx, PML4_ADDRESS

    call boot1_entry_point

.halt:
    cli
    hlt
    jmp .halt

; ---------------------------------------------------------------------------
; GDT
; ---------------------------------------------------------------------------
section .data
align 8

gdt_start:
    dq 0x0000000000000000   ; null
    dq 0x00CF9A000000FFFF   ; code32
    dq 0x00CF92000000FFFF   ; data32
    dq 0x00AF9A000000FFFF   ; code64
    dq 0x00AF92000000FFFF   ; data64
gdt_end:

gdt_ptr:
    dw (gdt_end - gdt_start - 1)
    dq gdt_start

extern boot1_entry_point

section .mbr_signature
    times (510 - ($ - $$)) db 0
    dw 0xAA55