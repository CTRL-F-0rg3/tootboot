/* bootabi.h — TootBoot ABI: common interface boot1 -> kernel
 * Every OS loader implements the bootabi_ops function table.
 */

#ifndef BOOTABI_H
#define BOOTABI_H

typedef unsigned char  uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int   uint32_t;
typedef unsigned long  uint64_t;

/* ---------------------------------------------------------------------------
 * OS type identifiers
 * ---------------------------------------------------------------------------*/
typedef enum {
    BOOTABI_OS_TANOS     = 0x01,
    BOOTABI_OS_LINUX     = 0x02,
    BOOTABI_OS_MULTIBOOT = 0x03,
    BOOTABI_OS_UNKNOWN   = 0xFF,
} BootAbiOsType;

/* ---------------------------------------------------------------------------
 * Boot parameters passed from boot1 to every OS loader.
 * Loaders must not modify fields marked [readonly].
 * ---------------------------------------------------------------------------*/
typedef struct {
    /* [readonly] magic — must equal BOOTABI_PARAMS_MAGIC */
    uint32_t        magic;

    /* OS type detected by boot1 */
    BootAbiOsType   os_type;

    /* LBA start sector of the OS partition on disk */
    uint64_t        partition_lba;

    /* Physical memory map (passed from boot0 via TootBoot ABI header) */
    uint64_t        pml4_address;

    /* Kernel command line (null-terminated, max 256 chars) */
    char            cmdline[256];

    /* Path to kernel image (null-terminated, max 128 chars) */
    char            kernel_path[128];

    /* [readonly] TootBoot version that created this struct */
    uint32_t        bootloader_version;

    /* Reserved for future use */
    uint8_t         reserved[64];
} BootAbiParams;

#define BOOTABI_PARAMS_MAGIC 0x42414249  /* 'BABI' */

/* ---------------------------------------------------------------------------
 * OS loader operations table.
 * Each loader (tan.c, linux.c, multiboot.c) fills this struct and
 * registers itself via bootabi_register().
 * ---------------------------------------------------------------------------*/
typedef struct {
    /* Human-readable loader name */
    const char *name;

    /* OS type this loader handles */
    BootAbiOsType os_type;

    /* Probe: return 1 if this loader can handle the given partition.
     * lba = partition start sector, drive = BIOS drive number */
    int (*probe)(uint64_t lba, uint8_t drive);

    /* Load and execute the kernel. Must not return on success. */
    void (*load)(BootAbiParams *params, uint8_t drive) __attribute__((noreturn));
} BootAbiOps;

/* ---------------------------------------------------------------------------
 * Registry API — called by each loader at init, used by boot1 dispatcher
 * ---------------------------------------------------------------------------*/
#define BOOTABI_MAX_LOADERS 8

/* Register a loader. Returns 0 on success, -1 if registry is full. */
int bootabi_register(const BootAbiOps *ops);

/* Detect OS type on partition at lba, return matching loader or NULL. */
const BootAbiOps *bootabi_probe(uint64_t lba, uint8_t drive);

/* Dispatch: find loader for params->os_type and call load(). */
void bootabi_dispatch(BootAbiParams *params, uint8_t drive) __attribute__((noreturn));

/* Fill in standard fields of BootAbiParams. */
void bootabi_params_init(BootAbiParams *p, BootAbiOsType type,
                         uint64_t lba, uint64_t pml4,
                         const char *cmdline, const char *kernel_path);

#endif /* BOOTABI_H */