#ifndef MK_HAL_DRIVER_H
#define MK_HAL_DRIVER_H

#include <stddef.h>
#include <stdint.h>
#include "mk/common.h"
#include "mk/hal.h"

typedef struct {
	const char *soc_name;
	mk_status_t (*init)(const mk_device_profile_t *profile, const char *block_path);
	mk_status_t (*read_blocks)(uint64_t lba, uint32_t count, void *out_buf, size_t out_size);
	uint32_t (*block_size)(void);
	void (*reboot_recovery)(void);
	void (*reboot_bootloader)(void);
} mk_hal_driver_t;

#endif
