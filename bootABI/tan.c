/* tan.c — TanOS kernel loader (BootABI adapter) */

#include "tan.h"
#include "bootabi.h"

/* ---------------------------------------------------------------------------
 * Disk read helper (INT 13h extended, real mode needed — stub for now)
 * In freestanding 64-bit we can't call INT 13h directly.
 * Boot1 must provide a disk_read() callback; for now we use a weak stub.
 * ---------------------------------------------------------------------------*/
__attribute__((weak))
int disk_read(uint8_t drive, uint64_t lba, uint16_t sectors, void *buf) {
    (void)drive; (void)lba; (void)sectors; (void)buf;
    return -1;  /* stub — implement via boot1 trampoline */
}

/* ---------------------------------------------------------------------------
 * Probe: check if partition contains a TanOS kernel.
 * Reads first sector of partition and checks for TANOS_KERNEL_MAGIC.
 * ---------------------------------------------------------------------------*/
static int tan_probe(uint64_t lba, uint8_t drive) {
    /* Read first sector into a temporary buffer at a known safe address */
    uint8_t *buf = (uint8_t *)0x9000;
    if (disk_read(drive, lba, 1, buf) != 0) return 0;

    TanOsKernelHeader *hdr = (TanOsKernelHeader *)buf;
    return hdr->magic == TANOS_KERNEL_MAGIC;
}

/* ---------------------------------------------------------------------------
 * Load: read TanOS kernel into memory and jump to entry point.
 * ---------------------------------------------------------------------------*/
static void __attribute__((noreturn)) tan_load(BootAbiParams *params, uint8_t drive) {
    uint8_t *buf = (uint8_t *)0x9000;

    /* Read kernel header */
    disk_read(drive, params->partition_lba, 1, buf);
    TanOsKernelHeader *hdr = (TanOsKernelHeader *)buf;

    /* Validate */
    if (hdr->magic != TANOS_KERNEL_MAGIC) goto fail;

    uint64_t load_addr  = hdr->load_address;
    uint64_t entry      = hdr->entry_point;
    uint32_t size       = hdr->image_size;
    uint32_t sectors    = (size + 511) / 512;

    /* Load full kernel image */
    if (disk_read(drive, params->partition_lba, (uint16_t)sectors,
                  (void *)load_addr) != 0) goto fail;

    /* Pass BootAbiParams to kernel:
     *   rdi = pointer to BootAbiParams (System V AMD64 ABI arg 1)
     */
    void (*kernel_entry)(BootAbiParams *) = (void (*)(BootAbiParams *))entry;
    kernel_entry(params);

fail:
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

/* ---------------------------------------------------------------------------
 * BootABI ops table for TanOS
 * ---------------------------------------------------------------------------*/
const BootAbiOps tan_loader_ops = {
    .name    = "TanOS",
    .os_type = BOOTABI_OS_TANOS,
    .probe   = tan_probe,
    .load    = tan_load,
};

void tan_loader_init(void) {
    bootabi_register(&tan_loader_ops);
}