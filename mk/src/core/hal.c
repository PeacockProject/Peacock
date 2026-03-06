#include <stdio.h>
#include <stdlib.h>
#include "mk/hal.h"
#include "mk/hal_driver.h"

const mk_hal_driver_t *mk_soc_registry_find(const char *soc_name);

static const char *g_soc_name;
static const char *g_block_path;
static const mk_hal_driver_t *g_driver;

void mk_hal_set_soc_name(const char *soc_name)
{
	g_soc_name = soc_name;
}

void mk_hal_set_block_device_path(const char *path)
{
	g_block_path = path;
}

mk_status_t mk_hal_init(const mk_device_profile_t *profile)
{
	const char *selected_soc;

	if (profile == NULL) {
		return MK_ERR;
	}

	selected_soc = g_soc_name;
	if (selected_soc == NULL || selected_soc[0] == '\0') {
		const char *env_soc = getenv("MK_SOC");
		if (env_soc != NULL && env_soc[0] != '\0') {
			selected_soc = env_soc;
		} else {
			selected_soc = profile->soc;
		}
	}

	if (selected_soc == NULL || selected_soc[0] == '\0') {
		fprintf(stderr, "mk_hal: no SoC selected\n");
		return MK_ERR;
	}

	if (g_block_path == NULL || g_block_path[0] == '\0') {
		const char *env_block = getenv("MK_BLOCK_DEV");
		if (env_block != NULL && env_block[0] != '\0') {
			g_block_path = env_block;
		}
	}

	g_driver = mk_soc_registry_find(selected_soc);
	if (g_driver == NULL) {
		fprintf(stderr, "mk_hal: unsupported SoC '%s'\n", selected_soc);
		return MK_ERR_NOT_FOUND;
	}

	return g_driver->init(profile, g_block_path);
}

mk_status_t mk_hal_read_blocks(uint64_t lba, uint32_t count, void *out_buf, size_t out_size)
{
	if (g_driver == NULL || g_driver->read_blocks == NULL) {
		return MK_ERR;
	}
	return g_driver->read_blocks(lba, count, out_buf, out_size);
}

uint32_t mk_hal_block_size(void)
{
	if (g_driver == NULL || g_driver->block_size == NULL) {
		return 512U;
	}
	return g_driver->block_size();
}

void mk_hal_reboot_recovery(void)
{
	if (g_driver != NULL && g_driver->reboot_recovery != NULL) {
		g_driver->reboot_recovery();
	}
}

void mk_hal_reboot_bootloader(void)
{
	if (g_driver != NULL && g_driver->reboot_bootloader != NULL) {
		g_driver->reboot_bootloader();
	}
}
