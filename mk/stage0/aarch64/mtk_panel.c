#include "mtk_panel.h"
static int str_eq_local(const char *a, const char *b)
{
	uint32_t i = 0;

	if (a == 0 || b == 0) {
		return 0;
	}

	while (a[i] != '\0' && b[i] != '\0') {
		if (a[i] != b[i]) {
			return 0;
		}
		i++;
	}

	return a[i] == b[i];
}

static const mk_stage0_panel_t g_panels[] = {
	{
		.id = MK_STAGE0_PANEL_ID_ILT7807S_HLT_JELLY_HDP_DSI_VDO,
		.backend = MK_STAGE0_PANEL_BACKEND_ILT7807S_HLT_JELLY_HDP_VDO,
		.fb_width = 720U,
		.fb_height = 1600U,
		.fb_align = 32U,
		.dsi_lanes = 3U,
		.dsi_packet_size = 256U,
		.dsi_pll_clock_cmd = 296U,
		.dsi_pll_clock_vdo = 296U,
		.dsi_vsync_active = 2U,
		.dsi_vback_porch = 16U,
		.dsi_vfront_porch = 32U,
		.dsi_hsync_active = 12U,
		.dsi_hback_porch = 78U,
		.dsi_hfront_porch = 74U,
		.lane_swap_enable = 0U,
		.lane_swap = {0U, 1U, 2U, 3U, 4U, 1U},
		.reset_delay0_ms = 2U,
		.reset_delay1_ms = 5U,
		.reset_delay2_ms = 10U,
		.reset_delay3_ms = 0U,
		.reset_gpio = 45U,
		.bias_enp_gpio = 150U,
		.bias_enn_gpio = 151U,
		.vddio_gpio = MK_STAGE0_GPIO_NONE,
		.runtime_fb_addr = 0x7b580000ULL,
		/*
		 * The reserved carveout is 3 pages, but the active mtkfb virtual
		 * surface is 736x3200 -> 2 visible pages. Touching the third page
		 * causes a second distorted redraw.
		 */
		.runtime_fb_size = 0x008fc800ULL,
		.runtime_fb_stride = 2944U,
		.lcd_bias_i2c_base = 0x1100f000ULL,
		.lcd_bias_i2c_addr = 0x3eU,
		.lcd_bias_voltage_reg = 0x0fU,
		.lcd_bias_ctrl3_reg = 0x43U,
		.dsi_mode = MK_STAGE0_DSI_MODE_SYNC_PULSE_VDO,
	},
	{
		.id = MK_STAGE0_PANEL_ID_NT35695B_FHD_DSI_CMD_AUO_RT5081,
		.backend = MK_STAGE0_PANEL_BACKEND_NT35695B_FHD_CMD_AUO_RT5081,
		/*
		 * The boot tag name is the legacy base string, but the live panel on
		 * this board is the HD+ 20:9 variant. TWRP reports 720x1600 and the
		 * matching vendor LCM driver is the hdp_20_9 variant.
		 */
		.fb_width = 720U,
		.fb_height = 1600U,
		.fb_align = 32U,
		.dsi_lanes = 4U,
		.dsi_packet_size = 256U,
		.dsi_pll_clock_cmd = 420U,
		.dsi_pll_clock_vdo = 440U,
		.dsi_vsync_active = 2U,
		.dsi_vback_porch = 8U,
		.dsi_vfront_porch = 40U,
		.dsi_hsync_active = 10U,
		.dsi_hback_porch = 20U,
		.dsi_hfront_porch = 40U,
		.lane_swap_enable = 1U,
		.lane_swap = {4U, 2U, 3U, 0U, 1U, 1U},
		.reset_delay0_ms = 15U,
		.reset_delay1_ms = 1U,
		.reset_delay2_ms = 10U,
		.reset_delay3_ms = 10U,
		.reset_gpio = 45U,
		.bias_enp_gpio = 150U,
		.bias_enn_gpio = 151U,
		.vddio_gpio = MK_STAGE0_GPIO_NONE,
		.runtime_fb_addr = 0U,
		.runtime_fb_size = 0U,
		.runtime_fb_stride = 0U,
		.lcd_bias_i2c_base = 0x1100f000ULL,
		.lcd_bias_i2c_addr = 0x3eU,
		.lcd_bias_voltage_reg = 0x14U,
		.lcd_bias_ctrl3_reg = 0x43U,
		.dsi_mode = MK_STAGE0_DSI_MODE_CMD,
	},
};

const mk_stage0_panel_t *mk_stage0_panel_find(const char *boot_name)
{
	uint32_t i;

	if (boot_name == 0 || boot_name[0] == '\0') {
		return 0;
	}

	for (i = 0; i < (sizeof(g_panels) / sizeof(g_panels[0])); i++) {
		if (str_eq_local(boot_name, mk_stage0_panel_name(&g_panels[i]))) {
			return &g_panels[i];
		}
	}

	return 0;
}

const mk_stage0_panel_t *mk_stage0_panel_resolve(const char *boot_name,
						 const char *target_name)
{
	const mk_stage0_panel_t *panel;

	panel = mk_stage0_panel_find(target_name);
	if (panel != 0) {
		return panel;
	}

	return mk_stage0_panel_find(boot_name);
}

const char *mk_stage0_panel_name(const mk_stage0_panel_t *panel)
{
	if (panel == 0) {
		return 0;
	}

	switch (panel->id) {
	case MK_STAGE0_PANEL_ID_ILT7807S_HLT_JELLY_HDP_DSI_VDO:
		return "ilt7807s_hlt_jelly_hdp_dsi_vdo_lcm";
	case MK_STAGE0_PANEL_ID_NT35695B_FHD_DSI_CMD_AUO_RT5081:
		return "nt35695B_fhd_dsi_cmd_auo_rt5081_drv";
	case MK_STAGE0_PANEL_ID_NONE:
	default:
		return 0;
	}
}

const char *mk_stage0_panel_backend_name(const mk_stage0_panel_t *panel)
{
	if (panel == 0) {
		return 0;
	}

	switch (panel->backend) {
	case MK_STAGE0_PANEL_BACKEND_ILT7807S_HLT_JELLY_HDP_VDO:
		return "ilt7807s-hlt-jelly-hdp-vdo";
	case MK_STAGE0_PANEL_BACKEND_NT35695B_FHD_CMD_AUO_RT5081:
		return "nt35695b-fhd-cmd-auo-rt5081";
	case MK_STAGE0_PANEL_BACKEND_NONE:
	default:
		return 0;
	}
}
