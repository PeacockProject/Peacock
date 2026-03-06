#include <stdio.h>
#include "mk/hal_driver.h"
#include "../mtk/common_block.h"
#include "../mtk/common_display.h"

static mtk_block_ctx_t g_ctx;
static mtk_display_ctx_t g_display;

static mk_status_t mt6761_open_hook(mtk_block_ctx_t *ctx)
{
	(void) ctx;
	return MK_OK;
}

static mk_status_t mt6761_init(const mk_device_profile_t *profile, const char *block_path)
{
	mk_status_t rc;

	rc = mtk_display_init(&g_display, "mt6761", profile);
	if (rc != MK_OK) {
		return rc;
	}
	printf("mk_hal[mt6761]: display init panel=%s\n", mtk_display_selected_panel(&g_display));

	return mtk_block_open(&g_ctx, "mt6761", profile, block_path, mt6761_open_hook);
}

static mk_status_t mt6761_read_blocks(uint64_t lba, uint32_t count, void *out_buf, size_t out_size)
{
	return mtk_block_read(&g_ctx, lba, count, out_buf, out_size);
}

static uint32_t mt6761_block_size(void)
{
	return mtk_block_size(&g_ctx);
}

static void mt6761_reboot_recovery(void)
{
	printf("mk_hal[mt6761]: reboot -> recovery (stub)\n");
}

static void mt6761_reboot_bootloader(void)
{
	printf("mk_hal[mt6761]: reboot -> bootloader (stub)\n");
}

const mk_hal_driver_t mk_hal_driver_mt6761 = {
	.soc_name = "mt6761",
	.init = mt6761_init,
	.read_blocks = mt6761_read_blocks,
	.block_size = mt6761_block_size,
	.reboot_recovery = mt6761_reboot_recovery,
	.reboot_bootloader = mt6761_reboot_bootloader,
};
