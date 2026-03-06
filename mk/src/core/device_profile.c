#include <string.h>
#include "mk/device_profile.h"

static const char *const g_oa16_block_paths[] = {
	"/dev/block/by-name/userdata",
	"/dev/block/bootdevice/by-name/userdata",
	"/dev/block/platform/bootdevice/by-name/userdata",
	"/dev/block/platform/mtk-msdc.0/by-name/userdata",
};

static const char *const g_oa16_panels[] = {
	"panel-ili7807",
	"panel-otm1911",
};

static const char *const g_mt6761_ref_block_paths[] = {
	"/dev/block/by-name/userdata",
	"/dev/block/bootdevice/by-name/userdata",
	"/dev/block/platform/bootdevice/by-name/userdata",
	"/dev/block/platform/mtk-msdc.0/by-name/userdata",
};

static const char *const g_mt6761_ref_panels[] = {
	"panel-generic",
};

static const mk_device_profile_t g_profiles[] = {
	{
		.name = "oppo-a16",
		.soc = "mt6765",
		.default_boot_partition = "PEACOCK_BOOT",
		.default_root_partition = "PEACOCK_ROOTFS",
		.block_probe_paths = g_oa16_block_paths,
		.block_probe_path_count = sizeof(g_oa16_block_paths) / sizeof(g_oa16_block_paths[0]),
		.panel_compatibles = g_oa16_panels,
		.panel_compatible_count = sizeof(g_oa16_panels) / sizeof(g_oa16_panels[0]),
		.phoenix_bootstage = "NATIVE_INIT_POST_FS",
		.phoenix_primary_partition = "oplusreserve1",
		.phoenix_fallback_partition = "opporeserve1",
		.phoenix_ufs_offset = 0x456000ULL,
		.phoenix_emmc_offset = 0x440000ULL,
		.phoenix_record_magic = "kernelog",
	},
	{
		.name = "mt6761-ref",
		.soc = "mt6761",
		.default_boot_partition = "PEACOCK_BOOT",
		.default_root_partition = "PEACOCK_ROOTFS",
		.block_probe_paths = g_mt6761_ref_block_paths,
		.block_probe_path_count = sizeof(g_mt6761_ref_block_paths) / sizeof(g_mt6761_ref_block_paths[0]),
		.panel_compatibles = g_mt6761_ref_panels,
		.panel_compatible_count = sizeof(g_mt6761_ref_panels) / sizeof(g_mt6761_ref_panels[0]),
		.phoenix_bootstage = NULL,
		.phoenix_primary_partition = NULL,
		.phoenix_fallback_partition = NULL,
		.phoenix_ufs_offset = 0,
		.phoenix_emmc_offset = 0,
		.phoenix_record_magic = NULL,
	},
};

const mk_device_profile_t *mk_device_profile_find(const char *name)
{
	size_t i;

	if (name == NULL || name[0] == '\0') {
		return NULL;
	}

	for (i = 0; i < (sizeof(g_profiles) / sizeof(g_profiles[0])); i++) {
		if (strcmp(g_profiles[i].name, name) == 0) {
			return &g_profiles[i];
		}
	}

	return NULL;
}

const mk_device_profile_t *mk_device_profile_default(void)
{
	return &g_profiles[0];
}

size_t mk_device_profile_count(void)
{
	return sizeof(g_profiles) / sizeof(g_profiles[0]);
}

const mk_device_profile_t *mk_device_profile_at(size_t index)
{
	if (index >= (sizeof(g_profiles) / sizeof(g_profiles[0]))) {
		return NULL;
	}
	return &g_profiles[index];
}
