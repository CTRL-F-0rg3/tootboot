/* linux.c — Linux bzImage loader (BootABI adapter)
 * Implements Linux x86 boot protocol 2.x (64-bit direct boot)
 */

#include "linux.h"
#include "bootabi.h"

/* Kernel load addresses per Linux boot protocol */
#define LINUX_KERNEL_LOAD_ADDR  0x100000   /* 1MB — standard for 64-bit */
#define LINUX_CMDLINE_ADDR      0x20000    /* safe area for command line */
#define LINUX_SETUP_HEADER_OFF  0x1F1      /* setup header offset in bzImage */
#define LINUX_LOADER_TYPE       0xFF       /* loader type: TootBoot */

__attribute__((weak))
int disk_read(uint8_t drive, uint64_t lba, uint16_t sectors, void *buf) {
    (void)drive; (void)lba; (void)sectors; (void)buf;
    return -1;
}

/* ---------------------------------------------------------------------------
 * Probe: check bzImage boot magic
 * ---------------------------------------------------------------------------*/
static int linux_probe(uint64_t lba, uint8_t drive) {
    uint8_t *buf = (uint8_t *)0x9000;
    if (disk_read(drive, lba, 1, buf) != 0) return 0;

    LinuxSetupHeader *hdr = (LinuxSetupHeader *)(buf + LINUX_SETUP_HEADER_OFF);
    return (hdr->boot_flag == LINUX_BOOT_MAGIC &&
            hdr->header    == LINUX_HDR_MAGIC);
}

/* ---------------------------------------------------------------------------
 * Copy null-terminated string, return length
 * ---------------------------------------------------------------------------*/
static int strcpy_s(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return i;
}

/* ---------------------------------------------------------------------------
 * Load: read bzImage and jump to 64-bit entry point
 * ---------------------------------------------------------------------------*/
static void __attribute__((noreturn)) linux_load(BootAbiParams *params, uint8_t drive) {
    uint8_t *setup_buf = (uint8_t *)0x9000;

    /* Read first 4 sectors (setup area) */
    if (disk_read(drive, params->partition_lba, 4, setup_buf) != 0)
        goto fail;

    LinuxSetupHeader *hdr = (LinuxSetupHeader *)(setup_buf + LINUX_SETUP_HEADER_OFF);

    if (hdr->boot_flag != LINUX_BOOT_MAGIC || hdr->header != LINUX_HDR_MAGIC)
        goto fail;

    /* Number of setup sectors (default 4 if 0) */
    uint32_t setup_sects = hdr->setup_sects ? hdr->setup_sects : 4;
    uint32_t setup_size  = (setup_sects + 1) * 512;

    /* Load full setup */
    disk_read(drive, params->partition_lba,
              (uint16_t)((setup_size + 511) / 512), setup_buf);

    /* Load kernel (after setup) to 1MB */
    uint64_t kernel_lba   = params->partition_lba + setup_size / 512;
    uint32_t kernel_size  = hdr->syssize * 16;
    uint32_t kernel_sects = (kernel_size + 511) / 512;

    disk_read(drive, kernel_lba, (uint16_t)kernel_sects,
              (void *)LINUX_KERNEL_LOAD_ADDR);

    /* Set up command line */
    char *cmdline_buf = (char *)LINUX_CMDLINE_ADDR;
    strcpy_s(cmdline_buf, params->cmdline, 256);

    /* Fill boot protocol fields */
    hdr->type_of_loader = LINUX_LOADER_TYPE;
    hdr->cmd_line_ptr   = LINUX_CMDLINE_ADDR;
    hdr->loadflags     |= 0x01;   /* LOADED_HIGH */

    /* 64-bit entry point: kernel base + 0x200 */
    uint64_t entry = LINUX_KERNEL_LOAD_ADDR + 0x200;
    void (*kernel_entry)(void) = (void (*)(void))entry;
    kernel_entry();

fail:
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

/* ---------------------------------------------------------------------------
 * BootABI ops table for Linux
 * ---------------------------------------------------------------------------*/
const BootAbiOps linux_loader_ops = {
    .name    = "Linux",
    .os_type = BOOTABI_OS_LINUX,
    .probe   = linux_probe,
    .load    = linux_load,
};

void linux_loader_init(void) {
    bootabi_register(&linux_loader_ops);
}