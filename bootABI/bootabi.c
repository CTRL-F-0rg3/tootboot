/* bootabi.c — BootABI registry and dispatcher */

#include "bootabi.h"

static const BootAbiOps *registry[BOOTABI_MAX_LOADERS];
static int registry_count = 0;

int bootabi_register(const BootAbiOps *ops) {
    if (registry_count >= BOOTABI_MAX_LOADERS) return -1;
    registry[registry_count++] = ops;
    return 0;
}

const BootAbiOps *bootabi_probe(uint64_t lba, uint8_t drive) {
    for (int i = 0; i < registry_count; i++) {
        if (registry[i]->probe && registry[i]->probe(lba, drive))
            return registry[i];
    }
    return (void*)0;
}

void bootabi_dispatch(BootAbiParams *params, uint8_t drive) {
    for (int i = 0; i < registry_count; i++) {
        if (registry[i]->os_type == params->os_type) {
            registry[i]->load(params, drive);
            /* noreturn — never reaches here */
        }
    }
    /* No loader found — halt */
    __asm__ volatile ("cli; hlt");
    __builtin_unreachable();
}

void bootabi_params_init(BootAbiParams *p, BootAbiOsType type,
                         uint64_t lba, uint64_t pml4,
                         const char *cmdline, const char *kernel_path) {
    /* Zero out the struct */
    uint8_t *b = (uint8_t *)p;
    for (int i = 0; i < (int)sizeof(BootAbiParams); i++) b[i] = 0;

    p->magic               = BOOTABI_PARAMS_MAGIC;
    p->os_type             = type;
    p->partition_lba       = lba;
    p->pml4_address        = pml4;
    p->bootloader_version  = 0x00010000;  /* 1.0.0 */

    /* Copy cmdline */
    if (cmdline) {
        int i = 0;
        while (cmdline[i] && i < 255) { p->cmdline[i] = cmdline[i]; i++; }
    }

    /* Copy kernel path */
    if (kernel_path) {
        int i = 0;
        while (kernel_path[i] && i < 127) { p->kernel_path[i] = kernel_path[i]; i++; }
    }
}