/* boot1.h — TootBoot stage 1 interface */

#ifndef BOOT1_H
#define BOOT1_H

/* Entry point called from boot0.asm
 * rdi = TootBoot ABI header
 * rsi = TOOTBOOT_MAGIC
 * rdx = PML4 address
 */
void boot1_entry_point(void);

#endif /* BOOT1_H */