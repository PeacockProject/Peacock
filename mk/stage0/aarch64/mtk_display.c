#include "mtk_display.h"

static mk_stage0_display_fail_stage_t g_last_fail_stage = MK_STAGE0_DISPLAY_FAIL_NONE;
static uint32_t g_last_fail_index;
static uint32_t g_last_fail_cmd;

static void clear_fail_state(void)
{
	g_last_fail_stage = MK_STAGE0_DISPLAY_FAIL_NONE;
	g_last_fail_index = 0U;
	g_last_fail_cmd = 0U;
}

static mk_stage0_display_status_t prepare_ilt7807s_hlt_jelly_hdp_vdo(const mk_stage0_panel_t *panel,
						      const mk_stage0_display_ctx_t *ctx)
{
	clear_fail_state();

	if (panel == 0 || ctx == 0) {
		return MK_STAGE0_DISPLAY_BAD_STATE;
	}
	return MK_STAGE0_DISPLAY_READY;
}

static mk_stage0_display_status_t prepare_nt35695b_fhd_cmd_auo_rt5081(const mk_stage0_panel_t *panel,
						      const mk_stage0_display_ctx_t *ctx)
{
	clear_fail_state();
	(void)panel;
	(void)ctx;
	return MK_STAGE0_DISPLAY_UNSUPPORTED;
}

mk_stage0_display_status_t mk_stage0_display_prepare(const mk_stage0_panel_t *panel,
						     const mk_stage0_display_ctx_t *ctx)
{
	if (ctx == 0) {
		return MK_STAGE0_DISPLAY_BAD_STATE;
	}

	if (ctx->videolfb_inited != 0) {
		return MK_STAGE0_DISPLAY_NOT_NEEDED;
	}

	if (panel == 0) {
		return MK_STAGE0_DISPLAY_UNSUPPORTED;
	}

	switch (panel->backend) {
	case MK_STAGE0_PANEL_BACKEND_ILT7807S_HLT_JELLY_HDP_VDO:
		return prepare_ilt7807s_hlt_jelly_hdp_vdo(panel, ctx);
	case MK_STAGE0_PANEL_BACKEND_NT35695B_FHD_CMD_AUO_RT5081:
		return prepare_nt35695b_fhd_cmd_auo_rt5081(panel, ctx);
	case MK_STAGE0_PANEL_BACKEND_NONE:
	default:
		return MK_STAGE0_DISPLAY_UNSUPPORTED;
	}
}

const char *mk_stage0_display_status_string(mk_stage0_display_status_t status)
{
	switch (status) {
	case MK_STAGE0_DISPLAY_NOT_NEEDED:
		return "already-inited";
	case MK_STAGE0_DISPLAY_READY:
		return "ready";
	case MK_STAGE0_DISPLAY_PENDING:
		return "pending";
	case MK_STAGE0_DISPLAY_UNSUPPORTED:
		return "unsupported";
	case MK_STAGE0_DISPLAY_BAD_STATE:
		return "bad-state";
	default:
		return "unknown";
	}
}

mk_stage0_display_fail_stage_t mk_stage0_display_last_fail_stage(void)
{
	return g_last_fail_stage;
}

uint32_t mk_stage0_display_last_fail_index(void)
{
	return g_last_fail_index;
}

uint32_t mk_stage0_display_last_fail_cmd(void)
{
	return g_last_fail_cmd;
}
