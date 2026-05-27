/* multiboot.h — Multiboot2 structures and constants
 * Spec: https://www.gnu.org/software/grub/manual/multiboot2/
 */

#ifndef MULTIBOOT_H
#define MULTIBOOT_H

/* Magic passed by bootloader to kernel in rax */
#define MULTIBOOT2_BOOTLOADER_MAGIC     0x36D76289

/* Header magic (in the kernel image) */
#define MULTIBOOT2_HEADER_MAGIC         0xE85250D6

/* Architectures */
#define MULTIBOOT2_ARCH_I386            0
#define MULTIBOOT2_ARCH_MIPS32          4

/* Tag types (info passed to kernel) */
#define MULTIBOOT2_TAG_END              0
#define MULTIBOOT2_TAG_CMDLINE          1
#define MULTIBOOT2_TAG_BOOT_LOADER      2
#define MULTIBOOT2_TAG_MODULE           3
#define MULTIBOOT2_TAG_BASIC_MEMINFO    4
#define MULTIBOOT2_TAG_BOOTDEV          5
#define MULTIBOOT2_TAG_MMAP             6
#define MULTIBOOT2_TAG_VBE              7
#define MULTIBOOT2_TAG_FRAMEBUFFER      8
#define MULTIBOOT2_TAG_ELF_SECTIONS     9
#define MULTIBOOT2_TAG_APM              10
#define MULTIBOOT2_TAG_EFI32            11
#define MULTIBOOT2_TAG_EFI64            12
#define MULTIBOOT2_TAG_ACPI_OLD         13
#define MULTIBOOT2_TAG_ACPI_NEW         14
#define MULTIBOOT2_TAG_NETWORK          15
#define MULTIBOOT2_TAG_EFI_MMAP         17
#define MULTIBOOT2_TAG_EFI_BS           18
#define MULTIBOOT2_TAG_EFI32_IH         19
#define MULTIBOOT2_TAG_EFI64_IH         20
#define MULTIBOOT2_TAG_LOAD_BASE_ADDR   21

/* Memory map entry types */
#define MULTIBOOT2_MMAP_AVAILABLE       1
#define MULTIBOOT2_MMAP_RESERVED        2
#define MULTIBOOT2_MMAP_ACPI_RECLAIM    3
#define MULTIBOOT2_MMAP_NVS             4
#define MULTIBOOT2_MMAP_BADRAM          5

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

/* ---------------------------------------------------------------------------
 * Boot information structure passed to the kernel.
 * Lives at the address given in rbx by the bootloader.
 * ---------------------------------------------------------------------------*/
typedef struct __attribute__((packed)) {
    uint32_t total_size;
    uint32_t reserved;
    /* followed by a sequence of tags */
} Multiboot2Info;

/* Generic tag header */
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t size;
} Multiboot2Tag;

/* Tag: command line string */
typedef struct __attribute__((packed)) {
    uint32_t type;      /* MULTIBOOT2_TAG_CMDLINE */
    uint32_t size;
    char     string[];
} Multiboot2TagCmdline;

/* Tag: boot loader name */
typedef struct __attribute__((packed)) {
    uint32_t type;      /* MULTIBOOT2_TAG_BOOT_LOADER */
    uint32_t size;
    char     string[];
} Multiboot2TagBootLoader;

/* Tag: basic memory info */
typedef struct __attribute__((packed)) {
    uint32_t type;      /* MULTIBOOT2_TAG_BASIC_MEMINFO */
    uint32_t size;
    uint32_t mem_lower; /* KB below 1MB */
    uint32_t mem_upper; /* KB above 1MB */
} Multiboot2TagBasicMeminfo;

/* Tag: boot device */
typedef struct __attribute__((packed)) {
    uint32_t type;      /* MULTIBOOT2_TAG_BOOTDEV */
    uint32_t size;
    uint32_t biosdev;
    uint32_t partition;
    uint32_t sub_partition;
} Multiboot2TagBootdev;

/* Memory map entry */
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint64_t len;
    uint32_t entry_type;
    uint32_t reserved;
} Multiboot2MmapEntry;

/* Tag: memory map */
typedef struct __attribute__((packed)) {
    uint32_t type;          /* MULTIBOOT2_TAG_MMAP */
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    Multiboot2MmapEntry entries[];
} Multiboot2TagMmap;

/* Tag: framebuffer */
typedef struct __attribute__((packed)) {
    uint32_t type;          /* MULTIBOOT2_TAG_FRAMEBUFFER */
    uint32_t size;
    uint64_t addr;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t  bpp;
    uint8_t  fb_type;       /* 0=indexed, 1=RGB, 2=EGA text */
    uint16_t reserved;
} Multiboot2TagFramebuffer;

/* Tag: EFI 64-bit image handle */
typedef struct __attribute__((packed)) {
    uint32_t type;          /* MULTIBOOT2_TAG_EFI64_IH */
    uint32_t size;
    uint64_t pointer;
} Multiboot2TagEfi64ih;

/* ---------------------------------------------------------------------------
 * Helper: iterate tags in the Multiboot2 info structure
 * Usage:
 *   Multiboot2Tag *tag = MB2_FIRST_TAG(info);
 *   while (tag->type != MULTIBOOT2_TAG_END) {
 *       // use tag
 *       tag = MB2_NEXT_TAG(tag);
 *   }
 * ---------------------------------------------------------------------------*/
#define MB2_FIRST_TAG(info) \
    ((Multiboot2Tag *)((uint8_t *)(info) + 8))

#define MB2_NEXT_TAG(tag) \
    ((Multiboot2Tag *)(((uint8_t *)(tag) + (tag)->size + 7) & ~7UL))

#endif /* MULTIBOOT_H */