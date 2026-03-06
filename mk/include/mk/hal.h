#ifndef MK_HAL_H
#define MK_HAL_H

#include <stddef.h>
#include <stdint.h>
#include "mk/common.h"

typedef struct {
	const char *name;
	const char *soc;
	const char *default_boot_partition;
	const char *default_root_partition;
	const char *const *block_probe_paths;
	size_t block_probe_path_count;
	const char *const *panel_compatibles;
	size_t panel_compatible_count;
	const char *phoenix_bootstage;
	const char *phoenix_primary_partition;
	const char *phoenix_fallback_partition;
	uint64_t phoenix_ufs_offset;
	uint64_t phoenix_emmc_offset;
	const char *phoenix_record_magic;
} mk_device_profile_t;

void mk_hal_set_soc_name(const char *soc_name);
void mk_hal_set_block_device_path(const char *path);
mk_status_t mk_hal_init(const mk_device_profile_t *profile);
mk_status_t mk_hal_read_blocks(uint64_t lba, uint32_t count, void *out_buf, size_t out_size);
uint32_t mk_hal_block_size(void);
void mk_hal_reboot_recovery(void);
void mk_hal_reboot_bootloader(void);

#endif
