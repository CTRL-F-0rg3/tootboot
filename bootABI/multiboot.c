/* multiboot.c — Multiboot2 kernel loader (BootABI adapter)
 * Handles any OS following the Multiboot2 spec (Redox, custom kernels, etc.)
 */

#include "multiboot.h"
#include "bootabi.h"

#define MULTIBOOT2_HEADER_MAGIC  0xE85250D6
#define MULTIBOOT2_LOAD_ADDR     0x100000   /* load kernel at 1MB */
#define MB2_SEARCH_LIMIT         32768      /* search first 32KB for header */

__attribute__((weak))
int disk_read(uint8_t drive, uint64_t lba, uint16_t sectors, void *buf) {
    (void)drive; (void)lba; (void)sectors; (void)buf;
    return -1;
}

/* ---------------------------------------------------------------------------
 * Find Multiboot2 header within first 32KB of image.
 * Returns offset into buf, or -1 if not found.
 * ---------------------------------------------------------------------------*/
static int mb2_find_header(uint8_t *buf, int size) {
    for (int i = 0; i < size - 8; i += 8) {
        uint32_t *p = (uint32_t *)(buf + i);
        if (p[0] == MULTIBOOT2_HEADER_MAGIC) {
            /* Verify checksum: magic + arch + length + checksum == 0 */
            uint32_t sum = p[0] + p[1] + p[2] + p[3];
            if (sum == 0) return i;
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 * Probe: check for Multiboot2 header in first 64 sectors
 * ---------------------------------------------------------------------------*/
static int mb2_probe(uint64_t lba, uint8_t drive) {
    uint8_t *buf = (uint8_t *)0x9000;
    if (disk_read(drive, lba, 64, buf) != 0) return 0;
    return mb2_find_header(buf, 64 * 512) >= 0;
}

/* ---------------------------------------------------------------------------
 * Build minimal Multiboot2 info structure in memory and jump to kernel
 * ---------------------------------------------------------------------------*/
static void __attribute__((noreturn)) mb2_load(BootAbiParams *params, uint8_t drive) {
    /* Load 64 sectors (32KB) to find header and estimate size */
    uint8_t *probe_buf = (uint8_t *)0x9000;
    disk_read(drive, params->partition_lba, 64, probe_buf);

    int hdr_off = mb2_find_header(probe_buf, 64 * 512);
    if (hdr_off < 0) goto fail;

    /* Scan for load address tag (type 2) or use default 1MB */
    uint64_t load_addr = MULTIBOOT2_LOAD_ADDR;
    uint64_t entry     = 0;

    uint8_t *hdr = probe_buf + hdr_off;
    uint32_t hdr_len = ((uint32_t *)hdr)[2];
    uint8_t *tag = hdr + 16;  /* tags start after 16-byte header */
    uint8_t *end = hdr + hdr_len;

    while (tag < end) {
        uint32_t type = ((uint32_t *)tag)[0];
        uint32_t size = ((uint32_t *)tag)[1];
        if (type == 0) break;  /* end tag */

        if (type == 2) {
            /* Load address tag */
            load_addr = ((uint32_t *)tag)[2];
        }
        if (type == 3) {
            /* Entry address tag */
            entry = ((uint32_t *)tag)[2];
        }
        /* Tags are 8-byte aligned */
        tag += (size + 7) & ~7;
    }

    /* Load kernel image to load_addr */
    uint32_t sects = 128;  /* 64KB — conservative, expand if needed */
    disk_read(drive, params->partition_lba, (uint16_t)sects, (void *)load_addr);

    if (entry == 0) entry = load_addr;

    /* Build minimal Multiboot2 info structure at 0x8000 */
    uint32_t *mb2_info = (uint32_t *)0x8000;
    mb2_info[0] = 8;   /* total_size = just the terminator */
    mb2_info[1] = 0;   /* reserved */
    /* end tag */
    mb2_info[2] = 0;
    mb2_info[3] = 8;

    /* Jump to kernel:
     *   eax = MULTIBOOT2_BOOTLOADER_MAGIC (0x36D76289)
     *   ebx = physical address of Multiboot2 info structure
     */
    __asm__ volatile (
        "mov $0x36D76289, %%eax\n"
        "mov %0, %%ebx\n"
        "jmp *%1\n"
        :
        : "r"((uint32_t)0x8000), "r"(entry)
        : "eax", "ebx"
    );

fail:
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

/* ---------------------------------------------------------------------------
 * BootABI ops table for Multiboot2
 * ---------------------------------------------------------------------------*/
const BootAbiOps multiboot_loader_ops = {
    .name    = "Multiboot2",
    .os_type = BOOTABI_OS_MULTIBOOT,
    .probe   = mb2_probe,
    .load    = mb2_load,
};

void multiboot_loader_init(void) {
    bootabi_register(&multiboot_loader_ops);
}