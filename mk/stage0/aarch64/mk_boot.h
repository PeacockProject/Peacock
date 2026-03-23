#ifndef MK_BOOT_H
#define MK_BOOT_H

#include <stdint.h>

/*
 * Attempt to boot the Linux kernel from the Peacock boot partition.
 * Reads extlinux/extlinux.conf, loads and decompresses the kernel,
 * loads the initramfs, patches the FDT, and jumps.
 * Does not return on success. Returns on failure.
 */
void mk_boot_linux(uint64_t fdt_ptr, uint64_t boot_lba);
void mk_boot_linux_override_kernel(uint64_t fdt_ptr, uint64_t boot_lba,
				   const uint8_t *kernel_buf,
				   uint32_t kernel_size);
void mk_stage0_display_restore_for_linux(void);
void mk_stage0_msdc_restore_for_linux(void);
void mk_stage0_wdt_restore_for_linux(void);

#endif /* MK_BOOT_H */
