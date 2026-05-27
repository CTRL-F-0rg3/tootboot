/* tan.h — TanOS kernel loader */

#ifndef TAN_H
#define TAN_H

#include "bootabi.h"

/* TanOS kernel image magic (first 4 bytes of kernel binary) */
#define TANOS_KERNEL_MAGIC  0x544E4F53   /* 'TNOS' */

/* TanOS kernel header (must match TanOS kernel source) */
typedef struct __attribute__((packed)) {
    uint32_t magic;          /* TANOS_KERNEL_MAGIC */
    uint32_t version;        /* kernel version */
    uint64_t load_address;   /* where to load the kernel */
    uint64_t entry_point;    /* kernel entry point */
    uint32_t image_size;     /* total image size in bytes */
    uint8_t  reserved[12];
} TanOsKernelHeader;

/* Register TanOS loader into BootABI registry */
void tan_loader_init(void);

extern const BootAbiOps tan_loader_ops;

#endif /* TAN_H */