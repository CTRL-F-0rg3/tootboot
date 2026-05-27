/* linux.h — Linux bzImage loader */

#ifndef LINUX_H
#define LINUX_H

#include "bootabi.h"

/* Linux boot protocol magic (offset 0x1FE in bzImage) */
#define LINUX_BOOT_MAGIC     0xAA55
/* "HdrS" at offset 0x202 */
#define LINUX_HDR_MAGIC      0x53726448

/* Minimal Linux boot protocol header (setup header at offset 0x1F1) */
typedef struct __attribute__((packed)) {
    uint8_t  setup_sects;       /* number of setup sectors - 1 */
    uint16_t root_flags;
    uint32_t syssize;
    uint16_t ram_size;
    uint16_t vid_mode;
    uint16_t root_dev;
    uint16_t boot_flag;         /* 0xAA55 */
    uint16_t jump;
    uint32_t header;            /* "HdrS" = 0x53726448 */
    uint16_t version;           /* boot protocol version */
    uint32_t realmode_swtch;
    uint16_t start_sys_seg;
    uint16_t kernel_version;
    uint8_t  type_of_loader;
    uint8_t  loadflags;
    uint16_t setup_move_size;
    uint32_t code32_start;      /* kernel 32-bit entry */
    uint32_t ramdisk_image;
    uint32_t ramdisk_size;
    uint32_t bootsect_kludge;
    uint16_t heap_end_ptr;
    uint8_t  ext_loader_ver;
    uint8_t  ext_loader_type;
    uint32_t cmd_line_ptr;      /* pointer to command line */
    uint32_t initrd_addr_max;
    uint32_t kernel_alignment;
    uint8_t  relocatable_kernel;
    uint8_t  min_alignment;
    uint16_t xloadflags;
    uint32_t cmdline_size;
    uint32_t hardware_subarch;
    uint64_t hardware_subarch_data;
    uint32_t payload_offset;
    uint32_t payload_length;
    uint64_t setup_data;
    uint64_t pref_address;
    uint32_t init_size;
    uint32_t handover_offset;
} LinuxSetupHeader;

/* Register Linux loader into BootABI registry */
void linux_loader_init(void);

extern const BootAbiOps linux_loader_ops;

#endif /* LINUX_H */